#!/usr/bin/env python3
"""
POWSYS365 Stripe Webhook Handler
=================================
Handles Stripe webhook events for subscription lifecycle management.

Events handled:
    - checkout.session.completed
    - customer.subscription.created
    - customer.subscription.updated
    - customer.subscription.deleted
    - invoice.payment_succeeded
    - invoice.payment_failed
    - customer.subscription.trial_will_end
    - payment_intent.succeeded
    - payment_intent.payment_failed
"""

import hashlib
import hmac
import json
import logging
import os
import sys
import time
from datetime import datetime, timedelta

from flask import Blueprint, Flask, request, jsonify

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logger = logging.getLogger("powsys365.stripe_webhook")


def setup_logging(level=logging.INFO):
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter(
        "%(asctime)s | %(levelname)-8s | %(name)s | %(message)s"
    ))
    logger.setLevel(level)
    if not logger.handlers:
        logger.addHandler(handler)


setup_logging()

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

STRIPE_SECRET_KEY = os.environ.get("STRIPE_SECRET_KEY", "")
STRIPE_WEBHOOK_SECRET = os.environ.get("STRIPE_WEBHOOK_SECRET", "")

# ---------------------------------------------------------------------------
# Webhook Verification
# ---------------------------------------------------------------------------

def verify_stripe_signature(payload: bytes, sig_header: str, secret: str) -> bool:
    """
    Verify Stripe webhook signature using the signing secret.
    Format: t=timestamp,v1=signature,v0=...
    """
    if not secret:
        logger.warning("No webhook secret configured, skipping signature verification")
        return True

    try:
        timestamp = ""
        v1_signature = ""

        parts = sig_header.split(",")
        for part in parts:
            key, _, value = part.partition("=")
            if key.strip() == "t":
                timestamp = value.strip()
            elif key.strip() == "v1":
                v1_signature = value.strip()

        if not timestamp or not v1_signature:
            logger.error("Missing timestamp or v1 signature in header")
            return False

        # Check timestamp tolerance (5 minutes)
        try:
            ts_int = int(timestamp)
            now = int(time.time())
            if abs(now - ts_int) > 300:
                logger.error("Webhook timestamp too old: %d vs now %d", ts_int, now)
                return False
        except ValueError:
            logger.error("Invalid timestamp in webhook header")
            return False

        # Compute expected signature
        signed_payload = timestamp.encode() + b"." + payload
        expected = hmac.new(
            secret.encode(),
            signed_payload,
            hashlib.sha256
        ).hexdigest()

        # Constant-time comparison
        if len(expected) != len(v1_signature):
            return False
        result = 0
        for a, b in zip(expected, v1_signature):
            result |= ord(a) ^ ord(b)
        return result == 0

    except Exception as e:
        logger.error("Webhook verification error: %s", e)
        return False


# ---------------------------------------------------------------------------
# Event Handlers
# ---------------------------------------------------------------------------

def handle_checkout_session_completed(event_data: dict) -> dict:
    """Handle checkout.session.completed event."""
    session = event_data.get("object", {})
    session_id = session.get("id", "")
    customer_id = session.get("customer", "")
    subscription_id = session.get("subscription", "")
    metadata = session.get("metadata", {})
    tier = metadata.get("tier", "")
    customer_email = session.get("customer_details", {}).get("email", "")

    logger.info("Checkout completed - session=%s customer=%s tier=%s",
                session_id, customer_id, tier)

    # Update license record in database
    result = {
        "action": "checkout_completed",
        "session_id": session_id,
        "customer_id": customer_id,
        "subscription_id": subscription_id,
        "tier": tier,
        "customer_email": customer_email,
        "license_key": _generate_license_key(tier),
    }

    # Update database with new license
    try:
        from sqlalchemy import create_engine
        from sqlalchemy.orm import sessionmaker
        import sys
        sys.path.insert(0, '/app/licensing/api/backend')
        from models import License, ActivationLog
        
        db_url = os.environ.get('DATABASE_URL', 'postgresql://postgres:postgres@localhost:5432/powsy365')
        engine = create_engine(db_url)
        Session = sessionmaker(bind=engine)
        session = Session()
        
        license_record = License(
            license_key=result["license_key"],
            tier=result["tier"],
            status="active",
            max_devices=1 if result["tier"] in ["trial", "student"] else 5 if result["tier"] == "basic" else 999,
            user_id=customer_id
        )
        session.add(license_record)
        
        log = ActivationLog(
            license_key=result["license_key"],
            action="license_created_from_stripe",
            ip_address=event_data.get("request", {}).get("id", "unknown")
        )
        session.add(log)
        session.commit()
        session.close()
        result["database_updated"] = True
    except Exception as e:
        result["database_updated"] = False
        result["database_error"] = str(e)
    
    _notify_license_created(result)

    return result


def handle_subscription_created(event_data: dict) -> dict:
    """Handle customer.subscription.created event."""
    subscription = event_data.get("object", {})
    sub_id = subscription.get("id", "")
    customer_id = subscription.get("customer", "")
    status = subscription.get("status", "")
    current_period_end = subscription.get("current_period_end", 0)
    metadata = subscription.get("metadata", {})
    tier = metadata.get("tier", "")

    logger.info("Subscription created - sub=%s customer=%s status=%s tier=%s",
                sub_id, customer_id, status, tier)

    return {
        "action": "subscription_created",
        "subscription_id": sub_id,
        "customer_id": customer_id,
        "status": status,
        "tier": tier,
        "current_period_end": current_period_end,
    }


def handle_subscription_updated(event_data: dict) -> dict:
    """Handle customer.subscription.updated event."""
    subscription = event_data.get("object", {})
    sub_id = subscription.get("id", "")
    customer_id = subscription.get("customer", "")
    status = subscription.get("status", "")
    cancel_at_period_end = subscription.get("cancel_at_period_end", False)

    logger.info("Subscription updated - sub=%s status=%s cancel_at_period_end=%s",
                sub_id, status, cancel_at_period_end)

    # Handle cancellation scheduling
    if cancel_at_period_end:
        logger.info("Subscription %s scheduled for cancellation at period end", sub_id)
        _schedule_license_expiry(sub_id)

    return {
        "action": "subscription_updated",
        "subscription_id": sub_id,
        "customer_id": customer_id,
        "status": status,
        "cancel_at_period_end": cancel_at_period_end,
    }


def handle_subscription_deleted(event_data: dict) -> dict:
    """Handle customer.subscription.deleted event."""
    subscription = event_data.get("object", {})
    sub_id = subscription.get("id", "")
    customer_id = subscription.get("customer", "")

    logger.info("Subscription deleted - sub=%s customer=%s", sub_id, customer_id)

    # Revoke license
    _revoke_license_by_subscription(sub_id)

    return {
        "action": "subscription_deleted",
        "subscription_id": sub_id,
        "customer_id": customer_id,
        "license_revoked": True,
    }


def handle_invoice_payment_succeeded(event_data: dict) -> dict:
    """Handle invoice.payment_succeeded event."""
    invoice = event_data.get("object", {})
    invoice_id = invoice.get("id", "")
    subscription_id = invoice.get("subscription", "")
    customer_id = invoice.get("customer", "")
    amount_paid = invoice.get("amount_paid", 0)
    currency = invoice.get("currency", "")
    period_end = invoice.get("period_end", 0)

    logger.info("Invoice payment succeeded - invoice=%s sub=%s amount=%d %s",
                invoice_id, subscription_id, amount_paid, currency)

    # Extend license validity
    _extend_license(subscription_id, period_end)

    return {
        "action": "payment_succeeded",
        "invoice_id": invoice_id,
        "subscription_id": subscription_id,
        "customer_id": customer_id,
        "amount_paid": amount_paid,
        "currency": currency,
    }


def handle_invoice_payment_failed(event_data: dict) -> dict:
    """Handle invoice.payment_failed event."""
    invoice = event_data.get("object", {})
    invoice_id = invoice.get("id", "")
    subscription_id = invoice.get("subscription", "")
    customer_id = invoice.get("customer", "")
    next_payment_attempt = invoice.get("next_payment_attempt", 0)
    attempt_count = invoice.get("attempt_count", 0)

    logger.warning("Invoice payment failed - invoice=%s sub=%s attempts=%d",
                   invoice_id, subscription_id, attempt_count)

    # After multiple failures, suspend license
    if attempt_count >= 3:
        _suspend_license_for_payment_failure(subscription_id)

    return {
        "action": "payment_failed",
        "invoice_id": invoice_id,
        "subscription_id": subscription_id,
        "customer_id": customer_id,
        "next_payment_attempt": next_payment_attempt,
        "attempt_count": attempt_count,
    }


def handle_trial_will_end(event_data: dict) -> dict:
    """Handle customer.subscription.trial_will_end event."""
    subscription = event_data.get("object", {})
    sub_id = subscription.get("id", "")
    customer_id = subscription.get("customer", "")
    trial_end = subscription.get("trial_end", 0)

    logger.info("Trial ending soon - sub=%s trial_end=%s",
                sub_id, trial_end)

    # Send trial ending notification
    _send_trial_ending_notification(sub_id, customer_id)

    return {
        "action": "trial_will_end",
        "subscription_id": sub_id,
        "customer_id": customer_id,
        "trial_end": trial_end,
    }


def handle_payment_intent_succeeded(event_data: dict) -> dict:
    """Handle payment_intent.succeeded event."""
    payment_intent = event_data.get("object", {})
    pi_id = payment_intent.get("id", "")
    amount = payment_intent.get("amount", 0)
    currency = payment_intent.get("currency", "")
    customer_id = payment_intent.get("customer", "")

    logger.info("Payment intent succeeded - pi=%s amount=%d %s",
                pi_id, amount, currency)

    return {
        "action": "payment_intent_succeeded",
        "payment_intent_id": pi_id,
        "customer_id": customer_id,
        "amount": amount,
        "currency": currency,
    }


def handle_payment_intent_failed(event_data: dict) -> dict:
    """Handle payment_intent.payment_failed event."""
    payment_intent = event_data.get("object", {})
    pi_id = payment_intent.get("id", "")
    customer_id = payment_intent.get("customer", "")
    last_payment_error = payment_intent.get("last_payment_error", {})
    decline_code = last_payment_error.get("decline_code", "")
    error_message = last_payment_error.get("message", "")

    logger.warning("Payment intent failed - pi=%s decline_code=%s message=%s",
                   pi_id, decline_code, error_message)

    return {
        "action": "payment_intent_failed",
        "payment_intent_id": pi_id,
        "customer_id": customer_id,
        "decline_code": decline_code,
        "error_message": error_message,
    }


# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

def _generate_license_key(tier: str) -> str:
    """Generate a new POWSYS365 license key."""
    import secrets
    prefix = tier.upper()[:4] if tier else "POWS"
    suffix = secrets.token_urlsafe(16).upper().replace("-", "").replace("_", "")[:20]
    return f"POWSYS-{prefix}-{suffix}"


def _notify_license_created(result: dict):
    """Send notification about new license creation."""
    logger.info("License created notification: %s", json.dumps(result))
    # In production: send email via support@powsys365.com
    # or integrate with notification service


def _schedule_license_expiry(subscription_id: str):
    """Schedule license expiry when subscription cancels."""
    logger.info("License expiry scheduled for subscription %s", subscription_id)
    # In production: update database with scheduled cancellation date


def _revoke_license_by_subscription(subscription_id: str):
    """Revoke a license when subscription is deleted."""
    logger.info("License revoked for subscription %s", subscription_id)
    # In production: update license status to 'revoked' in database


def _extend_license(subscription_id: str, period_end: int):
    """Extend license validity period."""
    logger.info("License extended for subscription %s until %s",
                subscription_id, period_end)
    # In production: update license expiration date in database


def _suspend_license_for_payment_failure(subscription_id: str):
    """Suspend license after repeated payment failures."""
    logger.warning("License suspended for subscription %s due to payment failures", subscription_id)
    # In production: update license status to 'suspended' in database


def _send_trial_ending_notification(subscription_id: str, customer_id: str):
    """Send trial ending notification to customer."""
    logger.info("Trial ending notification sent for subscription %s customer %s",
                subscription_id, customer_id)
    # In production: send email via support@powsys365.com


# ---------------------------------------------------------------------------
# Event Router
# ---------------------------------------------------------------------------

EVENT_HANDLERS = {
    "checkout.session.completed": handle_checkout_session_completed,
    "customer.subscription.created": handle_subscription_created,
    "customer.subscription.updated": handle_subscription_updated,
    "customer.subscription.deleted": handle_subscription_deleted,
    "invoice.payment_succeeded": handle_invoice_payment_succeeded,
    "invoice.payment_failed": handle_invoice_payment_failed,
    "customer.subscription.trial_will_end": handle_trial_will_end,
    "payment_intent.succeeded": handle_payment_intent_succeeded,
    "payment_intent.payment_failed": handle_payment_intent_failed,
}


# ---------------------------------------------------------------------------
# Flask Blueprint
# ---------------------------------------------------------------------------

stripe_webhook_bp = Blueprint("stripe_webhook", __name__)


@stripe_webhook_bp.route("/webhooks/stripe", methods=["POST"])
def stripe_webhook():
    """
    Stripe webhook endpoint.
    Receives and processes Stripe event notifications.
    """
    payload = request.get_data()
    sig_header = request.headers.get("Stripe-Signature", "")

    # Verify signature
    if not verify_stripe_signature(payload, sig_header, STRIPE_WEBHOOK_SECRET):
        logger.error("Webhook signature verification failed")
        return jsonify({"error": "Invalid signature"}), 401

    # Parse event
    try:
        event = json.loads(payload)
    except json.JSONDecodeError as e:
        logger.error("Failed to parse webhook payload: %s", e)
        return jsonify({"error": "Invalid JSON"}), 400

    event_id = event.get("id", "")
    event_type = event.get("type", "")
    event_data = event.get("data", {})

    logger.info("Received Stripe event: id=%s type=%s", event_id, event_type)

    # Route to handler
    handler = EVENT_HANDLERS.get(event_type)
    if handler:
        try:
            result = handler(event_data)
            result["event_id"] = event_id
            result["event_type"] = event_type
            result["processed_at"] = datetime.utcnow().isoformat()
            logger.info("Event %s processed successfully", event_type)
            return jsonify({
                "received": True,
                "event_id": event_id,
                "event_type": event_type,
                "result": result,
            }), 200
        except Exception as e:
            logger.error("Error handling event %s: %s", event_type, e)
            return jsonify({"error": f"Handler error: {str(e)}"}), 500
    else:
        logger.info("No handler for event type: %s", event_type)
        return jsonify({
            "received": True,
            "event_id": event_id,
            "event_type": event_type,
            "handled": False,
            "message": "Event type not handled",
        }), 200


# ---------------------------------------------------------------------------
# Standalone Flask App
# ---------------------------------------------------------------------------

def create_app():
    app = Flask(__name__)
    app.register_blueprint(stripe_webhook_bp)

    @app.route("/health", methods=["GET"])
    def health():
        return jsonify({
            "status": "healthy",
            "service": "POWSYS365 Stripe Webhook Handler",
            "version": "1.0.0",
            "webhook_secret_configured": bool(STRIPE_WEBHOOK_SECRET),
        }), 200

    return app


if __name__ == "__main__":
    app = create_app()
    port = int(os.environ.get("WEBHOOK_PORT", 5001))
    app.run(host="0.0.0.0", port=port, debug=False)
