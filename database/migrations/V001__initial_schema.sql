-- =============================================================================
-- POWSYS365 - Migration V001: Initial Schema
-- Description: Applies the complete POWSYS365 database schema
-- Compatible with: PostgreSQL 15+
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- =============================================================================
-- SECTION 1: EXTENSIONS
-- =============================================================================

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- =============================================================================
-- SECTION 2: CUSTOM ENUMERATION TYPES
-- =============================================================================

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'bus_type_enum') THEN
        CREATE TYPE bus_type_enum AS ENUM ('PQ', 'PV', 'Slack');
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'gen_type_enum') THEN
        CREATE TYPE gen_type_enum AS ENUM (
            'thermal', 'hydro', 'nuclear', 'wind', 'solar', 
            'diesel', 'gas', 'biomass', 'battery'
        );
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'load_model_enum') THEN
        CREATE TYPE load_model_enum AS ENUM (
            'constant_pq', 'constant_z', 'constant_i', 'zip'
        );
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'shunt_type_enum') THEN
        CREATE TYPE shunt_type_enum AS ENUM (
            'fixed', 'switched', 'svc', 'statcom'
        );
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'switch_type_enum') THEN
        CREATE TYPE switch_type_enum AS ENUM (
            'breaker', 'disconnect', 'fuse', 'load_break'
        );
    END IF;
END $$;

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
-- projects: Power system analysis projects/cases
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

-- ---------------------------------------------------------------------------
-- buses: Electrical buses (nodes) in the power system
-- Bus types: 1=PQ, 2=PV, 3=Slack
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

-- ---------------------------------------------------------------------------
-- lines: Transmission lines with pi-model parameters
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

-- ---------------------------------------------------------------------------
-- transformers: Two-winding power transformers
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

-- ---------------------------------------------------------------------------
-- generators: Synchronous generators and inverter-based resources
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

-- ---------------------------------------------------------------------------
-- loads: Electrical loads with multiple model support
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

-- ---------------------------------------------------------------------------
-- shunts: Shunt compensation devices (capacitors, reactors, SVC, STATCOM)
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

-- ---------------------------------------------------------------------------
-- switches: Circuit breakers, disconnectors, fuses
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

-- ---------------------------------------------------------------------------
-- load_profiles: Time-series load profiles for time-varying studies
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

-- ---------------------------------------------------------------------------
-- results_power_flow: Power flow (load flow) results per bus
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

-- ---------------------------------------------------------------------------
-- results_power_flow_lines: Power flow results per transmission line
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

-- ---------------------------------------------------------------------------
-- results_short_circuit: Short circuit analysis results per bus
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

-- ---------------------------------------------------------------------------
-- system_config: System-wide configuration key-value store
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS system_config (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    config_key      VARCHAR(255) NOT NULL UNIQUE,
    config_value    JSONB NOT NULL DEFAULT '{}',
    description     TEXT,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- =============================================================================
-- SECTION 4: INDICES
-- =============================================================================

CREATE INDEX IF NOT EXISTS idx_projects_status ON projects(status);
CREATE INDEX IF NOT EXISTS idx_projects_created_at ON projects(created_at);
CREATE INDEX IF NOT EXISTS idx_buses_project ON buses(project_id);
CREATE INDEX IF NOT EXISTS idx_buses_project_number ON buses(project_id, bus_number);
CREATE INDEX IF NOT EXISTS idx_buses_type ON buses(bus_type);
CREATE INDEX IF NOT EXISTS idx_buses_area ON buses(area);
CREATE INDEX IF NOT EXISTS idx_lines_project ON lines(project_id);
CREATE INDEX IF NOT EXISTS idx_lines_from_bus ON lines(from_bus);
CREATE INDEX IF NOT EXISTS idx_lines_to_bus ON lines(to_bus);
CREATE INDEX IF NOT EXISTS idx_lines_from_to ON lines(from_bus, to_bus);
CREATE INDEX IF NOT EXISTS idx_lines_status ON lines(status);
CREATE INDEX IF NOT EXISTS idx_transformers_project ON transformers(project_id);
CREATE INDEX IF NOT EXISTS idx_transformers_hv_bus ON transformers(hv_bus);
CREATE INDEX IF NOT EXISTS idx_transformers_lv_bus ON transformers(lv_bus);
CREATE INDEX IF NOT EXISTS idx_generators_project ON generators(project_id);
CREATE INDEX IF NOT EXISTS idx_generators_bus ON generators(bus_id);
CREATE INDEX IF NOT EXISTS idx_generators_type ON generators(gen_type);
CREATE INDEX IF NOT EXISTS idx_generators_bus_type ON generators(bus_id, gen_type);
CREATE INDEX IF NOT EXISTS idx_loads_project ON loads(project_id);
CREATE INDEX IF NOT EXISTS idx_loads_bus ON loads(bus_id);
CREATE INDEX IF NOT EXISTS idx_loads_profile ON loads(profile_id);
CREATE INDEX IF NOT EXISTS idx_shunts_project ON shunts(project_id);
CREATE INDEX IF NOT EXISTS idx_shunts_bus ON shunts(bus_id);
CREATE INDEX IF NOT EXISTS idx_switches_project ON switches(project_id);
CREATE INDEX IF NOT EXISTS idx_switches_from ON switches(bus_from);
CREATE INDEX IF NOT EXISTS idx_switches_to ON switches(bus_to);
CREATE INDEX IF NOT EXISTS idx_load_profiles_project ON load_profiles(project_id);
CREATE INDEX IF NOT EXISTS idx_load_profiles_bus ON load_profiles(bus_id);
CREATE INDEX IF NOT EXISTS idx_load_profiles_timestamp ON load_profiles(timestamp);
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
-- SECTION 5: TRIGGERS
-- =============================================================================

CREATE OR REPLACE FUNCTION trigger_set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

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
    COALESCE(rpfl.s_mva, 
        SQRT(
            POWER((l.r_pu / NULLIF(l.r_pu*l.r_pu + l.x_pu*l.x_pu, 0)) * l.rate_a_mva, 2) + 
            POWER((l.x_pu / NULLIF(l.r_pu*l.r_pu + l.x_pu*l.x_pu, 0)) * l.rate_a_mva, 2)
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
      COALESCE(rpfl.loading_percent, 0) > 80
      OR rpfl.loading_percent IS NULL
  );

-- =============================================================================
-- SECTION 7: FUNCTIONS
-- =============================================================================

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
    IF p_standard NOT IN ('IEEE', 'IEC') THEN
        RAISE EXCEPTION 'Invalid standard: %. Must be IEEE or IEC', p_standard;
    END IF;
    IF p_base_mva <= 0 THEN
        RAISE EXCEPTION 'Base MVA must be positive';
    END IF;
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

-- =============================================================================
-- SECTION 8: INITIAL CONFIGURATION
-- =============================================================================

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
    '{"max_iterations_pf": 100, "convergence_tolerance": 1e-6, "default_algorithm": "newton_raphson", "parallel_computing": true, "auto_save_results": true, "version": "1.0.0"}'::jsonb,
    'Default power flow and system calculation settings'
)
ON CONFLICT (config_key) DO UPDATE SET 
    config_value = EXCLUDED.config_value,
    updated_at = NOW();

INSERT INTO system_config (config_key, config_value, description)
VALUES (
    'voltage_limits', 
    '{"default_vmin_pu": 0.90, "default_vmax_pu": 1.10, "transmission_vmin_pu": 0.95, "transmission_vmax_pu": 1.05, "distribution_vmin_pu": 0.90, "distribution_vmax_pu": 1.10}'::jsonb,
    'Default voltage limit settings for different voltage levels'
)
ON CONFLICT (config_key) DO UPDATE SET 
    config_value = EXCLUDED.config_value,
    updated_at = NOW();

-- =============================================================================
-- END OF MIGRATION V001
-- =============================================================================