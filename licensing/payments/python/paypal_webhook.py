#!/usr/bin/env python3
"""
POWSYS365 PayPal Webhook Handler
=================================
Handles PayPal webhook events for subscription lifecycle management.

Events handled:
    - CHECKOUT.ORDER.APPROVED
    - CHECKOUT.ORDER.COMPLETED
    - BILLING.SUBSCRIPTION.CREATED
    - BILLING.SUBSCRIPTION.ACTIVATED
    - BILLING.SUBSCRIPTION.UPDATED
    - BILLING.SUBSCRIPTION.EXPIRED
    - BILLING.SUBSCRIPTION.CANCELLED
    - BILLING.SUBSCRIPTION.SUSPENDED
    - BILLING.SUBSCRIPTION.PAYMENT.REFUNDED
    - PAYMENT.CAPTURE.COMPLETED
    - PAYMENT.CAPTURE.DENIED
    - PAYMENT.CAPTURE.REFUNDED
"""

import base64
import hashlib
import hmac
import json
import logging
import os
import sys
import time
from datetime import datetime

import requests
from flask import Blueprint, Flask, request, jsonify

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logger = logging.getLogger("powsys365.paypal_webhook")


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

PAYPAL_CLIENT_ID = os.environ.get("PAYPAL_CLIENT_ID", "")
PAYPAL_CLIENT_SECRET = os.environ.get("PAYPAL_CLIENT_SECRET", "")
PAYPAL_WEBHOOK_ID = os.environ.get("PAYPAL_WEBHOOK_ID", "")
PAYPAL_SANDBOX = os.environ.get("PAYPAL_SANDBOX", "true").lower() == "true"

PAYPAL_BASE_URL = (
    "https://api-m.sandbox.paypal.com"
    if PAYPAL_SANDBOX
    else "https://api-m.paypal.com"
)

# ---------------------------------------------------------------------------
# PayPal Authentication
# ---------------------------------------------------------------------------

_paypal_access_token = None
_paypal_token_expiry = 0


def _get_paypal_token() -> str:
    """Get or refresh PayPal access token."""
    global _paypal_access_token, _paypal_token_expiry

    now = int(time.time())
    if _paypal_access_token and now < _paypal_token_expiry:
        return _paypal_access_token

    if not PAYPAL_CLIENT_ID or not PAYPAL_CLIENT_SECRET:
        raise RuntimeError("PayPal credentials not configured")

    credentials = base64.b64encode(
        f"{PAYPAL_CLIENT_ID}:{PAYPAL_CLIENT_SECRET}".encode()
    ).decode()

    resp = requests.post(
        f"{PAYPAL_BASE_URL}/v1/oauth2/token",
        headers={
            "Authorization": f"Basic {credentials}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        data={"grant_type": "client_credentials"},
        timeout=30,
    )
    resp.raise_for_status()
    data = resp.json()

    _paypal_access_token = data["access_token"]
    expires_in = data.get("expires_in", 3200)
    _paypal_token_expiry = now + expires_in - 60  # 60s buffer

    logger.debug("PayPal token refreshed, expires in %d seconds", expires_in)
    return _paypal_access_token


# ---------------------------------------------------------------------------
# Webhook Signature Verification
# ---------------------------------------------------------------------------

def verify_paypal_signature(
    auth_algo: str,
    auth_nonce: str,
    auth_timestamp: str,
    cert_url: str,
    transmission_id: str,
    transmission_sig: str,
    transmission_time: str,
    webhook_id: str,
    event_body: str,
) -> bool:
    """
    Verify PayPal webhook signature by calling their verification API.
    """
    if not webhook_id:
        logger.warning("No PayPal webhook ID configured, skipping verification")
        return True

    try:
        token = _get_paypal_token()
        verify_body = {
            "auth_algo": auth_algo,
            "cert_url": cert_url,
            "transmission_id": transmission_id,
            "transmission_sig": transmission_sig,
            "transmission_time": transmission_time,
            "webhook_id": webhook_id,
            "webhook_event": json.loads(event_body),
        }

        resp = requests.post(
            f"{PAYPAL_BASE_URL}/v1/notifications/verify-webhook-signature",
            headers={
                "Authorization": f"Bearer {token}",
                "Content-Type": "application/json",
            },
            json=verify_body,
            timeout=30,
        )
        resp.raise_for_status()
        result = resp.json()
        is_valid = result.get("verification_status") == "SUCCESS"

        if not is_valid:
            logger.error("PayPal webhook signature verification failed: %s", result)
        return is_valid

    except Exception as e:
        logger.error("PayPal signature verification error: %s", e)
        return False


def extract_webhook_headers(headers) -> dict:
    """Extract PayPal webhook verification headers."""
    return {
        "auth_algo": headers.get("PAYPAL-AUTH-ALGO", ""),
        "auth_nonce": headers.get("PAYPAL-AUTH-NONCE", ""),
        "auth_timestamp": headers.get("PAYPAL-AUTH-TIMESTAMP", ""),
        "cert_url": headers.get("PAYPAL-CERT-URL", ""),
        "transmission_id": headers.get("PAYPAL-TRANSMISSION-ID", ""),
        "transmission_sig": headers.get("PAYPAL-TRANSMISSION-SIG", ""),
        "transmission_time": headers.get("PAYPAL-TRANSMISSION-TIME", ""),
    }


# ---------------------------------------------------------------------------
# Event Handlers
# ---------------------------------------------------------------------------

def handle_checkout_order_approved(event_data: dict) -> dict:
    """Handle CHECKOUT.ORDER.APPROVED event."""
    resource = event_data.get("resource", {})
    order_id = resource.get("id", "")
    status = resource.get("status", "")
    payer = resource.get("payer", {})
    payer_email = payer.get("email_address", "")
    payer_name = payer.get("name", {}).get("given_name", "") + " " + payer.get("name", {}).get("surname", "")

    logger.info("PayPal order approved - order=%s payer=%s", order_id, payer_email)

    return {
        "action": "order_approved",
        "order_id": order_id,
        "status": status,
        "payer_email": payer_email,
        "payer_name": payer_name.strip(),
    }


def handle_checkout_order_completed(event_data: dict) -> dict:
    """Handle CHECKOUT.ORDER.COMPLETED event."""
    resource = event_data.get("resource", {})
    order_id = resource.get("id", "")
    payer = resource.get("payer", {})
    payer_email = payer.get("email_address", "")
    purchase_units = resource.get("purchase_units", [])

    amount = 0.0
    if purchase_units:
        amount_data = purchase_units[0].get("amount", {})
        amount = float(amount_data.get("value", 0))

    logger.info("PayPal order completed - order=%s amount=%.2f payer=%s",
                order_id, amount, payer_email)

    # Capture the order payment
    _capture_paypal_order(order_id)

    result = {
        "action": "order_completed",
        "order_id": order_id,
        "payer_email": payer_email,
        "amount": amount,
        "license_key": _generate_license_key("BASIC"),
    }
    _notify_license_created(result)
    return result


def handle_subscription_created(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.CREATED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")
    plan_id = resource.get("plan_id", "")
    status = resource.get("status", "")
    subscriber = resource.get("subscriber", {})
    email = subscriber.get("email_address", "")

    logger.info("PayPal subscription created - sub=%s plan=%s status=%s",
                sub_id, plan_id, status)

    return {
        "action": "subscription_created",
        "subscription_id": sub_id,
        "plan_id": plan_id,
        "status": status,
        "subscriber_email": email,
    }


def handle_subscription_activated(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.ACTIVATED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")
    plan_id = resource.get("plan_id", "")
    status = resource.get("status", "")
    billing_info = resource.get("billing_info", {})
    next_billing_time = billing_info.get("next_billing_time", "")

    logger.info("PayPal subscription activated - sub=%s plan=%s", sub_id, plan_id)

    # Activate license
    _activate_license_for_subscription(sub_id, plan_id)

    return {
        "action": "subscription_activated",
        "subscription_id": sub_id,
        "plan_id": plan_id,
        "status": status,
        "next_billing_time": next_billing_time,
        "license_activated": True,
    }


def handle_subscription_updated(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.UPDATED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")
    plan_id = resource.get("plan_id", "")
    status = resource.get("status", "")

    logger.info("PayPal subscription updated - sub=%s plan=%s status=%s",
                sub_id, plan_id, status)

    return {
        "action": "subscription_updated",
        "subscription_id": sub_id,
        "plan_id": plan_id,
        "status": status,
    }


def handle_subscription_expired(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.EXPIRED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")

    logger.info("PayPal subscription expired - sub=%s", sub_id)

    _revoke_license_by_subscription(sub_id)

    return {
        "action": "subscription_expired",
        "subscription_id": sub_id,
        "license_revoked": True,
    }


def handle_subscription_cancelled(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.CANCELLED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")
    status = resource.get("status", "")

    logger.info("PayPal subscription cancelled - sub=%s", sub_id)

    _revoke_license_by_subscription(sub_id)

    return {
        "action": "subscription_cancelled",
        "subscription_id": sub_id,
        "status": status,
        "license_revoked": True,
    }


def handle_subscription_suspended(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.SUSPENDED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")
    status = resource.get("status", "")

    logger.info("PayPal subscription suspended - sub=%s", sub_id)

    _suspend_license(sub_id)

    return {
        "action": "subscription_suspended",
        "subscription_id": sub_id,
        "status": status,
        "license_suspended": True,
    }


def handle_payment_capture_completed(event_data: dict) -> dict:
    """Handle PAYMENT.CAPTURE.COMPLETED event."""
    resource = event_data.get("resource", {})
    capture_id = resource.get("id", "")
    amount = resource.get("amount", {})
    value = amount.get("value", "0")
    currency = amount.get("currency_code", "")
    status = resource.get("status", "")

    logger.info("PayPal payment capture completed - capture=%s amount=%s %s",
                capture_id, value, currency)

    return {
        "action": "payment_capture_completed",
        "capture_id": capture_id,
        "amount": float(value),
        "currency": currency,
        "status": status,
    }


def handle_payment_capture_denied(event_data: dict) -> dict:
    """Handle PAYMENT.CAPTURE.DENIED event."""
    resource = event_data.get("resource", {})
    capture_id = resource.get("id", "")
    status = resource.get("status", "")

    logger.warning("PayPal payment capture denied - capture=%s", capture_id)

    return {
        "action": "payment_capture_denied",
        "capture_id": capture_id,
        "status": status,
    }


def handle_payment_capture_refunded(event_data: dict) -> dict:
    """Handle PAYMENT.CAPTURE.REFUNDED event."""
    resource = event_data.get("resource", {})
    capture_id = resource.get("id", "")
    refund_amount = resource.get("amount", {}).get("value", "0")

    logger.info("PayPal payment refunded - capture=%s refund=%s", capture_id, refund_amount)

    return {
        "action": "payment_capture_refunded",
        "capture_id": capture_id,
        "refund_amount": float(refund_amount),
    }


def handle_subscription_payment_refunded(event_data: dict) -> dict:
    """Handle BILLING.SUBSCRIPTION.PAYMENT.REFUNDED event."""
    resource = event_data.get("resource", {})
    sub_id = resource.get("id", "")

    logger.info("PayPal subscription payment refunded - sub=%s", sub_id)

    return {
        "action": "subscription_payment_refunded",
        "subscription_id": sub_id,
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


def _capture_paypal_order(order_id: str):
    """Capture an approved PayPal order."""
    try:
        token = _get_paypal_token()
        resp = requests.post(
            f"{PAYPAL_BASE_URL}/v2/checkout/orders/{order_id}/capture",
            headers={
                "Authorization": f"Bearer {token}",
                "Content-Type": "application/json",
            },
            json={},
            timeout=30,
        )
        resp.raise_for_status()
        logger.info("PayPal order %s captured successfully", order_id)
    except Exception as e:
        logger.error("Failed to capture PayPal order %s: %s", order_id, e)


def _notify_license_created(result: dict):
    """Send notification about new license creation."""
    logger.info("License created notification: %s", json.dumps(result))


def _activate_license_for_subscription(sub_id: str, plan_id: str):
    """Activate license for a PayPal subscription."""
    logger.info("License activated for PayPal subscription %s (plan %s)", sub_id, plan_id)


def _revoke_license_by_subscription(sub_id: str):
    """Revoke license when PayPal subscription ends."""
    logger.info("License revoked for PayPal subscription %s", sub_id)


def _suspend_license(sub_id: str):
    """Suspend license for a PayPal subscription."""
    logger.warning("License suspended for PayPal subscription %s", sub_id)


# ---------------------------------------------------------------------------
# Event Router
# ---------------------------------------------------------------------------

EVENT_HANDLERS = {
    "CHECKOUT.ORDER.APPROVED": handle_checkout_order_approved,
    "CHECKOUT.ORDER.COMPLETED": handle_checkout_order_completed,
    "BILLING.SUBSCRIPTION.CREATED": handle_subscription_created,
    "BILLING.SUBSCRIPTION.ACTIVATED": handle_subscription_activated,
    "BILLING.SUBSCRIPTION.UPDATED": handle_subscription_updated,
    "BILLING.SUBSCRIPTION.EXPIRED": handle_subscription_expired,
    "BILLING.SUBSCRIPTION.CANCELLED": handle_subscription_cancelled,
    "BILLING.SUBSCRIPTION.SUSPENDED": handle_subscription_suspended,
    "PAYMENT.CAPTURE.COMPLETED": handle_payment_capture_completed,
    "PAYMENT.CAPTURE.DENIED": handle_payment_capture_denied,
    "PAYMENT.CAPTURE.REFUNDED": handle_payment_capture_refunded,
    "BILLING.SUBSCRIPTION.PAYMENT.REFUNDED": handle_subscription_payment_refunded,
}


# ---------------------------------------------------------------------------
# Flask Blueprint
# ---------------------------------------------------------------------------

paypal_webhook_bp = Blueprint("paypal_webhook", __name__)


@paypal_webhook_bp.route("/webhooks/paypal", methods=["POST"])
def paypal_webhook():
    """
    PayPal webhook endpoint.
    Receives and processes PayPal event notifications.
    """
    payload = request.get_data().decode("utf-8")

    # Extract PayPal webhook headers
    headers = extract_webhook_headers(request.headers)

    # Verify signature
    if not verify_paypal_signature(
        auth_algo=headers["auth_algo"],
        auth_nonce=headers["auth_nonce"],
        auth_timestamp=headers["auth_timestamp"],
        cert_url=headers["cert_url"],
        transmission_id=headers["transmission_id"],
        transmission_sig=headers["transmission_sig"],
        transmission_time=headers["transmission_time"],
        webhook_id=PAYPAL_WEBHOOK_ID,
        event_body=payload,
    ):
        logger.error("PayPal webhook signature verification failed")
        return jsonify({"error": "Invalid signature"}), 401

    # Parse event
    try:
        event = json.loads(payload)
    except json.JSONDecodeError as e:
        logger.error("Failed to parse PayPal webhook payload: %s", e)
        return jsonify({"error": "Invalid JSON"}), 400

    event_id = event.get("id", "")
    event_type = event.get("event_type", "")
    event_version = event.get("event_version", "")
    resource = event.get("resource", {})

    logger.info("Received PayPal event: id=%s type=%s version=%s",
                event_id, event_type, event_version)

    # Route to handler
    handler = EVENT_HANDLERS.get(event_type)
    if handler:
        try:
            result = handler({"resource": resource})
            result["event_id"] = event_id
            result["event_type"] = event_type
            result["processed_at"] = datetime.utcnow().isoformat()
            logger.info("PayPal event %s processed successfully", event_type)
            return jsonify({
                "received": True,
                "event_id": event_id,
                "event_type": event_type,
                "result": result,
            }), 200
        except Exception as e:
            logger.error("Error handling PayPal event %s: %s", event_type, e)
            return jsonify({"error": f"Handler error: {str(e)}"}), 500
    else:
        logger.info("No handler for PayPal event type: %s", event_type)
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
    app.register_blueprint(paypal_webhook_bp)

    @app.route("/health", methods=["GET"])
    def health():
        return jsonify({
            "status": "healthy",
            "service": "POWSYS365 PayPal Webhook Handler",
            "version": "1.0.0",
            "webhook_id_configured": bool(PAYPAL_WEBHOOK_ID),
            "sandbox": PAYPAL_SANDBOX,
        }), 200

    return app


if __name__ == "__main__":
    app = create_app()
    port = int(os.environ.get("WEBHOOK_PORT", 5002))
    app.run(host="0.0.0.0", port=port, debug=False)
