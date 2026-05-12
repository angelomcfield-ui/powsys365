"""
POWSYS365 License API Server Configuration
===========================================
Environment-based configuration for Flask backend.
"""

import os
from datetime import timedelta


class Config:
    """Base configuration."""

    # Flask
    SECRET_KEY = os.environ.get("POWSYS_SECRET_KEY", "powsys365-dev-secret-change-in-production")
    JSON_SORT_KEYS = False

    # Database
    DB_HOST = os.environ.get("DB_HOST", "localhost")
    DB_PORT = int(os.environ.get("DB_PORT", "5432"))
    DB_NAME = os.environ.get("DB_NAME", "powsys365_licenses")
    DB_USER = os.environ.get("DB_USER", "powsys365")
    DB_PASSWORD = os.environ.get("DB_PASSWORD", "powsys365_secure_password")
    SQLALCHEMY_DATABASE_URI = os.environ.get(
        "DATABASE_URL",
        f"postgresql://{DB_USER}:{DB_PASSWORD}@{DB_HOST}:{DB_PORT}/{DB_NAME}",
    )
    SQLALCHEMY_TRACK_MODIFICATIONS = False
    SQLALCHEMY_ENGINE_OPTIONS = {
        "pool_size": 10,
        "max_overflow": 20,
        "pool_timeout": 30,
        "pool_recycle": 1800,
    }

    # Rate limiting
    RATELIMIT_STORAGE_URI = os.environ.get("REDIS_URL", "memory://")
    RATELIMIT_STRATEGY = "fixed-window"
    RATELIMIT_DEFAULT = "100 per minute"
    RATELIMIT_HEADERS_ENABLED = True

    # Auth
    API_KEY_HEADER = "X-API-Key"
    ADMIN_API_KEYS = os.environ.get("ADMIN_API_KEYS", "").split(",")

    # Encryption
    ENCRYPTION_KEY = os.environ.get("ENCRYPTION_KEY", "")

    # Logging
    LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")
    LOG_FILE = os.environ.get("LOG_FILE", "/var/log/powsys365/license_api.log")
    AUDIT_LOG_FILE = os.environ.get("AUDIT_LOG_FILE", "/var/log/powsys365/audit.log")

    # License defaults
    DEFAULT_TRIAL_DAYS = 30
    MAX_DEVICES_PER_TIER = {
        "LIFE_TIME": 9999,
        "ENTERPRISE": 100,
        "PRO": 20,
        "BASIC": 5,
        "TRIAL": 1,
        "STUDENT": 1,
    }

    # Stripe
    STRIPE_SECRET_KEY = os.environ.get("STRIPE_SECRET_KEY", "")
    STRIPE_WEBHOOK_SECRET = os.environ.get("STRIPE_WEBHOOK_SECRET", "")

    # PayPal
    PAYPAL_CLIENT_ID = os.environ.get("PAYPAL_CLIENT_ID", "")
    PAYPAL_CLIENT_SECRET = os.environ.get("PAYPAL_CLIENT_SECRET", "")
    PAYPAL_WEBHOOK_ID = os.environ.get("PAYPAL_WEBHOOK_ID", "")
    PAYPAL_SANDBOX = os.environ.get("PAYPAL_SANDBOX", "true").lower() == "true"

    # Geolocation
    GEOIP_DB_PATH = os.environ.get("GEOIP_DB_PATH", "/usr/share/GeoIP/GeoLite2-City.mmdb")


class DevelopmentConfig(Config):
    """Development configuration."""
    DEBUG = True
    LOG_LEVEL = "DEBUG"


class ProductionConfig(Config):
    """Production configuration."""
    DEBUG = False
    LOG_LEVEL = "WARNING"


class TestingConfig(Config):
    """Testing configuration."""
    TESTING = True
    SQLALCHEMY_DATABASE_URI = os.environ.get(
        "TEST_DATABASE_URL", "postgresql://powsys365:powsys365_secure_password@localhost:5432/powsys365_licenses_test"
    )
    RATELIMIT_ENABLED = False


config_by_name = {
    "development": DevelopmentConfig,
    "production": ProductionConfig,
    "testing": TestingConfig,
    "default": DevelopmentConfig,
}


def get_config():
    env = os.environ.get("FLASK_ENV", "development")
    return config_by_name.get(env, DevelopmentConfig)()
