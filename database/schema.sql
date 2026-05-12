-- =============================================================================
-- POWSYS365 - Power System Analysis Database Schema
-- PostgreSQL 15+ Compatible
-- Company: XNOX L.L.C
-- Default License: 1A2B-3C4D-5E6F-7G8H
-- Description: Complete database schema for power system electrical analysis
-- =============================================================================

-- =============================================================================
-- SECTION 1: EXTENSIONS
-- =============================================================================

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- =============================================================================
-- SECTION 2: CUSTOM TYPES
-- =============================================================================

-- Bus type enumeration: 1=PQ (load bus), 2=PV (generator bus), 3=Slack (reference bus)
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'bus_type_enum') THEN
        CREATE TYPE bus_type_enum AS ENUM ('PQ', 'PV', 'Slack');
    END IF;
END $$;

-- Generator type enumeration covering all common generation sources
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'gen_type_enum') THEN
        CREATE TYPE gen_type_enum AS ENUM (
            'thermal', 'hydro', 'nuclear', 'wind', 'solar', 
            'diesel', 'gas', 'biomass', 'battery'
        );
    END IF;
END $$;

-- Load model enumeration for different load representation models
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'load_model_enum') THEN
        CREATE TYPE load_model_enum AS ENUM (
            'constant_pq', 'constant_z', 'constant_i', 'zip'
        );
    END IF;
END $$;

-- Shunt type enumeration for reactive compensation devices
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'shunt_type_enum') THEN
        CREATE TYPE shunt_type_enum AS ENUM (
            'fixed', 'switched', 'svc', 'statcom'
        );
    END IF;
END $$;

-- Switch type enumeration for protection and switching devices
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'switch_type_enum') THEN
        CREATE TYPE switch_type_enum AS ENUM (
            'breaker', 'disconnect', 'fuse', 'load_break'
        );
    END IF;
END $$;

-- Fault type enumeration for short circuit analysis
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'fault_type_enum') THEN
        CREATE TYPE fault_type_enum AS ENUM (
            'three_phase', 'single_phase', 'phase_to_phase', 'two_phase_ground'
        );
    END IF;
END $$;

-- =============================================================================
-- SECTION 3: CORE TABLES
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Table: projects
-- Description: Stores power system projects/cases for analysis.
-- Each project represents a complete electrical network configuration.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS projects (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name            VARCHAR(255) NOT NULL,
    description     TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_by      VARCHAR(255),
    base_mva        DECIMAL(10,2) NOT NULL DEFAULT 100.0,
    frequency       DECIMAL(5,2) NOT NULL DEFAULT 60.0,
    standard        VARCHAR(20) NOT NULL DEFAULT 'IEEE' CHECK (standard IN ('IEEE', 'IEC')),
    metadata        JSONB NOT NULL DEFAULT '{}',
    status          VARCHAR(20) NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'archived', 'deleted'))
);

COMMENT ON TABLE projects IS 'Power system analysis projects containing complete network configurations';
COMMENT ON COLUMN projects.id IS 'Unique project identifier (UUID)';
COMMENT ON COLUMN projects.name IS 'Project name or case identifier';
COMMENT ON COLUMN projects.description IS 'Detailed project description';
COMMENT ON COLUMN projects.created_at IS 'Project creation timestamp';
COMMENT ON COLUMN projects.updated_at IS 'Last modification timestamp';
COMMENT ON COLUMN projects.created_by IS 'Username or identifier of creator';
COMMENT ON COLUMN projects.base_mva IS 'System base power in MVA (typically 100)';
COMMENT ON COLUMN projects.frequency IS 'Nominal system frequency in Hz (50 or 60)';
COMMENT ON COLUMN projects.standard IS 'Standard compliance: IEEE or IEC';
COMMENT ON COLUMN projects.metadata IS 'Flexible JSON metadata for custom attributes';
COMMENT ON COLUMN projects.status IS 'Project status: active, archived, or deleted';

-- ---------------------------------------------------------------------------
-- Table: buses
-- Description: Electrical buses (nodes) in the power system network.
-- Each bus represents a point of interconnection in the grid.
-- Bus types: 1=PQ (load bus), 2=PV (voltage-controlled generator bus), 3=Slack (reference bus)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS buses (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    bus_number      INTEGER NOT NULL,
    name            VARCHAR(255) NOT NULL,
    vn_kv           DECIMAL(10,4) NOT NULL,
    bus_type        INTEGER NOT NULL CHECK (bus_type IN (1, 2, 3)),
    area            INTEGER NOT NULL DEFAULT 1,
    zone            INTEGER NOT NULL DEFAULT 1,
    latitude        DECIMAL(12,8),
    longitude       DECIMAL(12,8),
    elevation       DECIMAL(8,2) DEFAULT 0.0,
    vm_pu           DECIMAL(8,6) NOT NULL DEFAULT 1.0,
    va_deg          DECIMAL(10,6) NOT NULL DEFAULT 0.0,
    vmin_pu         DECIMAL(8,6) NOT NULL DEFAULT 0.9,
    vmax_pu         DECIMAL(8,6) NOT NULL DEFAULT 1.1,
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_buses_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT uq_buses_project_number 
        UNIQUE (project_id, bus_number)
);

COMMENT ON TABLE buses IS 'Electrical buses (nodes) in the power system network';
COMMENT ON COLUMN buses.id IS 'Unique bus identifier (UUID)';
COMMENT ON COLUMN buses.project_id IS 'Reference to parent project';
COMMENT ON COLUMN buses.bus_number IS 'Bus number within project (1-based)';
COMMENT ON COLUMN buses.name IS 'Bus name or description';
COMMENT ON COLUMN buses.vn_kv IS 'Nominal voltage in kV';
COMMENT ON COLUMN buses.bus_type IS 'Bus type: 1=PQ, 2=PV, 3=Slack';
COMMENT ON COLUMN buses.area IS 'Control area number';
COMMENT ON COLUMN buses.zone IS 'Zone number for pricing/zoning';
COMMENT ON COLUMN buses.latitude IS 'Geographic latitude (WGS84)';
COMMENT ON COLUMN buses.longitude IS 'Geographic longitude (WGS84)';
COMMENT ON COLUMN buses.elevation IS 'Elevation in meters above sea level';
COMMENT ON COLUMN buses.vm_pu IS 'Voltage magnitude in per unit';
COMMENT ON COLUMN buses.va_deg IS 'Voltage angle in degrees';
COMMENT ON COLUMN buses.vmin_pu IS 'Minimum allowable voltage in per unit';
COMMENT ON COLUMN buses.vmax_pu IS 'Maximum allowable voltage in per unit';
COMMENT ON COLUMN buses.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: lines
-- Description: Transmission lines connecting two buses.
-- Contains pi-model parameters for power flow and short circuit analysis.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS lines (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    line_number     INTEGER NOT NULL,
    name            VARCHAR(255),
    from_bus        UUID NOT NULL,
    to_bus          UUID NOT NULL,
    length_km       DECIMAL(10,4) NOT NULL DEFAULT 1.0,
    r_pu            DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    x_pu            DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    b_pu            DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    g_pu            DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    rate_a_mva      DECIMAL(10,2) NOT NULL DEFAULT 0.0,
    rate_b_mva      DECIMAL(10,2) NOT NULL DEFAULT 0.0,
    rate_c_mva      DECIMAL(10,2) NOT NULL DEFAULT 0.0,
    status          INTEGER NOT NULL DEFAULT 1 CHECK (status IN (0, 1)),
    parallel_lines  INTEGER NOT NULL DEFAULT 1,
    line_model      VARCHAR(50),
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_lines_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_lines_from_bus 
        FOREIGN KEY (from_bus) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT fk_lines_to_bus 
        FOREIGN KEY (to_bus) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_lines_project_number 
        UNIQUE (project_id, line_number),
    CONSTRAINT chk_lines_from_to_different 
        CHECK (from_bus != to_bus)
);

COMMENT ON TABLE lines IS 'Transmission lines connecting two buses in the network';
COMMENT ON COLUMN lines.id IS 'Unique line identifier (UUID)';
COMMENT ON COLUMN lines.project_id IS 'Reference to parent project';
COMMENT ON COLUMN lines.line_number IS 'Line number within project';
COMMENT ON COLUMN lines.name IS 'Line name or description';
COMMENT ON COLUMN lines.from_bus IS 'Origin bus UUID';
COMMENT ON COLUMN lines.to_bus IS 'Destination bus UUID';
COMMENT ON COLUMN lines.length_km IS 'Line length in kilometers';
COMMENT ON COLUMN lines.r_pu IS 'Series resistance in per unit';
COMMENT ON COLUMN lines.x_pu IS 'Series reactance in per unit';
COMMENT ON COLUMN lines.b_pu IS 'Shunt susceptance (charging) in per unit';
COMMENT ON COLUMN lines.g_pu IS 'Shunt conductance in per unit';
COMMENT ON COLUMN lines.rate_a_mva IS 'Thermal rating A (normal) in MVA';
COMMENT ON COLUMN lines.rate_b_mva IS 'Thermal rating B (emergency) in MVA';
COMMENT ON COLUMN lines.rate_c_mva IS 'Thermal rating C (max emergency) in MVA';
COMMENT ON COLUMN lines.status IS 'Line status: 0=open, 1=closed';
COMMENT ON COLUMN lines.parallel_lines IS 'Number of parallel circuits';
COMMENT ON COLUMN lines.line_model IS 'Line model identifier for conductor library';
COMMENT ON COLUMN lines.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: transformers
-- Description: Power transformers with tap-changing capabilities.
-- Supports two-winding transformers with configurable tap ratios.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS transformers (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    trafo_number    INTEGER NOT NULL,
    name            VARCHAR(255),
    hv_bus          UUID NOT NULL,
    lv_bus          UUID NOT NULL,
    tap_nom         DECIMAL(6,4) NOT NULL DEFAULT 1.0,
    tap_min         DECIMAL(6,4) NOT NULL DEFAULT 0.9,
    tap_max         DECIMAL(6,4) NOT NULL DEFAULT 1.1,
    tap_step_pu     DECIMAL(8,6) NOT NULL DEFAULT 0.00625,
    vk_percent      DECIMAL(8,4) NOT NULL DEFAULT 10.0,
    vkr_percent     DECIMAL(8,4) NOT NULL DEFAULT 0.5,
    pfe_kw          DECIMAL(10,2) NOT NULL DEFAULT 0.0,
    i0_percent      DECIMAL(8,4) NOT NULL DEFAULT 0.0,
    sn_mva          DECIMAL(10,2) NOT NULL DEFAULT 100.0,
    shift_deg       DECIMAL(8,4) NOT NULL DEFAULT 0.0,
    tap_side        VARCHAR(10) NOT NULL DEFAULT 'hv' CHECK (tap_side IN ('hv', 'lv')),
    trafo_type      VARCHAR(50) DEFAULT 'two_winding',
    status          INTEGER NOT NULL DEFAULT 1 CHECK (status IN (0, 1)),
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_transformers_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_transformers_hv_bus 
        FOREIGN KEY (hv_bus) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT fk_transformers_lv_bus 
        FOREIGN KEY (lv_bus) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_transformers_project_number 
        UNIQUE (project_id, trafo_number),
    CONSTRAINT chk_transformers_hv_lv_different 
        CHECK (hv_bus != lv_bus)
);

COMMENT ON TABLE transformers IS 'Power transformers connecting different voltage levels';
COMMENT ON COLUMN transformers.id IS 'Unique transformer identifier (UUID)';
COMMENT ON COLUMN transformers.project_id IS 'Reference to parent project';
COMMENT ON COLUMN transformers.trafo_number IS 'Transformer number within project';
COMMENT ON COLUMN transformers.name IS 'Transformer name or description';
COMMENT ON COLUMN transformers.hv_bus IS 'High voltage side bus UUID';
COMMENT ON COLUMN transformers.lv_bus IS 'Low voltage side bus UUID';
COMMENT ON COLUMN transformers.tap_nom IS 'Nominal tap ratio in per unit';
COMMENT ON COLUMN transformers.tap_min IS 'Minimum tap ratio';
COMMENT ON COLUMN transformers.tap_max IS 'Maximum tap ratio';
COMMENT ON COLUMN transformers.tap_step_pu IS 'Tap step size in per unit';
COMMENT ON COLUMN transformers.vk_percent IS 'Short-circuit voltage (impedance) in percent';
COMMENT ON COLUMN transformers.vkr_percent IS 'Resistive component of short-circuit voltage in percent';
COMMENT ON COLUMN transformers.pfe_kw IS 'Iron/core losses in kW';
COMMENT ON COLUMN transformers.i0_percent IS 'No-load current in percent';
COMMENT ON COLUMN transformers.sn_mva IS 'Rated apparent power in MVA';
COMMENT ON COLUMN transformers.shift_deg IS 'Phase shift angle in degrees';
COMMENT ON COLUMN transformers.tap_side IS 'Tap changing side: hv or lv';
COMMENT ON COLUMN transformers.trafo_type IS 'Transformer type classification';
COMMENT ON COLUMN transformers.status IS 'Transformer status: 0=open, 1=closed';
COMMENT ON COLUMN transformers.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: generators
-- Description: Synchronous generators and inverter-based resources.
-- Includes full machine parameters for stability analysis.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS generators (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    gen_number      INTEGER NOT NULL,
    bus_id          UUID NOT NULL,
    name            VARCHAR(255),
    p_mw            DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    q_mvar          DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    q_min_mvar      DECIMAL(10,4) NOT NULL DEFAULT -9999.0,
    q_max_mvar      DECIMAL(10,4) NOT NULL DEFAULT 9999.0,
    vm_pu           DECIMAL(8,6) NOT NULL DEFAULT 1.0,
    sn_mva          DECIMAL(10,2),
    pg_min_mw       DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    pg_max_mw       DECIMAL(10,4) NOT NULL DEFAULT 9999.0,
    gen_type        VARCHAR(20) NOT NULL DEFAULT 'thermal' 
        CHECK (gen_type IN ('thermal', 'hydro', 'nuclear', 'wind', 'solar', 'diesel', 'gas', 'biomass', 'battery')),
    model           VARCHAR(50),
    inertia_h       DECIMAL(8,4),
    damping_d       DECIMAL(8,4) DEFAULT 0.0,
    xd_pu           DECIMAL(10,8),
    xq_pu           DECIMAL(10,8),
    xdp_pu          DECIMAL(10,8),
    xqp_pu          DECIMAL(10,8),
    xdpp_pu         DECIMAL(10,8),
    xqpp_pu         DECIMAL(10,8),
    tdp_sec         DECIMAL(8,4),
    tqp_sec         DECIMAL(8,4),
    tdpp_sec        DECIMAL(8,4),
    tqpp_sec        DECIMAL(8,4),
    status          INTEGER NOT NULL DEFAULT 1 CHECK (status IN (0, 1)),
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_generators_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_generators_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_generators_project_number 
        UNIQUE (project_id, gen_number)
);

COMMENT ON TABLE generators IS 'Synchronous generators and inverter-based generation resources';
COMMENT ON COLUMN generators.id IS 'Unique generator identifier (UUID)';
COMMENT ON COLUMN generators.project_id IS 'Reference to parent project';
COMMENT ON COLUMN generators.gen_number IS 'Generator number within project';
COMMENT ON COLUMN generators.bus_id IS 'Connected bus UUID';
COMMENT ON COLUMN generators.name IS 'Generator name or plant name';
COMMENT ON COLUMN generators.p_mw IS 'Active power output in MW';
COMMENT ON COLUMN generators.q_mvar IS 'Reactive power output in MVAR';
COMMENT ON COLUMN generators.q_min_mvar IS 'Minimum reactive power limit in MVAR';
COMMENT ON COLUMN generators.q_max_mvar IS 'Maximum reactive power limit in MVAR';
COMMENT ON COLUMN generators.vm_pu IS 'Voltage setpoint in per unit';
COMMENT ON COLUMN generators.sn_mva IS 'Rated apparent power in MVA';
COMMENT ON COLUMN generators.pg_min_mw IS 'Minimum active power output in MW';
COMMENT ON COLUMN generators.pg_max_mw IS 'Maximum active power output in MW';
COMMENT ON COLUMN generators.gen_type IS 'Generation technology type';
COMMENT ON COLUMN generators.model IS 'Generator model identifier';
COMMENT ON COLUMN generators.inertia_h IS 'Inertia constant H in seconds';
COMMENT ON COLUMN generators.damping_d IS 'Damping coefficient D';
COMMENT ON COLUMN generators.xd_pu IS 'Direct-axis synchronous reactance';
COMMENT ON COLUMN generators.xq_pu IS 'Quadrature-axis synchronous reactance';
COMMENT ON COLUMN generators.xdp_pu IS 'Direct-axis transient reactance';
COMMENT ON COLUMN generators.xqp_pu IS 'Quadrature-axis transient reactance';
COMMENT ON COLUMN generators.xdpp_pu IS 'Direct-axis subtransient reactance';
COMMENT ON COLUMN generators.xqpp_pu IS 'Quadrature-axis subtransient reactance';
COMMENT ON COLUMN generators.tdp_sec IS 'Direct-axis transient time constant (s)';
COMMENT ON COLUMN generators.tqp_sec IS 'Quadrature-axis transient time constant (s)';
COMMENT ON COLUMN generators.tdpp_sec IS 'Direct-axis subtransient time constant (s)';
COMMENT ON COLUMN generators.tqpp_sec IS 'Quadrature-axis subtransient time constant (s)';
COMMENT ON COLUMN generators.status IS 'Generator status: 0=off, 1=on';
COMMENT ON COLUMN generators.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: loads
-- Description: Electrical loads connected to buses.
-- Supports multiple load models: constant PQ, constant Z, constant I, ZIP.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS loads (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    load_number     INTEGER NOT NULL,
    bus_id          UUID NOT NULL,
    name            VARCHAR(255),
    p_mw            DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    q_mvar          DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    scaling_p       DECIMAL(6,4) NOT NULL DEFAULT 1.0,
    scaling_q       DECIMAL(6,4) NOT NULL DEFAULT 1.0,
    load_model      VARCHAR(20) NOT NULL DEFAULT 'constant_pq'
        CHECK (load_model IN ('constant_pq', 'constant_z', 'constant_i', 'zip')),
    zip_pz          DECIMAL(6,4) DEFAULT 0.0,
    zip_pi          DECIMAL(6,4) DEFAULT 0.0,
    profile_id      UUID,
    status          INTEGER NOT NULL DEFAULT 1 CHECK (status IN (0, 1)),
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_loads_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_loads_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_loads_project_number 
        UNIQUE (project_id, load_number)
);

COMMENT ON TABLE loads IS 'Electrical loads connected to buses in the network';
COMMENT ON COLUMN loads.id IS 'Unique load identifier (UUID)';
COMMENT ON COLUMN loads.project_id IS 'Reference to parent project';
COMMENT ON COLUMN loads.load_number IS 'Load number within project';
COMMENT ON COLUMN loads.bus_id IS 'Connected bus UUID';
COMMENT ON COLUMN loads.name IS 'Load name or description';
COMMENT ON COLUMN loads.p_mw IS 'Active power demand in MW';
COMMENT ON COLUMN loads.q_mvar IS 'Reactive power demand in MVAR';
COMMENT ON COLUMN loads.scaling_p IS 'Active power scaling factor';
COMMENT ON COLUMN loads.scaling_q IS 'Reactive power scaling factor';
COMMENT ON COLUMN loads.load_model IS 'Load model: constant_pq, constant_z, constant_i, zip';
COMMENT ON COLUMN loads.zip_pz IS 'ZIP model constant-Z fraction (P)';
COMMENT ON COLUMN loads.zip_pi IS 'ZIP model constant-I fraction (P)';
COMMENT ON COLUMN loads.profile_id IS 'Reference to load profile (optional)';
COMMENT ON COLUMN loads.status IS 'Load status: 0=off, 1=on';
COMMENT ON COLUMN loads.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: shunts
-- Description: Shunt compensation devices connected to buses.
-- Includes fixed/switched capacitors, reactors, SVC, and STATCOM.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS shunts (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    shunt_number    INTEGER NOT NULL,
    bus_id          UUID NOT NULL,
    name            VARCHAR(255),
    q_mvar          DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    vn_kv           DECIMAL(10,4),
    steps           INTEGER NOT NULL DEFAULT 1,
    q_min_mvar      DECIMAL(10,4),
    q_max_mvar      DECIMAL(10,4),
    shunt_type      VARCHAR(20) NOT NULL DEFAULT 'fixed'
        CHECK (shunt_type IN ('fixed', 'switched', 'svc', 'statcom')),
    status          INTEGER NOT NULL DEFAULT 1 CHECK (status IN (0, 1)),
    properties      JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_shunts_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_shunts_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_shunts_project_number 
        UNIQUE (project_id, shunt_number)
);

COMMENT ON TABLE shunts IS 'Shunt compensation devices: capacitors, reactors, SVC, STATCOM';
COMMENT ON COLUMN shunts.id IS 'Unique shunt identifier (UUID)';
COMMENT ON COLUMN shunts.project_id IS 'Reference to parent project';
COMMENT ON COLUMN shunts.shunt_number IS 'Shunt number within project';
COMMENT ON COLUMN shunts.bus_id IS 'Connected bus UUID';
COMMENT ON COLUMN shunts.name IS 'Shunt device name';
COMMENT ON COLUMN shunts.q_mvar IS 'Reactive power in MVAR (positive=capacitive, negative=inductive)';
COMMENT ON COLUMN shunts.vn_kv IS 'Nominal voltage in kV';
COMMENT ON COLUMN shunts.steps IS 'Number of switchable steps';
COMMENT ON COLUMN shunts.q_min_mvar IS 'Minimum reactive power in MVAR';
COMMENT ON COLUMN shunts.q_max_mvar IS 'Maximum reactive power in MVAR';
COMMENT ON COLUMN shunts.shunt_type IS 'Shunt type: fixed, switched, svc, statcom';
COMMENT ON COLUMN shunts.status IS 'Shunt status: 0=off, 1=on';
COMMENT ON COLUMN shunts.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: switches
-- Description: Switching devices: circuit breakers, disconnectors, fuses.
-- Connects two buses and can open/close the connection.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS switches (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id          UUID NOT NULL,
    switch_number       INTEGER NOT NULL,
    bus_from            UUID NOT NULL,
    bus_to              UUID NOT NULL,
    name                VARCHAR(255),
    switch_type         VARCHAR(20) NOT NULL DEFAULT 'breaker'
        CHECK (switch_type IN ('breaker', 'disconnect', 'fuse', 'load_break')),
    state               INTEGER NOT NULL DEFAULT 1 CHECK (state IN (0, 1)),
    rated_current_a     DECIMAL(10,2),
    rated_voltage_kv    DECIMAL(10,4),
    breaking_capacity_ka DECIMAL(8,2),
    properties          JSONB NOT NULL DEFAULT '{}',
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_switches_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_switches_bus_from 
        FOREIGN KEY (bus_from) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT fk_switches_bus_to 
        FOREIGN KEY (bus_to) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_switches_project_number 
        UNIQUE (project_id, switch_number),
    CONSTRAINT chk_switches_bus_different 
        CHECK (bus_from != bus_to)
);

COMMENT ON TABLE switches IS 'Switching devices: circuit breakers, disconnectors, fuses, load break';
COMMENT ON COLUMN switches.id IS 'Unique switch identifier (UUID)';
COMMENT ON COLUMN switches.project_id IS 'Reference to parent project';
COMMENT ON COLUMN switches.switch_number IS 'Switch number within project';
COMMENT ON COLUMN switches.bus_from IS 'From bus UUID';
COMMENT ON COLUMN switches.bus_to IS 'To bus UUID';
COMMENT ON COLUMN switches.name IS 'Switch name or description';
COMMENT ON COLUMN switches.switch_type IS 'Switch type: breaker, disconnect, fuse, load_break';
COMMENT ON COLUMN switches.state IS 'Switch state: 0=open, 1=closed';
COMMENT ON COLUMN switches.rated_current_a IS 'Rated continuous current in Amperes';
COMMENT ON COLUMN switches.rated_voltage_kv IS 'Rated voltage in kV';
COMMENT ON COLUMN switches.breaking_capacity_ka IS 'Short-circuit breaking capacity in kA';
COMMENT ON COLUMN switches.properties IS 'Flexible JSON properties for custom attributes';

-- ---------------------------------------------------------------------------
-- Table: load_profiles
-- Description: Time-series load profiles for time-varying analysis.
-- Stores multipliers that scale base load values at each timestamp.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS load_profiles (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id      UUID NOT NULL,
    profile_name    VARCHAR(255) NOT NULL,
    bus_id          UUID,
    load_id         UUID,
    timestamp       TIMESTAMPTZ NOT NULL,
    p_multiplier    DECIMAL(8,6) NOT NULL DEFAULT 1.0,
    q_multiplier    DECIMAL(8,6) NOT NULL DEFAULT 1.0,
    
    CONSTRAINT fk_load_profiles_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_load_profiles_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT fk_load_profiles_load 
        FOREIGN KEY (load_id) REFERENCES loads(id) ON DELETE CASCADE,
    CONSTRAINT chk_load_profiles_target 
        CHECK (bus_id IS NOT NULL OR load_id IS NOT NULL)
);

COMMENT ON TABLE load_profiles IS 'Time-series load profiles for time-varying power flow studies';
COMMENT ON COLUMN load_profiles.id IS 'Unique profile entry identifier (UUID)';
COMMENT ON COLUMN load_profiles.project_id IS 'Reference to parent project';
COMMENT ON COLUMN load_profiles.profile_name IS 'Profile name (e.g., daily, seasonal)';
COMMENT ON COLUMN load_profiles.bus_id IS 'Target bus UUID (if bus-level profile)';
COMMENT ON COLUMN load_profiles.load_id IS 'Target load UUID (if load-level profile)';
COMMENT ON COLUMN load_profiles.timestamp IS 'Profile timestamp';
COMMENT ON COLUMN load_profiles.p_multiplier IS 'Active power multiplier';
COMMENT ON COLUMN load_profiles.q_multiplier IS 'Reactive power multiplier';

-- ---------------------------------------------------------------------------
-- Table: results_power_flow
-- Description: Per-bus results from power flow (load flow) calculations.
-- Stores converged voltage magnitudes, angles, and power injections.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS results_power_flow (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id          UUID NOT NULL,
    run_id              UUID NOT NULL,
    bus_id              UUID NOT NULL,
    vm_pu               DECIMAL(8,6) NOT NULL,
    va_deg              DECIMAL(10,6) NOT NULL,
    p_injected_mw       DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    q_injected_mvar     DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    p_gen_mw            DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    q_gen_mvar          DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    p_load_mw           DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    q_load_mvar         DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    violations          JSONB NOT NULL DEFAULT '[]',
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_results_pf_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_results_pf_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_results_pf_run_bus 
        UNIQUE (project_id, run_id, bus_id)
);

COMMENT ON TABLE results_power_flow IS 'Power flow (load flow) results per bus';
COMMENT ON COLUMN results_power_flow.id IS 'Unique result identifier (UUID)';
COMMENT ON COLUMN results_power_flow.project_id IS 'Reference to parent project';
COMMENT ON COLUMN results_power_flow.run_id IS 'Analysis run identifier (groups results from same execution)';
COMMENT ON COLUMN results_power_flow.bus_id IS 'Bus UUID for these results';
COMMENT ON COLUMN results_power_flow.vm_pu IS 'Voltage magnitude in per unit (result)';
COMMENT ON COLUMN results_power_flow.va_deg IS 'Voltage angle in degrees (result)';
COMMENT ON COLUMN results_power_flow.p_injected_mw IS 'Net active power injected at bus in MW';
COMMENT ON COLUMN results_power_flow.q_injected_mvar IS 'Net reactive power injected at bus in MVAR';
COMMENT ON COLUMN results_power_flow.p_gen_mw IS 'Total active generation at bus in MW';
COMMENT ON COLUMN results_power_flow.q_gen_mvar IS 'Total reactive generation at bus in MVAR';
COMMENT ON COLUMN results_power_flow.p_load_mw IS 'Total active load at bus in MW';
COMMENT ON COLUMN results_power_flow.q_load_mvar IS 'Total reactive load at bus in MVAR';
COMMENT ON COLUMN results_power_flow.violations IS 'JSON array of constraint violations at this bus';

-- ---------------------------------------------------------------------------
-- Table: results_power_flow_lines
-- Description: Per-line power flow results including loading and losses.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS results_power_flow_lines (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id          UUID NOT NULL,
    run_id              UUID NOT NULL,
    line_id             UUID NOT NULL,
    p_from_mw           DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    q_from_mvar         DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    p_to_mw             DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    q_to_mvar           DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    s_mva               DECIMAL(12,6) NOT NULL DEFAULT 0.0,
    loading_percent     DECIMAL(8,4) NOT NULL DEFAULT 0.0,
    p_loss_mw           DECIMAL(12,8) NOT NULL DEFAULT 0.0,
    q_loss_mvar         DECIMAL(12,8) NOT NULL DEFAULT 0.0,
    current_ka          DECIMAL(10,6) NOT NULL DEFAULT 0.0,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_results_pfl_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_results_pfl_line 
        FOREIGN KEY (line_id) REFERENCES lines(id) ON DELETE CASCADE,
    CONSTRAINT uq_results_pfl_run_line 
        UNIQUE (project_id, run_id, line_id)
);

COMMENT ON TABLE results_power_flow_lines IS 'Power flow results per transmission line';
COMMENT ON COLUMN results_power_flow_lines.id IS 'Unique result identifier (UUID)';
COMMENT ON COLUMN results_power_flow_lines.project_id IS 'Reference to parent project';
COMMENT ON COLUMN results_power_flow_lines.run_id IS 'Analysis run identifier';
COMMENT ON COLUMN results_power_flow_lines.line_id IS 'Line UUID for these results';
COMMENT ON COLUMN results_power_flow_lines.p_from_mw IS 'Active power at from-end in MW';
COMMENT ON COLUMN results_power_flow_lines.q_from_mvar IS 'Reactive power at from-end in MVAR';
COMMENT ON COLUMN results_power_flow_lines.p_to_mw IS 'Active power at to-end in MW';
COMMENT ON COLUMN results_power_flow_lines.q_to_mvar IS 'Reactive power at to-end in MVAR';
COMMENT ON COLUMN results_power_flow_lines.s_mva IS 'Apparent power flow in MVA';
COMMENT ON COLUMN results_power_flow_lines.loading_percent IS 'Line loading percentage relative to rate_a';
COMMENT ON COLUMN results_power_flow_lines.p_loss_mw IS 'Active power losses in MW';
COMMENT ON COLUMN results_power_flow_lines.q_loss_mvar IS 'Reactive power losses in MVAR';
COMMENT ON COLUMN results_power_flow_lines.current_ka IS 'Current magnitude in kA';

-- ---------------------------------------------------------------------------
-- Table: results_short_circuit
-- Description: Short circuit (fault) analysis results per bus.
-- Contains IEC 60909 / IEEE C37 standards compliant results.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS results_short_circuit (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id          UUID NOT NULL,
    run_id              UUID NOT NULL,
    bus_id              UUID NOT NULL,
    fault_type          VARCHAR(20) NOT NULL
        CHECK (fault_type IN ('three_phase', 'single_phase', 'phase_to_phase', 'two_phase_ground')),
    ik_ka               DECIMAL(10,6) NOT NULL DEFAULT 0.0,
    ip_ka               DECIMAL(10,6) NOT NULL DEFAULT 0.0,
    ib_ka               DECIMAL(10,6) NOT NULL DEFAULT 0.0,
    sk_mva              DECIMAL(10,4) NOT NULL DEFAULT 0.0,
    rx_ratio            DECIMAL(8,6) NOT NULL DEFAULT 0.0,
    kappa               DECIMAL(8,6) NOT NULL DEFAULT 1.0,
    z1_pu               DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    z0_pu               DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    z2_pu               DECIMAL(12,10) NOT NULL DEFAULT 0.0,
    contributions       JSONB NOT NULL DEFAULT '{}',
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    CONSTRAINT fk_results_sc_project 
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
    CONSTRAINT fk_results_sc_bus 
        FOREIGN KEY (bus_id) REFERENCES buses(id) ON DELETE CASCADE,
    CONSTRAINT uq_results_sc_run_bus_fault 
        UNIQUE (project_id, run_id, bus_id, fault_type)
);

COMMENT ON TABLE results_short_circuit IS 'Short circuit (fault) analysis results per bus';
COMMENT ON COLUMN results_short_circuit.id IS 'Unique result identifier (UUID)';
COMMENT ON COLUMN results_short_circuit.project_id IS 'Reference to parent project';
COMMENT ON COLUMN results_short_circuit.run_id IS 'Analysis run identifier';
COMMENT ON COLUMN results_short_circuit.bus_id IS 'Faulted bus UUID';
COMMENT ON COLUMN results_short_circuit.fault_type IS 'Fault type: three_phase, single_phase, phase_to_phase, two_phase_ground';
COMMENT ON COLUMN results_short_circuit.ik_ka IS 'Steady-state short circuit current Ik in kA';
COMMENT ON COLUMN results_short_circuit.ip_ka IS 'Peak short circuit current Ip in kA';
COMMENT ON COLUMN results_short_circuit.ib_ka IS 'Breaking current Ib in kA';
COMMENT ON COLUMN results_short_circuit.sk_mva IS 'Short circuit power Sk in MVA';
COMMENT ON COLUMN results_short_circuit.rx_ratio IS 'R/X ratio at fault location';
COMMENT ON COLUMN results_short_circuit.kappa IS 'Peak factor kappa';
COMMENT ON COLUMN results_short_circuit.z1_pu IS 'Positive sequence impedance in per unit';
COMMENT ON COLUMN results_short_circuit.z0_pu IS 'Zero sequence impedance in per unit';
COMMENT ON COLUMN results_short_circuit.z2_pu IS 'Negative sequence impedance in per unit';
COMMENT ON COLUMN results_short_circuit.contributions IS 'JSON object with generator/line contributions to fault current';

-- ---------------------------------------------------------------------------
-- Table: system_config
-- Description: System-wide configuration key-value store.
-- Used for application settings, license info, and global parameters.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS system_config (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    config_key      VARCHAR(255) NOT NULL UNIQUE,
    config_value    JSONB NOT NULL DEFAULT '{}',
    description     TEXT,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE system_config IS 'System-wide configuration key-value store';
COMMENT ON COLUMN system_config.id IS 'Unique config entry identifier (UUID)';
COMMENT ON COLUMN system_config.config_key IS 'Configuration key (unique identifier)';
COMMENT ON COLUMN system_config.config_value IS 'Configuration value as JSON';
COMMENT ON COLUMN system_config.description IS 'Human-readable description of the setting';
COMMENT ON COLUMN system_config.updated_at IS 'Last modification timestamp';

-- =============================================================================
-- SECTION 4: INDICES
-- =============================================================================

-- Project indices
CREATE INDEX IF NOT EXISTS idx_projects_status ON projects(status);
CREATE INDEX IF NOT EXISTS idx_projects_created_at ON projects(created_at);

-- Bus indices
CREATE INDEX IF NOT EXISTS idx_buses_project ON buses(project_id);
CREATE INDEX IF NOT EXISTS idx_buses_project_number ON buses(project_id, bus_number);
CREATE INDEX IF NOT EXISTS idx_buses_type ON buses(bus_type);
CREATE INDEX IF NOT EXISTS idx_buses_area ON buses(area);

-- Line indices
CREATE INDEX IF NOT EXISTS idx_lines_project ON lines(project_id);
CREATE INDEX IF NOT EXISTS idx_lines_from_bus ON lines(from_bus);
CREATE INDEX IF NOT EXISTS idx_lines_to_bus ON lines(to_bus);
CREATE INDEX IF NOT EXISTS idx_lines_from_to ON lines(from_bus, to_bus);
CREATE INDEX IF NOT EXISTS idx_lines_status ON lines(status);

-- Transformer indices
CREATE INDEX IF NOT EXISTS idx_transformers_project ON transformers(project_id);
CREATE INDEX IF NOT EXISTS idx_transformers_hv_bus ON transformers(hv_bus);
CREATE INDEX IF NOT EXISTS idx_transformers_lv_bus ON transformers(lv_bus);

-- Generator indices
CREATE INDEX IF NOT EXISTS idx_generators_project ON generators(project_id);
CREATE INDEX IF NOT EXISTS idx_generators_bus ON generators(bus_id);
CREATE INDEX IF NOT EXISTS idx_generators_type ON generators(gen_type);
CREATE INDEX IF NOT EXISTS idx_generators_bus_type ON generators(bus_id, gen_type);

-- Load indices
CREATE INDEX IF NOT EXISTS idx_loads_project ON loads(project_id);
CREATE INDEX IF NOT EXISTS idx_loads_bus ON loads(bus_id);
CREATE INDEX IF NOT EXISTS idx_loads_profile ON loads(profile_id);

-- Shunt indices
CREATE INDEX IF NOT EXISTS idx_shunts_project ON shunts(project_id);
CREATE INDEX IF NOT EXISTS idx_shunts_bus ON shunts(bus_id);

-- Switch indices
CREATE INDEX IF NOT EXISTS idx_switches_project ON switches(project_id);
CREATE INDEX IF NOT EXISTS idx_switches_from ON switches(bus_from);
CREATE INDEX IF NOT EXISTS idx_switches_to ON switches(bus_to);

-- Load profile indices
CREATE INDEX IF NOT EXISTS idx_load_profiles_project ON load_profiles(project_id);
CREATE INDEX IF NOT EXISTS idx_load_profiles_bus ON load_profiles(bus_id);
CREATE INDEX IF NOT EXISTS idx_load_profiles_load ON load_profiles(load_id);
CREATE INDEX IF NOT EXISTS idx_load_profiles_timestamp ON load_profiles(timestamp);

-- Results indices
CREATE INDEX IF NOT EXISTS idx_results_pf_project ON results_power_flow(project_id);
CREATE INDEX IF NOT EXISTS idx_results_pf_run ON results_power_flow(run_id);
CREATE INDEX IF NOT EXISTS idx_results_pf_bus ON results_power_flow(bus_id);
CREATE INDEX IF NOT EXISTS idx_results_pf_project_run ON results_power_flow(project_id, run_id);

CREATE INDEX IF NOT EXISTS idx_results_pfl_project ON results_power_flow_lines(project_id);
CREATE INDEX IF NOT EXISTS idx_results_pfl_run ON results_power_flow_lines(run_id);
CREATE INDEX IF NOT EXISTS idx_results_pfl_line ON results_power_flow_lines(line_id);

CREATE INDEX IF NOT EXISTS idx_results_sc_project ON results_short_circuit(project_id);
CREATE INDEX IF NOT EXISTS idx_results_sc_run ON results_short_circuit(run_id);
CREATE INDEX IF NOT EXISTS idx_results_sc_bus ON results_short_circuit(bus_id);
CREATE INDEX IF NOT EXISTS idx_results_sc_project_run ON results_short_circuit(project_id, run_id);

-- =============================================================================
-- SECTION 5: TRIGGERS (Auto-updated updated_at)
-- =============================================================================

-- Trigger function to automatically update the updated_at timestamp
CREATE OR REPLACE FUNCTION trigger_set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION trigger_set_updated_at() IS 'Automatically sets updated_at to current timestamp on row update';

-- Apply updated_at trigger to all tables that have the column
DO $$
DECLARE
    tbl TEXT;
    tables TEXT[] := ARRAY[
        'projects', 'buses', 'lines', 'transformers', 
        'generators', 'loads', 'shunts', 'switches',
        'system_config'
    ];
BEGIN
    FOREACH tbl IN ARRAY tables
    LOOP
        EXECUTE format(
            'DROP TRIGGER IF EXISTS trg_%I_updated_at ON %I; 
             CREATE TRIGGER trg_%I_updated_at 
             BEFORE UPDATE ON %I 
             FOR EACH ROW 
             EXECUTE FUNCTION trigger_set_updated_at();',
            tbl, tbl, tbl, tbl
        );
    END LOOP;
END $$;

-- =============================================================================
-- SECTION 6: VIEWS
-- =============================================================================

-- ---------------------------------------------------------------------------
-- View: v_system_summary
-- Description: Aggregated system summary showing generation, load, and losses
-- per project. Uses CORRECT column: SUM(g.q_mvar) for reactive generation.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_system_summary AS
SELECT 
    p.id AS project_id,
    p.name AS project_name,
    p.base_mva,
    p.frequency,
    p.standard,
    p.status AS project_status,
    COUNT(DISTINCT b.id) AS total_buses,
    COUNT(DISTINCT l.id) AS total_lines,
    COUNT(DISTINCT t.id) AS total_transformers,
    COUNT(DISTINCT g.id) AS total_generators,
    COUNT(DISTINCT ld.id) AS total_loads,
    COUNT(DISTINCT s.id) AS total_shunts,
    COUNT(DISTINCT sw.id) AS total_switches,
    COALESCE(SUM(g.p_mw), 0) AS total_gen_mw,
    COALESCE(SUM(g.q_mvar), 0) AS total_gen_mvar,
    COALESCE(SUM(ld.p_mw), 0) AS total_load_mw,
    COALESCE(SUM(ld.q_mvar), 0) AS total_load_mvar,
    COALESCE(SUM(g.p_mw), 0) - COALESCE(SUM(ld.p_mw), 0) AS gen_load_balance_mw,
    COALESCE(SUM(CASE WHEN b.bus_type = 3 THEN 1 ELSE 0 END), 0) AS slack_buses,
    COALESCE(SUM(CASE WHEN b.bus_type = 2 THEN 1 ELSE 0 END), 0) AS pv_buses,
    COALESCE(SUM(CASE WHEN b.bus_type = 1 THEN 1 ELSE 0 END), 0) AS pq_buses,
    p.created_at AS project_created,
    p.updated_at AS project_updated
FROM projects p
LEFT JOIN buses b ON b.project_id = p.id
LEFT JOIN lines l ON l.project_id = p.id
LEFT JOIN transformers t ON t.project_id = p.id
LEFT JOIN generators g ON g.project_id = p.id
LEFT JOIN loads ld ON ld.project_id = p.id
LEFT JOIN shunts s ON s.project_id = p.id
LEFT JOIN switches sw ON sw.project_id = p.id
GROUP BY p.id, p.name, p.base_mva, p.frequency, p.standard, p.status, p.created_at, p.updated_at;

COMMENT ON VIEW v_system_summary IS 'Aggregated system summary with generation, load counts, and power balance per project';

-- ---------------------------------------------------------------------------
-- View: v_buses_detail
-- Description: Detailed bus view with connected equipment and power balance.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_buses_detail AS
SELECT 
    b.id AS bus_id,
    b.project_id,
    p.name AS project_name,
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    CASE b.bus_type 
        WHEN 1 THEN 'PQ'
        WHEN 2 THEN 'PV'
        WHEN 3 THEN 'Slack'
    END AS bus_type_label,
    b.bus_type,
    b.vm_pu,
    b.va_deg,
    b.vmin_pu,
    b.vmax_pu,
    b.area,
    b.zone,
    b.latitude,
    b.longitude,
    COALESCE(g.p_mw, 0) AS gen_p_mw,
    COALESCE(g.q_mvar, 0) AS gen_q_mvar,
    COALESCE(g.vm_pu, b.vm_pu) AS gen_vm_setpoint,
    COALESCE(ld.p_mw, 0) AS load_p_mw,
    COALESCE(ld.q_mvar, 0) AS load_q_mvar,
    COALESCE(ld.scaling_p, 1.0) AS load_scaling_p,
    COALESCE(ld.scaling_q, 1.0) AS load_scaling_q,
    COALESCE(s.q_mvar, 0) AS shunt_q_mvar,
    COALESCE(g.p_mw, 0) - COALESCE(ld.p_mw, 0) AS net_p_mw,
    COALESCE(g.q_mvar, 0) - COALESCE(ld.q_mvar, 0) + COALESCE(s.q_mvar, 0) AS net_q_mvar,
    CASE 
        WHEN b.vm_pu < b.vmin_pu THEN 'UNDERVOLTAGE'
        WHEN b.vm_pu > b.vmax_pu THEN 'OVERVOLTAGE'
        ELSE 'OK'
    END AS voltage_status,
    b.created_at,
    b.updated_at
FROM buses b
JOIN projects p ON p.id = b.project_id
LEFT JOIN generators g ON g.bus_id = b.id AND g.status = 1
LEFT JOIN loads ld ON ld.bus_id = b.id AND ld.status = 1
LEFT JOIN shunts s ON s.bus_id = b.id AND s.status = 1;

COMMENT ON VIEW v_buses_detail IS 'Detailed bus information with connected generation, load, shunt, and voltage status';

-- ---------------------------------------------------------------------------
-- View: v_voltage_violations
-- Description: Shows buses with voltage outside allowable limits.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_voltage_violations AS
SELECT 
    b.id AS bus_id,
    b.project_id,
    p.name AS project_name,
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    CASE b.bus_type 
        WHEN 1 THEN 'PQ'
        WHEN 2 THEN 'PV'
        WHEN 3 THEN 'Slack'
    END AS bus_type_label,
    b.vm_pu,
    b.vmin_pu,
    b.vmax_pu,
    b.va_deg,
    CASE 
        WHEN b.vm_pu < b.vmin_pu THEN 'UNDERVOLTAGE'
        WHEN b.vm_pu > b.vmax_pu THEN 'OVERVOLTAGE'
    END AS violation_type,
    CASE 
        WHEN b.vm_pu < b.vmin_pu THEN ROUND(((b.vmin_pu - b.vm_pu) / b.vmin_pu * 100)::NUMERIC, 4)
        WHEN b.vm_pu > b.vmax_pu THEN ROUND(((b.vm_pu - b.vmax_pu) / b.vmax_pu * 100)::NUMERIC, 4)
        ELSE 0.0
    END AS violation_percent,
    ABS(b.vm_pu - b.vmin_pu) AS violation_magnitude_low,
    ABS(b.vm_pu - b.vmax_pu) AS violation_magnitude_high,
    b.created_at
FROM buses b
JOIN projects p ON p.id = b.project_id
WHERE b.vm_pu < b.vmin_pu OR b.vm_pu > b.vmax_pu;

COMMENT ON VIEW v_voltage_violations IS 'Buses with voltage magnitude outside allowable vmin/vmax limits';

-- ---------------------------------------------------------------------------
-- View: v_line_overloads
-- Description: Shows lines operating above their rated capacity.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_line_overloads AS
SELECT 
    l.id AS line_id,
    l.project_id,
    p.name AS project_name,
    l.line_number,
    l.name AS line_name,
    fb.bus_number AS from_bus_number,
    fb.name AS from_bus_name,
    tb.bus_number AS to_bus_number,
    tb.name AS to_bus_name,
    l.length_km,
    l.r_pu,
    l.x_pu,
    l.rate_a_mva,
    l.rate_b_mva,
    l.rate_c_mva,
    l.status AS line_status,
    -- Calculate apparent power from parameters (for pre-analysis checking)
    -- Also include actual results if available
    COALESCE(rpfl.s_mva, 
        SQRT(
            POWER((l.r_pu / (l.r_pu*l.r_pu + l.x_pu*l.x_pu)) * l.rate_a_mva, 2) + 
            POWER((l.x_pu / (l.r_pu*l.r_pu + l.x_pu*l.x_pu)) * l.rate_a_mva, 2)
        ), 0
    ) AS calculated_s_mva,
    COALESCE(rpfl.loading_percent, 0) AS loading_percent,
    COALESCE(rpfl.p_loss_mw, 0) AS p_loss_mw,
    COALESCE(rpfl.q_loss_mvar, 0) AS q_loss_mvar,
    COALESCE(rpfl.current_ka, 0) AS current_ka,
    CASE 
        WHEN COALESCE(rpfl.loading_percent, 0) > 100 THEN 'OVERLOAD'
        WHEN COALESCE(rpfl.loading_percent, 0) > 80 THEN 'WARNING'
        ELSE 'OK'
    END AS overload_status,
    l.created_at
FROM lines l
JOIN projects p ON p.id = l.project_id
JOIN buses fb ON fb.id = l.from_bus
JOIN buses tb ON tb.id = l.to_bus
LEFT JOIN results_power_flow_lines rpfl ON rpfl.line_id = l.id
WHERE l.status = 1
  AND (
      COALESCE(rpfl.loading_percent, 0) > 80  -- Include lines approaching limits
      OR rpfl.loading_percent IS NULL          -- Include lines with no results yet
  );

COMMENT ON VIEW v_line_overloads IS 'Lines with loading above 80% of rated capacity (warning) or 100% (overload)';

-- =============================================================================
-- SECTION 7: FUNCTIONS
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Function: get_total_losses()
-- Description: Calculates total active and reactive power losses for a given
-- project and run_id from power flow line results.
-- Parameters:
--   p_project_id UUID - Project identifier
--   p_run_id UUID - Analysis run identifier
-- Returns: TABLE with total_p_loss_mw and total_q_loss_mvar
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION get_total_losses(
    p_project_id UUID,
    p_run_id UUID
)
RETURNS TABLE (
    total_p_loss_mw DECIMAL(14,6),
    total_q_loss_mvar DECIMAL(14,6),
    total_lines_considered INTEGER,
    max_loading_percent DECIMAL(8,4),
    avg_loading_percent DECIMAL(8,4)
) AS $$
BEGIN
    RETURN QUERY
    SELECT 
        COALESCE(SUM(rpfl.p_loss_mw), 0)::DECIMAL(14,6) AS total_p_loss_mw,
        COALESCE(SUM(rpfl.q_loss_mvar), 0)::DECIMAL(14,6) AS total_q_loss_mvar,
        COUNT(*)::INTEGER AS total_lines_considered,
        COALESCE(MAX(rpfl.loading_percent), 0)::DECIMAL(8,4) AS max_loading_percent,
        COALESCE(AVG(rpfl.loading_percent), 0)::DECIMAL(8,4) AS avg_loading_percent
    FROM results_power_flow_lines rpfl
    JOIN lines l ON l.id = rpfl.line_id
    WHERE rpfl.project_id = p_project_id
      AND rpfl.run_id = p_run_id
      AND l.status = 1;
END;
$$ LANGUAGE plpgsql STABLE;

COMMENT ON FUNCTION get_total_losses(UUID, UUID) IS 
'Calculates total P and Q losses, line count, max/average loading for a project run';

-- ---------------------------------------------------------------------------
-- Function: create_project()
-- Description: Creates a new project with default configuration and returns
-- the newly created project record.
-- Parameters:
--   p_name VARCHAR(255) - Project name
--   p_description TEXT - Project description (optional)
--   p_base_mva DECIMAL(10,2) - Base MVA (default 100)
--   p_frequency DECIMAL(5,2) - Frequency in Hz (default 60)
--   p_standard VARCHAR(20) - Standard: IEEE or IEC (default IEEE)
--   p_created_by VARCHAR(255) - Creator identifier (optional)
-- Returns: The newly created project row
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION create_project(
    p_name VARCHAR(255),
    p_description TEXT DEFAULT NULL,
    p_base_mva DECIMAL(10,2) DEFAULT 100.0,
    p_frequency DECIMAL(5,2) DEFAULT 60.0,
    p_standard VARCHAR(20) DEFAULT 'IEEE',
    p_created_by VARCHAR(255) DEFAULT NULL
)
RETURNS projects AS $$
DECLARE
    v_project projects%ROWTYPE;
BEGIN
    -- Validate standard
    IF p_standard NOT IN ('IEEE', 'IEC') THEN
        RAISE EXCEPTION 'Invalid standard: %. Must be IEEE or IEC', p_standard;
    END IF;
    
    -- Validate base_mva
    IF p_base_mva <= 0 THEN
        RAISE EXCEPTION 'Base MVA must be positive';
    END IF;
    
    -- Validate frequency
    IF p_frequency NOT IN (50.0, 60.0) THEN
        RAISE WARNING 'Unusual frequency: %. Standard values are 50 or 60 Hz', p_frequency;
    END IF;

    INSERT INTO projects (
        name, description, base_mva, frequency, standard, 
        created_by, metadata, status
    ) VALUES (
        p_name, 
        COALESCE(p_description, 'Power system analysis project: ' || p_name),
        p_base_mva,
        p_frequency,
        p_standard,
        p_created_by,
        jsonb_build_object(
            'created_via', 'create_project() function',
            'base_mva', p_base_mva,
            'frequency', p_frequency,
            'standard', p_standard,
            'license', '1A2B-3C4D-5E6F-7G8H',
            'company', 'XNOX L.L.C'
        ),
        'active'
    )
    RETURNING * INTO v_project;
    
    RETURN v_project;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION create_project(VARCHAR(255), TEXT, DECIMAL(10,2), DECIMAL(5,2), VARCHAR(20), VARCHAR(255)) IS 
'Creates a new power system project with validation and default metadata. Returns the created project row.';

-- =============================================================================
-- SECTION 8: INITIAL SYSTEM CONFIGURATION
-- =============================================================================

-- Insert default license and system configuration
INSERT INTO system_config (config_key, config_value, description)
VALUES (
    'license', 
    '{"key": "1A2B-3C4D-5E6F-7G8H", "company": "XNOX L.L.C", "type": "standard", "status": "active"}'::jsonb,
    'System license information - XNOX L.L.C'
)
ON CONFLICT (config_key) DO UPDATE SET 
    config_value = EXCLUDED.config_value,
    updated_at = NOW();

INSERT INTO system_config (config_key, config_value, description)
VALUES (
    'system_settings', 
    '{
        "max_iterations_pf": 100,
        "convergence_tolerance": 1e-6,
        "default_algorithm": "newton_raphson",
        "parallel_computing": true,
        "auto_save_results": true,
        "version": "1.0.0"
    }'::jsonb,
    'Default power flow and system calculation settings'
)
ON CONFLICT (config_key) DO UPDATE SET 
    config_value = EXCLUDED.config_value,
    updated_at = NOW();

INSERT INTO system_config (config_key, config_value, description)
VALUES (
    'voltage_limits', 
    '{
        "default_vmin_pu": 0.90,
        "default_vmax_pu": 1.10,
        "transmission_vmin_pu": 0.95,
        "transmission_vmax_pu": 1.05,
        "distribution_vmin_pu": 0.90,
        "distribution_vmax_pu": 1.10
    }'::jsonb,
    'Default voltage limit settings for different voltage levels'
)
ON CONFLICT (config_key) DO UPDATE SET 
    config_value = EXCLUDED.config_value,
    updated_at = NOW();

-- =============================================================================
-- END OF SCHEMA
-- =============================================================================