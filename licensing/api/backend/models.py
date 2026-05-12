"""
POWSYS365 License API Models
==============================
SQLAlchemy models for License, Device, ActivationLog, and FeatureAccess.
"""

import json
import logging
from datetime import datetime

from database import db

logger = logging.getLogger("powsys365.models")


class License(db.Model):
    """License entity."""

    __tablename__ = "licenses"

    id = db.Column(db.Integer, primary_key=True)
    key = db.Column(db.String(64), unique=True, nullable=False, index=True)
    tier = db.Column(db.String(32), nullable=False, default="TRIAL")
    status = db.Column(db.String(32), nullable=False, default="trial")
    created_at = db.Column(db.DateTime, default=datetime.utcnow)
    expires_at = db.Column(db.DateTime, nullable=True)
    max_devices = db.Column(db.Integer, default=1)
    user_id = db.Column(db.String(128), nullable=True, index=True)
    features = db.Column(db.Text, default="[]")
    stripe_customer_id = db.Column(db.String(128), nullable=True)
    stripe_subscription_id = db.Column(db.String(128), nullable=True)
    paypal_order_id = db.Column(db.String(128), nullable=True)
    payment_provider = db.Column(db.String(32), nullable=True)
    suspended_reason = db.Column(db.Text, nullable=True)
    suspended_at = db.Column(db.DateTime, nullable=True)

    devices = db.relationship("Device", backref="license", lazy=True,
                               cascade="all, delete-orphan")
    activation_logs = db.relationship("ActivationLog", backref="license", lazy=True,
                                       cascade="all, delete-orphan")

    def to_dict(self):
        return {
            "id": self.id,
            "key": self.key,
            "tier": self.tier,
            "status": self.status,
            "created_at": self.created_at.isoformat() if self.created_at else None,
            "expires_at": self.expires_at.isoformat() if self.expires_at else None,
            "max_devices": self.max_devices,
            "active_devices": len([d for d in self.devices if d.is_active]),
            "user_id": self.user_id,
            "features": json.loads(self.features) if self.features else [],
            "payment_provider": self.payment_provider,
            "suspended_reason": self.suspended_reason,
        }

    def is_expired(self):
        if self.status == "suspended":
            return False
        if self.expires_at is None:
            return False  # LIFE_TIME
        return datetime.utcnow() > self.expires_at

    def can_activate_device(self):
        active_count = len([d for d in self.devices if d.is_active])
        return active_count < self.max_devices

    def __repr__(self):
        return f"<License {self.key} [{self.tier}] ({self.status})>"


class Device(db.Model):
    """Device registered under a license."""

    __tablename__ = "devices"

    id = db.Column(db.Integer, primary_key=True)
    license_key = db.Column(db.String(64), db.ForeignKey("licenses.key"), nullable=False, index=True)
    hwid = db.Column(db.String(128), nullable=False)
    ip = db.Column(db.String(45), nullable=True)
    geo_lat = db.Column(db.Float, nullable=True)
    geo_lon = db.Column(db.Float, nullable=True)
    disk_serial = db.Column(db.String(128), nullable=True)
    mac = db.Column(db.String(32), nullable=True)
    os = db.Column(db.String(256), nullable=True)
    cpu = db.Column(db.String(256), nullable=True)
    model = db.Column(db.String(256), nullable=True)
    last_seen = db.Column(db.DateTime, default=datetime.utcnow)
    is_active = db.Column(db.Boolean, default=True)
    activated_at = db.Column(db.DateTime, default=datetime.utcnow)
    deactivated_at = db.Column(db.DateTime, nullable=True)

    def to_dict(self):
        return {
            "id": self.id,
            "license_key": self.license_key,
            "hwid": self.hwid,
            "ip": self.ip,
            "geo_location": {"lat": self.geo_lat, "lon": self.geo_lon},
            "disk_serial": self.disk_serial,
            "mac": self.mac,
            "os": self.os,
            "cpu": self.cpu,
            "model": self.model,
            "last_seen": self.last_seen.isoformat() if self.last_seen else None,
            "is_active": self.is_active,
            "activated_at": self.activated_at.isoformat() if self.activated_at else None,
        }

    def __repr__(self):
        return f"<Device {self.hwid[:16]}... lic={self.license_key}>"


class ActivationLog(db.Model):
    """Audit log for license activations/deactivations."""

    __tablename__ = "activation_logs"

    id = db.Column(db.Integer, primary_key=True)
    license_key = db.Column(db.String(64), db.ForeignKey("licenses.key"), nullable=False, index=True)
    device_id = db.Column(db.Integer, nullable=True)
    action = db.Column(db.String(32), nullable=False)  # activate, deactivate, suspend, reactivate
    timestamp = db.Column(db.DateTime, default=datetime.utcnow, index=True)
    ip = db.Column(db.String(45), nullable=True)
    geo_location = db.Column(db.String(128), nullable=True)
    details = db.Column(db.Text, nullable=True)
    success = db.Column(db.Boolean, default=True)

    def to_dict(self):
        return {
            "id": self.id,
            "license_key": self.license_key,
            "device_id": self.device_id,
            "action": self.action,
            "timestamp": self.timestamp.isoformat() if self.timestamp else None,
            "ip": self.ip,
            "geo_location": self.geo_location,
            "details": self.details,
            "success": self.success,
        }

    def __repr__(self):
        return f"<ActivationLog {self.action} {self.license_key} at {self.timestamp}>"


class FeatureAccess(db.Model):
    """Feature access rules per tier."""

    __tablename__ = "feature_access"

    id = db.Column(db.Integer, primary_key=True)
    license_key = db.Column(db.String(64), nullable=False, index=True)  # * = all
    feature_name = db.Column(db.String(64), nullable=False)
    tier = db.Column(db.String(32), nullable=False)
    is_enabled = db.Column(db.Boolean, default=True)

    __table_args__ = (db.UniqueConstraint("license_key", "feature_name", "tier"),)

    def to_dict(self):
        return {
            "id": self.id,
            "license_key": self.license_key,
            "feature_name": self.feature_name,
            "tier": self.tier,
            "is_enabled": self.is_enabled,
        }

    @staticmethod
    def check_access(license_key, feature_name, tier):
        """Check if a feature is accessible for a given license."""
        rule = FeatureAccess.query.filter_by(
            license_key="*", feature_name=feature_name, tier=tier
        ).first()
        if rule:
            return rule.is_enabled
        return False

    @staticmethod
    def get_features_for_tier(tier):
        """Get all features available for a tier."""
        rules = FeatureAccess.query.filter_by(license_key="*", tier=tier).all()
        return [r.feature_name for r in rules if r.is_enabled]

    def __repr__(self):
        return f"<FeatureAccess {self.feature_name} [{self.tier}]={self.is_enabled}>"


class UsageStats(db.Model):
    """Aggregated usage statistics per license."""

    __tablename__ = "usage_stats"

    id = db.Column(db.Integer, primary_key=True)
    license_key = db.Column(db.String(64), db.ForeignKey("licenses.key"), nullable=False, index=True)
    calls_per_hour = db.Column(db.Integer, default=0)
    total_requests_24h = db.Column(db.Integer, default=0)
    last_check = db.Column(db.DateTime, default=datetime.utcnow)
    devices_active = db.Column(db.Integer, default=0)

    def to_dict(self):
        return {
            "license_key": self.license_key,
            "calls_per_hour": self.calls_per_hour,
            "total_requests_24h": self.total_requests_24h,
            "last_check": self.last_check.isoformat() if self.last_check else None,
            "devices_active": self.devices_active,
        }

    def __repr__(self):
        return f"<UsageStats {self.license_key} calls={self.calls_per_hour}>"
