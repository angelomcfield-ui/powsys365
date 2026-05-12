-- ============================================================
-- POWSYS365 Licensing Schema
-- PostgreSQL 15+
-- ============================================================

-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ============================================================
-- Table: users
-- Registered users of POWSYS365
-- ============================================================
CREATE TABLE IF NOT EXISTS users (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    email           VARCHAR(255) NOT NULL UNIQUE,
    password_hash   VARCHAR(255) NOT NULL,
    full_name       VARCHAR(255),
    company         VARCHAR(255),
    phone           VARCHAR(50),
    license_key     VARCHAR(19) REFERENCES licenses(license_key) ON DELETE SET NULL,
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    is_verified     BOOLEAN NOT NULL DEFAULT FALSE,
    verification_token VARCHAR(128),
    reset_token     VARCHAR(128),
    reset_token_expires TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_login      TIMESTAMPTZ
);

CREATE INDEX idx_users_email ON users(email);
CREATE INDEX idx_users_license_key ON users(license_key);
CREATE INDEX idx_users_verification_token ON users(verification_token);
CREATE INDEX idx_users_reset_token ON users(reset_token);

-- ============================================================
-- Table: licenses
-- License keys and their properties
-- ============================================================
CREATE TABLE IF NOT EXISTS licenses (
    license_key     VARCHAR(19) PRIMARY KEY,
    issuer          VARCHAR(255) NOT NULL DEFAULT 'XNOX L.L.C',
    tier            VARCHAR(20) NOT NULL
                    CHECK (tier IN ('trial', 'basic', 'pro', 'enterprise', 'lifetime')),
    max_buses       INTEGER NOT NULL DEFAULT 50,
    max_projects    INTEGER NOT NULL DEFAULT 1,
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    issued_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at      TIMESTAMPTZ NOT NULL,
    grace_period_ends TIMESTAMPTZ NOT NULL,
    auto_renew      BOOLEAN NOT NULL DEFAULT FALSE,
    payment_provider VARCHAR(50),
    subscription_id  VARCHAR(255),
    rsa_signature   TEXT,
    raw_payload     TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_licenses_tier ON licenses(tier);
CREATE INDEX idx_licenses_subscription ON licenses(subscription_id);
CREATE INDEX idx_licenses_expires ON licenses(expires_at);

-- ============================================================
-- Table: devices
-- Device fingerprinting for license enforcement
-- ============================================================
CREATE TABLE IF NOT EXISTS devices (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id         UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    license_key     VARCHAR(19) NOT NULL REFERENCES licenses(license_key) ON DELETE CASCADE,
    hwid            VARCHAR(255),
    ip_address      INET,
    geo_location    VARCHAR(100),
    disk_serial     VARCHAR(255),
    mac_address     VARCHAR(17),
    os_version      VARCHAR(255),
    cpu_id          VARCHAR(255),
    motherboard_serial VARCHAR(255),
    hostname        VARCHAR(255),
    bios_version    VARCHAR(255),
    total_ram       BIGINT,
    total_disk      BIGINT,
    num_cores       INTEGER,
    last_seen       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    UNIQUE(hwid, license_key)
);

CREATE INDEX idx_devices_user ON devices(user_id);
CREATE INDEX idx_devices_license ON devices(license_key);
CREATE INDEX idx_devices_hwid ON devices(hwid);
CREATE INDEX idx_devices_last_seen ON devices(last_seen);

-- ============================================================
-- Table: payments
-- Payment transactions and subscriptions
-- ============================================================
CREATE TABLE IF NOT EXISTS payments (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id         UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    license_key     VARCHAR(19) REFERENCES licenses(license_key) ON DELETE SET NULL,
    provider        VARCHAR(20) NOT NULL
                    CHECK (provider IN ('paypal', 'stripe')),
    amount          DECIMAL(10, 2) NOT NULL,
    currency        VARCHAR(3) NOT NULL DEFAULT 'USD',
    status          VARCHAR(20) NOT NULL
                    CHECK (status IN ('pending', 'completed', 'failed', 'refunded', 'cancelled', 'disputed')),
    tier            VARCHAR(20) NOT NULL,
    period          VARCHAR(20) NOT NULL DEFAULT 'monthly',
    transaction_id  VARCHAR(255),
    subscription_id VARCHAR(255),
    receipt_url     TEXT,
    webhook_payload JSONB,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_payments_user ON payments(user_id);
CREATE INDEX idx_payments_license ON payments(license_key);
CREATE INDEX idx_payments_transaction ON payments(transaction_id);
CREATE INDEX idx_payments_subscription ON payments(subscription_id);
CREATE INDEX idx_payments_created ON payments(created_at);

-- ============================================================
-- Table: audit_log
-- Comprehensive audit trail for licensing events
-- ============================================================
CREATE TABLE IF NOT EXISTS audit_log (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    license_key     VARCHAR(19) REFERENCES licenses(license_key) ON DELETE SET NULL,
    user_id         UUID REFERENCES users(id) ON DELETE SET NULL,
    action          VARCHAR(50) NOT NULL,
    details         JSONB,
    ip_address      INET,
    hwid            VARCHAR(255),
    success         BOOLEAN NOT NULL DEFAULT TRUE,
    timestamp       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_audit_license ON audit_log(license_key);
CREATE INDEX idx_audit_user ON audit_log(user_id);
CREATE INDEX idx_audit_action ON audit_log(action);
CREATE INDEX idx_audit_timestamp ON audit_log(timestamp);
CREATE INDEX idx_audit_details ON audit_log USING GIN(details);

-- ============================================================
-- Table: license_activations
-- Track each activation event
-- ============================================================
CREATE TABLE IF NOT EXISTS license_activations (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    license_key     VARCHAR(19) NOT NULL REFERENCES licenses(license_key) ON DELETE CASCADE,
    device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
    activated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    deactivated_at  TIMESTAMPTZ,
    is_active       BOOLEAN NOT NULL DEFAULT TRUE
);

CREATE INDEX idx_activations_license ON license_activations(license_key);
CREATE INDEX idx_activations_device ON license_activations(device_id);

-- ============================================================
-- Row Level Security Policies
-- ============================================================

-- Users can only see their own data
ALTER TABLE users ENABLE ROW LEVEL SECURITY;
CREATE POLICY users_own_data ON users
    FOR ALL
    USING (id = current_setting('app.current_user_id')::UUID);

-- Devices filtered by user
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
CREATE POLICY devices_own ON devices
    FOR ALL
    USING (user_id = current_setting('app.current_user_id')::UUID);

-- Payments filtered by user
ALTER TABLE payments ENABLE ROW LEVEL SECURITY;
CREATE POLICY payments_own ON payments
    FOR ALL
    USING (user_id = current_setting('app.current_user_id')::UUID);

-- Audit log - admin only (users see their own)
ALTER TABLE audit_log ENABLE ROW LEVEL SECURITY;
CREATE POLICY audit_own ON audit_log
    FOR SELECT
    USING (user_id = current_setting('app.current_user_id')::UUID);

-- ============================================================
-- Triggers for updated_at
-- ============================================================
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_licenses_updated_at
    BEFORE UPDATE ON licenses
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_payments_updated_at
    BEFORE UPDATE ON payments
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================
-- Insert Default License: 1A2B-3C4D-5E6F-7G8H
-- Lifetime license by XNOX L.L.C, expires 2099-12-31
-- ============================================================

-- Only insert if not exists
INSERT INTO licenses (
    license_key,
    issuer,
    tier,
    max_buses,
    max_projects,
    is_active,
    issued_at,
    expires_at,
    grace_period_ends,
    auto_renew,
    payment_provider,
    subscription_id,
    rsa_signature,
    raw_payload
) VALUES (
    '1A2B-3C4D-5E6F-7G8H',
    'XNOX L.L.C',
    'lifetime',
    999999,
    999,
    TRUE,
    NOW(),
    '2099-12-31 23:59:59+00',
    '2099-12-31 23:59:59+00',  -- Grace period same as expiry for lifetime
    FALSE,
    NULL,
    NULL,
    NULL,  -- No RSA signature for default dev license
    '1A2B-3C4D-5E6F-7G8H|XNOX L.L.C|lifetime|999999|default-device'
)
ON CONFLICT (license_key) DO NOTHING;

-- ============================================================
-- Views for convenient querying
-- ============================================================

-- Active licenses view
CREATE OR REPLACE VIEW active_licenses AS
SELECT l.*, u.email as owner_email, u.full_name as owner_name
FROM licenses l
LEFT JOIN users u ON l.license_key = u.license_key
WHERE l.is_active = TRUE
  AND (l.expires_at > NOW() OR l.tier = 'lifetime');

-- License usage summary
CREATE OR REPLACE VIEW license_usage_summary AS
SELECT
    l.license_key,
    l.tier,
    l.issuer,
    l.expires_at,
    l.max_buses,
    l.max_projects,
    COUNT(DISTINCT d.id) as active_devices,
    COUNT(DISTINCT la.id) as total_activations,
    l.is_active
FROM licenses l
LEFT JOIN devices d ON l.license_key = d.license_key AND d.is_active = TRUE
LEFT JOIN license_activations la ON l.license_key = la.license_key
GROUP BY l.license_key;

-- Payment summary by user
CREATE OR REPLACE VIEW payment_summary AS
SELECT
    u.id as user_id,
    u.email,
    u.full_name,
    COUNT(p.id) as total_payments,
    SUM(CASE WHEN p.status = 'completed' THEN p.amount ELSE 0 END) as total_spent,
    MAX(p.created_at) as last_payment_date,
    p.currency
FROM users u
LEFT JOIN payments p ON u.id = p.user_id
GROUP BY u.id, u.email, u.full_name, p.currency;

-- ============================================================
-- Comments / Documentation
-- ============================================================

COMMENT ON TABLE users IS 'Registered users of POWSYS365';
COMMENT ON TABLE licenses IS 'License keys and their properties';
COMMENT ON TABLE devices IS 'Device fingerprints for license enforcement';
COMMENT ON TABLE payments IS 'Payment transactions and subscriptions';
COMMENT ON TABLE audit_log IS 'Comprehensive audit trail for licensing events';
COMMENT ON TABLE license_activations IS 'Track each license activation event';

COMMENT ON COLUMN licenses.tier IS 'License tier: trial, basic, pro, enterprise, lifetime';
COMMENT ON COLUMN licenses.max_buses IS 'Maximum number of buses allowed';
COMMENT ON COLUMN licenses.grace_period_ends IS 'End of 30-day grace period after expiration';
COMMENT ON COLUMN devices.hwid IS 'Hardware UUID of the device';
COMMENT ON COLUMN devices.mac_address IS 'Primary MAC address';
