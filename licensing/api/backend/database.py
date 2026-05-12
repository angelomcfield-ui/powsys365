"""
POWSYS365 License API Database Module
=======================================
PostgreSQL connection, migration helpers, and seed data.
"""

import logging
import os
from datetime import datetime, timedelta

from flask_sqlalchemy import SQLAlchemy
from sqlalchemy import create_engine, text
from sqlalchemy.exc import OperationalError, ProgrammingError

logger = logging.getLogger("powsys365.database")

db = SQLAlchemy()


def init_db(app):
    """Initialize the database with the Flask app."""
    db.init_app(app)
    with app.app_context():
        try:
            db.create_all()
            logger.info("Database tables created/verified successfully.")
            seed_database()
        except Exception as e:
            logger.error(f"Database initialization error: {e}")
            raise


def get_db_connection():
    """Get a raw database connection for advanced queries."""
    config = os.environ.get("DATABASE_URL", "")
    if not config:
        db_host = os.environ.get("DB_HOST", "localhost")
        db_port = os.environ.get("DB_PORT", "5432")
        db_name = os.environ.get("DB_NAME", "powsys365_licenses")
        db_user = os.environ.get("DB_USER", "powsys365")
        db_pass = os.environ.get("DB_PASSWORD", "powsys365_secure_password")
        config = f"postgresql://{db_user}:{db_pass}@{db_host}:{db_port}/{db_name}"
    engine = create_engine(config)
    return engine.connect()


def check_db_health():
    """Check if the database is accessible."""
    try:
        connection = get_db_connection()
        result = connection.execute(text("SELECT 1"))
        connection.close()
        return True, "Database connection healthy"
    except Exception as e:
        logger.error(f"Database health check failed: {e}")
        return False, str(e)


def run_migration(migration_sql):
    """Execute a raw SQL migration."""
    try:
        connection = get_db_connection()
        connection.execute(text(migration_sql))
        connection.commit()
        connection.close()
        logger.info("Migration executed successfully.")
        return True
    except Exception as e:
        logger.error(f"Migration failed: {e}")
        return False


def seed_database():
    """Seed the database with initial data if tables are empty."""
    from models import License, Device, ActivationLog, FeatureAccess

    try:
        license_count = db.session.query(License).count()
        if license_count > 0:
            logger.info("Database already seeded (%d licenses found).", license_count)
            return

        logger.info("Seeding database with initial data...")

        now = datetime.utcnow()

        # Seed sample licenses
        trial_license = License(
            key="POWSYS-TRIAL-DEMO-2024",
            tier="TRIAL",
            status="trial",
            created_at=now,
            expires_at=now + timedelta(days=30),
            max_devices=1,
            user_id="demo_user",
            features='["power_flow","short_circuit"]',
        )

        basic_license = License(
            key="POWSYS-BASIC-DEMO-2024",
            tier="BASIC",
            status="valid",
            created_at=now,
            expires_at=now + timedelta(days=365),
            max_devices=5,
            user_id="demo_basic_user",
            features='["power_flow","short_circuit","reporting"]',
        )

        pro_license = License(
            key="POWSYS-PRO-DEMO-2024",
            tier="PRO",
            status="valid",
            created_at=now,
            expires_at=now + timedelta(days=365),
            max_devices=20,
            user_id="demo_pro_user",
            features='["power_flow","short_circuit","arc_flash","harmonics","cable_sizing","reporting","api_access"]',
        )

        enterprise_license = License(
            key="POWSYS-ENT-DEMO-2024",
            tier="ENTERPRISE",
            status="valid",
            created_at=now,
            expires_at=now + timedelta(days=365),
            max_devices=100,
            user_id="demo_enterprise_user",
            features='["power_flow","short_circuit","arc_flash","transient","motor_starting","harmonics","relay_coordination","cable_sizing","generator_sizing","transformer_sizing","grounding","reporting","api_access","cloud_sync","multi_user","priority_support"]',
        )

        lifetime_license = License(
            key="POWSYS-LIFE-DEMO-2024",
            tier="LIFE_TIME",
            status="valid",
            created_at=now,
            expires_at=None,
            max_devices=9999,
            user_id="demo_lifetime_user",
            features='["all_modules","power_flow","short_circuit","arc_flash","transient","motor_starting","harmonics","relay_coordination","cable_sizing","generator_sizing","transformer_sizing","grounding","lightning","reporting","api_access","cloud_sync","multi_user","priority_support"]',
        )

        student_license = License(
            key="POWSYS-STD-DEMO-2024",
            tier="STUDENT",
            status="valid",
            created_at=now,
            expires_at=now + timedelta(days=365),
            max_devices=1,
            user_id="demo_student_user",
            features='["power_flow","short_circuit","reporting"]',
        )

        db.session.add_all([
            trial_license, basic_license, pro_license,
            enterprise_license, lifetime_license, student_license,
        ])

        # Seed feature access rules
        feature_rules = [
            FeatureAccess(license_key="*", feature_name="power_flow", tier="TRIAL", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="short_circuit", tier="TRIAL", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="power_flow", tier="BASIC", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="short_circuit", tier="BASIC", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="reporting", tier="BASIC", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="power_flow", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="short_circuit", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="arc_flash", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="harmonics", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="cable_sizing", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="reporting", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="api_access", tier="PRO", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="transient", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="motor_starting", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="relay_coordination", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="generator_sizing", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="transformer_sizing", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="grounding", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="lightning", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="cloud_sync", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="multi_user", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="priority_support", tier="ENTERPRISE", is_enabled=True),
            FeatureAccess(license_key="*", feature_name="all_modules", tier="LIFE_TIME", is_enabled=True),
        ]

        db.session.add_all(feature_rules)
        db.session.commit()
        logger.info("Database seeded successfully with %d licenses and %d feature rules.",
                     6, len(feature_rules))

    except Exception as e:
        db.session.rollback()
        logger.error("Database seeding error: %s", e)
        raise
