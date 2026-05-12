#!/usr/bin/env python3
"""
POWSYS365 License API Server
==============================
Flask-based REST API for license management.

Endpoints:
    POST /api/v1/auth           - Authenticate license
    POST /api/v1/validate       - Validate license
    POST /api/v1/activate       - Activate on device
    POST /api/v1/deactivate     - Deactivate device
    POST /api/v1/suspend        - Suspend license
    POST /api/v1/reactivate     - Reactivate license
    GET  /api/v1/license/<key>  - License info
    POST /api/v1/device/fingerprint - Receive fingerprint
    GET  /api/v1/device/location/<key> - Device locations
    GET  /api/v1/health         - Health check
    GET  /api/v1/feature/check  - Check feature access
    GET  /api/v1/usage/<key>    - Usage statistics
"""

import hashlib
import hmac
import json
import logging
import logging.handlers
import os
import secrets
import sys
import uuid
from datetime import datetime, timedelta
from functools import wraps

from flask import Flask, request, jsonify
from flask_limiter import Limiter
from flask_limiter.util import get_remote_address

from config import get_config
from database import db, init_db, check_db_health
from models import License, Device, ActivationLog, FeatureAccess, UsageStats


# ============================================================================
# Logging Setup
# ============================================================================

def setup_logging(config):
    """Configure structured logging."""
    os.makedirs(os.path.dirname(config.LOG_FILE), exist_ok=True)
    os.makedirs(os.path.dirname(config.AUDIT_LOG_FILE), exist_ok=True)

    logger = logging.getLogger("powsys365")
    logger.setLevel(getattr(logging, config.LOG_LEVEL, logging.INFO))

    # Avoid duplicate handlers
    if logger.handlers:
        return logger

    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.DEBUG)
    console_fmt = logging.Formatter(
        "%(asctime)s | %(levelname)-8s | %(name)s | %(message)s"
    )
    console_handler.setFormatter(console_fmt)
    logger.addHandler(console_handler)

    # File handler
    file_handler = logging.handlers.RotatingFileHandler(
        config.LOG_FILE, maxBytes=10_000_000, backupCount=5
    )
    file_handler.setLevel(logging.INFO)
    file_fmt = logging.Formatter(
        "%(asctime)s | %(levelname)-8s | %(name)s | %(message)s"
    )
    file_handler.setFormatter(file_fmt)
    logger.addHandler(file_handler)

    # Audit log handler
    audit_handler = logging.handlers.RotatingFileHandler(
        config.AUDIT_LOG_FILE, maxBytes=50_000_000, backupCount=10
    )
    audit_handler.setLevel(logging.INFO)
    audit_fmt = logging.Formatter(
        "%(asctime)s | AUDIT | %(name)s | %(message)s"
    )
    audit_handler.setFormatter(audit_fmt)

    audit_logger = logging.getLogger("powsys365.audit")
    audit_logger.setLevel(logging.INFO)
    audit_logger.addHandler(audit_handler)

    return logger


# ============================================================================
# Flask App Factory
# ============================================================================

def create_app():
    """Create and configure the Flask application."""
    app = Flask(__name__)
    config = get_config()
    app.config.from_object(config)

    # Setup logging
    logger = setup_logging(config)
    logger.info("POWSYS365 License API Server starting...")

    # Initialize rate limiter
    limiter = Limiter(
        key_func=get_remote_address,
        app=app,
        default_limits=[config.RATELIMIT_DEFAULT],
        storage_uri=config.RATELIMIT_STORAGE_URI,
        strategy=config.RATELIMIT_STRATEGY,
        headers_enabled=config.RATELIMIT_HEADERS_ENABLED,
    )

    # Initialize database
    init_db(app)

    # Audit logger
    audit_logger = logging.getLogger("powsys365.audit")

    # =========================================================================
    # Helpers
    # =========================================================================

    def log_audit(action, license_key, details=None, success=True, ip=None):
        """Write an audit log entry."""
        entry = {
            "timestamp": datetime.utcnow().isoformat(),
            "action": action,
            "license_key": license_key,
            "ip": ip or request.remote_addr,
            "details": details or {},
            "success": success,
        }
        audit_logger.info(json.dumps(entry))

    def require_api_key(f):
        """Decorator to require a valid API key."""
        @wraps(f)
        def decorated(*args, **kwargs):
            api_key = request.headers.get(config.API_KEY_HEADER)
            if not api_key:
                log_audit("auth_failed", "unknown", {"reason": "missing_api_key"}, False)
                return jsonify({"error": "API key required"}), 401
            # In production, validate against database or secrets manager
            valid_keys = config.ADMIN_API_KEYS if config.ADMIN_API_KEYS != [''] else []
            if valid_keys and api_key not in valid_keys:
                log_audit("auth_failed", "unknown", {"reason": "invalid_api_key"}, False)
                return jsonify({"error": "Invalid API key"}), 403
            return f(*args, **kwargs)
        return decorated

    def get_license_or_404(key):
        """Fetch license by key or return 404."""
        license_obj = License.query.filter_by(key=key).first()
        if not license_obj:
            return None, (jsonify({"error": "License not found"}), 404)
        return license_obj, None

    def update_usage_stats(license_key, device_count=None):
        """Update usage statistics for a license."""
        stats = UsageStats.query.filter_by(license_key=license_key).first()
        if not stats:
            stats = UsageStats(
                license_key=license_key,
                calls_per_hour=1,
                total_requests_24h=1,
                last_check=datetime.utcnow(),
                devices_active=device_count or 0,
            )
            db.session.add(stats)
        else:
            stats.calls_per_hour += 1
            stats.total_requests_24h += 1
            stats.last_check = datetime.utcnow()
            if device_count is not None:
                stats.devices_active = device_count
        db.session.commit()

    # =========================================================================
    # Routes
    # =========================================================================

    @app.route("/api/v1/health", methods=["GET"])
    @limiter.exempt
    def health_check():
        """Health check endpoint."""
        db_ok, db_msg = check_db_health()
        status_code = 200 if db_ok else 503
        return jsonify({
            "status": "healthy" if db_ok else "unhealthy",
            "database": "ok" if db_ok else db_msg,
            "timestamp": datetime.utcnow().isoformat(),
            "version": "1.0.0",
            "service": "POWSYS365 License API",
        }), status_code

    @app.route("/api/v1/version", methods=["GET"])
    @limiter.exempt
    def get_version():
        """Get API version."""
        return jsonify({
            "version": "1.0.0",
            "name": "POWSYS365 License API",
            "build": "2024.06.15-1",
        }), 200

    @app.route("/api/v1/auth", methods=["POST"])
    @limiter.limit("30 per minute")
    def authenticate_license():
        """Authenticate a license key with device fingerprint."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()
            fingerprint = data.get("fingerprint", {})

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                log_audit("authenticate", license_key, {"reason": "license_not_found"}, False)
                return error

            # Check status
            if license_obj.status == "suspended":
                log_audit("authenticate", license_key, {"reason": "license_suspended"}, False)
                return jsonify({
                    "authenticated": False,
                    "reason": "License is suspended",
                    "suspended_reason": license_obj.suspended_reason,
                }), 403

            if license_obj.is_expired():
                license_obj.status = "expired"
                db.session.commit()
                log_audit("authenticate", license_key, {"reason": "license_expired"}, False)
                return jsonify({
                    "authenticated": False,
                    "reason": "License has expired",
                    "expires_at": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                }), 403

            # Verify device is registered
            hwid = fingerprint.get("hwid", "")
            device = Device.query.filter_by(license_key=license_key, hwid=hwid).first()
            if not device:
                log_audit("authenticate", license_key, {"reason": "device_not_registered", "hwid": hwid}, False)
                return jsonify({
                    "authenticated": False,
                    "reason": "Device not registered. Please activate first.",
                }), 403

            if not device.is_active:
                log_audit("authenticate", license_key, {"reason": "device_inactive", "hwid": hwid}, False)
                return jsonify({
                    "authenticated": False,
                    "reason": "Device is deactivated",
                }), 403

            # Update last seen
            device.last_seen = datetime.utcnow()
            device.ip = request.remote_addr
            db.session.commit()

            active_count = len([d for d in license_obj.devices if d.is_active])
            update_usage_stats(license_key, active_count)

            log_audit("authenticate", license_key, {"hwid": hwid, "status": "success"}, True)
            return jsonify({
                "authenticated": True,
                "license_key": license_key,
                "tier": license_obj.tier,
                "status": license_obj.status,
                "expires_at": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                "features": json.loads(license_obj.features) if license_obj.features else [],
                "device_authorized": True,
            }), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"authenticate_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/validate", methods=["POST"])
    @limiter.limit("60 per minute")
    def validate_license():
        """Validate a license key (without device check)."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                return jsonify({"status": "invalid", "valid": False}), 200

            if license_obj.status == "suspended":
                return jsonify({"status": "suspended", "valid": False}), 200

            if license_obj.is_expired():
                license_obj.status = "expired"
                db.session.commit()
                return jsonify({
                    "status": "expired",
                    "valid": False,
                    "expires_at": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                }), 200

            status_map = {
                "valid": "valid",
                "trial": "trial",
            }
            status = status_map.get(license_obj.status, license_obj.status)

            return jsonify({
                "status": status,
                "valid": True,
                "tier": license_obj.tier,
                "expires_at": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                "max_devices": license_obj.max_devices,
                "active_devices": len([d for d in license_obj.devices if d.is_active]),
            }), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"validate_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/activate", methods=["POST"])
    @limiter.limit("20 per minute")
    def activate_license():
        """Activate a license on a device."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()
            device_data = data.get("device_data", {})
            fingerprint = device_data.get("fingerprint", device_data)

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                log_audit("activate", license_key, {"reason": "license_not_found"}, False)
                return error

            # Check license status
            if license_obj.status == "suspended":
                log_audit("activate", license_key, {"reason": "license_suspended"}, False)
                return jsonify({
                    "success": False,
                    "message": "License is suspended",
                }), 403

            if license_obj.is_expired():
                license_obj.status = "expired"
                db.session.commit()
                log_audit("activate", license_key, {"reason": "license_expired"}, False)
                return jsonify({
                    "success": False,
                    "message": "License has expired",
                }), 403

            # Check device limit
            if not license_obj.can_activate_device():
                log_audit("activate", license_key, {"reason": "device_limit_reached"}, False)
                return jsonify({
                    "success": False,
                    "message": f"Maximum device limit ({license_obj.max_devices}) reached.",
                }), 403

            hwid = fingerprint.get("hwid", "")
            if not hwid:
                hwid = hashlib.sha256(
                    (fingerprint.get("mac_address", "") + fingerprint.get("disk_serial", "")).encode()
                ).hexdigest()

            # Check if device already exists
            existing = Device.query.filter_by(license_key=license_key, hwid=hwid).first()
            if existing:
                existing.is_active = True
                existing.last_seen = datetime.utcnow()
                existing.ip = request.remote_addr
                db.session.commit()

                log_audit("activate", license_key, {"hwid": hwid, "status": "reactivated"}, True)
                return jsonify({
                    "success": True,
                    "message": "Device reactivated successfully",
                    "features": json.loads(license_obj.features) if license_obj.features else [],
                    "expiration": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                    "activation_token": secrets.token_urlsafe(32),
                }), 200

            # Create new device
            geo_location = fingerprint.get("geo_location", "")
            geo_lat, geo_lon = None, None
            if geo_location and "," in geo_location:
                try:
                    parts = geo_location.split(",")
                    geo_lat = float(parts[0].strip())
                    geo_lon = float(parts[1].strip())
                except (ValueError, IndexError):
                    pass

            new_device = Device(
                license_key=license_key,
                hwid=hwid,
                ip=request.remote_addr,
                geo_lat=geo_lat,
                geo_lon=geo_lon,
                disk_serial=fingerprint.get("disk_serial", ""),
                mac=fingerprint.get("mac_address", ""),
                os=fingerprint.get("os_version", ""),
                cpu=fingerprint.get("cpu_id", ""),
                model=fingerprint.get("model", ""),
                last_seen=datetime.utcnow(),
                is_active=True,
                activated_at=datetime.utcnow(),
            )
            db.session.add(new_device)

            # Log activation
            activation_log = ActivationLog(
                license_key=license_key,
                device_id=new_device.id,
                action="activate",
                ip=request.remote_addr,
                geo_location=geo_location,
                details=json.dumps(fingerprint),
                success=True,
            )
            db.session.add(activation_log)
            db.session.commit()

            active_count = len([d for d in license_obj.devices if d.is_active])
            update_usage_stats(license_key, active_count)

            log_audit("activate", license_key, {"hwid": hwid, "status": "success"}, True)
            return jsonify({
                "success": True,
                "message": "Device activated successfully",
                "features": json.loads(license_obj.features) if license_obj.features else [],
                "expiration": license_obj.expires_at.isoformat() if license_obj.expires_at else None,
                "activation_token": secrets.token_urlsafe(32),
            }), 201

        except Exception as e:
            db.session.rollback()
            logging.getLogger("powsys365").error(f"activate_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/deactivate", methods=["POST"])
    @limiter.limit("20 per minute")
    def deactivate_license():
        """Deactivate a device from a license."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()
            device_data = data.get("device_data", {})
            fingerprint = device_data.get("fingerprint", device_data)

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                log_audit("deactivate", license_key, {"reason": "license_not_found"}, False)
                return error

            hwid = fingerprint.get("hwid", "")
            if not hwid:
                return jsonify({"error": "hwid is required in fingerprint"}), 400

            device = Device.query.filter_by(license_key=license_key, hwid=hwid).first()
            if not device:
                return jsonify({"error": "Device not found"}), 404

            device.is_active = False
            device.deactivated_at = datetime.utcnow()

            activation_log = ActivationLog(
                license_key=license_key,
                device_id=device.id,
                action="deactivate",
                ip=request.remote_addr,
                details=json.dumps(fingerprint),
                success=True,
            )
            db.session.add(activation_log)
            db.session.commit()

            active_count = len([d for d in license_obj.devices if d.is_active])
            update_usage_stats(license_key, active_count)

            log_audit("deactivate", license_key, {"hwid": hwid}, True)
            return jsonify({"deactivated": True, "message": "Device deactivated successfully"}), 200

        except Exception as e:
            db.session.rollback()
            logging.getLogger("powsys365").error(f"deactivate_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/suspend", methods=["POST"])
    @require_api_key
    @limiter.limit("10 per minute")
    def suspend_license():
        """Suspend a license (admin only)."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()
            reason = data.get("reason", "Administrative suspension")

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            license_obj.status = "suspended"
            license_obj.suspended_reason = reason
            license_obj.suspended_at = datetime.utcnow()

            # Deactivate all devices
            for device in license_obj.devices:
                device.is_active = False

            activation_log = ActivationLog(
                license_key=license_key,
                action="suspend",
                ip=request.remote_addr,
                details=json.dumps({"reason": reason}),
                success=True,
            )
            db.session.add(activation_log)
            db.session.commit()

            log_audit("suspend", license_key, {"reason": reason}, True)
            return jsonify({"suspended": True, "message": "License suspended successfully"}), 200

        except Exception as e:
            db.session.rollback()
            logging.getLogger("powsys365").error(f"suspend_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/reactivate", methods=["POST"])
    @require_api_key
    @limiter.limit("10 per minute")
    def reactivate_license():
        """Reactivate a suspended license (admin only)."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            if license_obj.is_expired():
                license_obj.status = "expired"
                db.session.commit()
                return jsonify({
                    "reactivated": False,
                    "message": "Cannot reactivate: license has expired",
                }), 400

            license_obj.status = "valid"
            license_obj.suspended_reason = None
            license_obj.suspended_at = None

            activation_log = ActivationLog(
                license_key=license_key,
                action="reactivate",
                ip=request.remote_addr,
                success=True,
            )
            db.session.add(activation_log)
            db.session.commit()

            log_audit("reactivate", license_key, {}, True)
            return jsonify({"reactivated": True, "message": "License reactivated successfully"}), 200

        except Exception as e:
            db.session.rollback()
            logging.getLogger("powsys365").error(f"reactivate_license error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/license/<license_key>", methods=["GET"])
    @require_api_key
    @limiter.limit("30 per minute")
    def get_license_info(license_key):
        """Get full license information."""
        try:
            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            result = license_obj.to_dict()
            result["devices"] = [d.to_dict() for d in license_obj.devices]
            result["is_expired"] = license_obj.is_expired()

            return jsonify(result), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"get_license_info error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/device/fingerprint", methods=["POST"])
    @limiter.limit("30 per minute")
    def receive_fingerprint():
        """Receive and store a device fingerprint."""
        try:
            data = request.get_json(silent=True) or {}
            license_key = data.get("license_key", "").strip()
            fingerprint = data.get("fingerprint", {})

            if not license_key:
                return jsonify({"error": "license_key is required"}), 400

            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            hwid = fingerprint.get("hwid", "")
            if not hwid:
                return jsonify({"error": "hwid is required in fingerprint"}), 400

            # Update or create device record
            device = Device.query.filter_by(license_key=license_key, hwid=hwid).first()
            geo_location = fingerprint.get("geo_location", "")
            geo_lat, geo_lon = None, None
            if geo_location and "," in geo_location:
                try:
                    parts = geo_location.split(",")
                    geo_lat = float(parts[0].strip())
                    geo_lon = float(parts[1].strip())
                except (ValueError, IndexError):
                    pass

            if device:
                device.ip = request.remote_addr
                device.geo_lat = geo_lat
                device.geo_lon = geo_lon
                device.last_seen = datetime.utcnow()
                device.os = fingerprint.get("os_version", device.os)
                device.cpu = fingerprint.get("cpu_id", device.cpu)
                device.model = fingerprint.get("model", device.model)
            else:
                device = Device(
                    license_key=license_key,
                    hwid=hwid,
                    ip=request.remote_addr,
                    geo_lat=geo_lat,
                    geo_lon=geo_lon,
                    disk_serial=fingerprint.get("disk_serial", ""),
                    mac=fingerprint.get("mac_address", ""),
                    os=fingerprint.get("os_version", ""),
                    cpu=fingerprint.get("cpu_id", ""),
                    model=fingerprint.get("model", ""),
                    last_seen=datetime.utcnow(),
                )
                db.session.add(device)

            db.session.commit()
            return jsonify({"received": True, "device_id": device.id}), 200

        except Exception as e:
            db.session.rollback()
            logging.getLogger("powsys365").error(f"receive_fingerprint error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/device/location/<license_key>", methods=["GET"])
    @require_api_key
    @limiter.limit("30 per minute")
    def get_device_locations(license_key):
        """Get device locations for a license (for map display)."""
        try:
            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            locations = []
            for device in license_obj.devices:
                if device.geo_lat and device.geo_lon:
                    locations.append({
                        "device_id": device.id,
                        "hwid": device.hwid[:16] + "...",
                        "lat": device.geo_lat,
                        "lon": device.geo_lon,
                        "ip": device.ip,
                        "os": device.os,
                        "last_seen": device.last_seen.isoformat() if device.last_seen else None,
                        "is_active": device.is_active,
                    })

            return jsonify({
                "license_key": license_key,
                "total_devices": len(license_obj.devices),
                "locations": locations,
            }), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"get_device_locations error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/feature/check", methods=["POST"])
    @limiter.limit("120 per minute")
    def check_feature_access():
        """Check if a feature is accessible for a tier."""
        try:
            data = request.get_json(silent=True) or {}
            feature = data.get("feature", "").strip()
            tier = data.get("tier", "").strip().upper()

            if not feature or not tier:
                return jsonify({"error": "feature and tier are required"}), 400

            has_access = FeatureAccess.check_access("*", feature, tier)

            return jsonify({
                "feature": feature,
                "tier": tier,
                "has_access": has_access,
            }), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"check_feature_access error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/usage/<license_key>", methods=["GET"])
    @require_api_key
    @limiter.limit("30 per minute")
    def get_usage_stats(license_key):
        """Get usage statistics for a license."""
        try:
            license_obj, error = get_license_or_404(license_key)
            if error:
                return error

            stats = UsageStats.query.filter_by(license_key=license_key).first()
            if not stats:
                return jsonify({
                    "license_key": license_key,
                    "calls_per_hour": 0,
                    "last_check": None,
                    "devices_active": len([d for d in license_obj.devices if d.is_active]),
                    "total_requests_24h": 0,
                }), 200

            return jsonify(stats.to_dict()), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"get_usage_stats error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    @app.route("/api/v1/licenses", methods=["GET"])
    @require_api_key
    @limiter.limit("10 per minute")
    def list_licenses():
        """List all licenses (admin only)."""
        try:
            page = request.args.get("page", 1, type=int)
            per_page = request.args.get("per_page", 50, type=int)
            tier_filter = request.args.get("tier", "").upper()
            status_filter = request.args.get("status", "").lower()

            query = License.query
            if tier_filter:
                query = query.filter_by(tier=tier_filter)
            if status_filter:
                query = query.filter_by(status=status_filter)

            pagination = query.order_by(License.created_at.desc()).paginate(
                page=page, per_page=min(per_page, 100), error_out=False
            )

            return jsonify({
                "total": pagination.total,
                "page": page,
                "per_page": per_page,
                "pages": pagination.pages,
                "licenses": [lic.to_dict() for lic in pagination.items],
            }), 200

        except Exception as e:
            logging.getLogger("powsys365").error(f"list_licenses error: {e}")
            return jsonify({"error": "Internal server error"}), 500

    # Error handlers
    @app.errorhandler(429)
    def ratelimit_handler(e):
        return jsonify({
            "error": "Rate limit exceeded",
            "retry_after": e.description,
        }), 429

    @app.errorhandler(500)
    def internal_error(e):
        db.session.rollback()
        return jsonify({"error": "Internal server error"}), 500

    logger.info("POWSYS365 License API Server started successfully.")
    return app


# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == "__main__":
    app = create_app()
    port = int(os.environ.get("PORT", 5000))
    debug = os.environ.get("FLASK_DEBUG", "false").lower() == "true"
    app.run(host="0.0.0.0", port=port, debug=debug)
