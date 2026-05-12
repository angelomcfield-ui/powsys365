-- ============================================================
-- POWSYS365 - PostgreSQL 15 Schema Completo
-- Sistema de Analisis de Sistemas Electricos de Potencia
-- Compatible: macOS 12+, Windows, Linux
-- ============================================================

-- Extensiones requeridas
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ============================================================
-- 1. TABLA DE PROYECTOS
-- ============================================================
CREATE TABLE IF NOT EXISTS projects (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name VARCHAR(255) NOT NULL UNIQUE,
    description TEXT,
    base_mva DECIMAL(10,2) DEFAULT 100.0,
    frequency DECIMAL(5,2) DEFAULT 60.0,
    standard VARCHAR(50) DEFAULT 'IEEE',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    status VARCHAR(20) DEFAULT 'active' CHECK (status IN ('active', 'archived', 'deleted'))
);

CREATE INDEX idx_projects_name ON projects(name);
CREATE INDEX idx_projects_status ON projects(status);

-- Trigger para updated_at
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_projects_updated_at
    BEFORE UPDATE ON projects
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================
-- 2. TABLA DE BARRAS (BUSES)
-- ============================================================
CREATE TABLE IF NOT EXISTS buses (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    bus_number INTEGER NOT NULL,
    bus_type INTEGER DEFAULT 1 CHECK (bus_type IN (1,2,3)), -- 1=PQ, 2=PV, 3=Slack
    name VARCHAR(255),
    v_base_kv DECIMAL(10,2),
    vm_pu DECIMAL(8,6) DEFAULT 1.0,
    va_deg DECIMAL(8,6) DEFAULT 0.0,
    vmax_pu DECIMAL(8,6) DEFAULT 1.1,
    vmin_pu DECIMAL(8,6) DEFAULT 0.9,
    area INTEGER DEFAULT 1,
    zone INTEGER DEFAULT 1,
    status INTEGER DEFAULT 1, -- 1=active, 0=out
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(project_id, bus_number)
);

CREATE INDEX idx_buses_project ON buses(project_id);
CREATE INDEX idx_buses_number ON buses(bus_number);
CREATE INDEX idx_buses_type ON buses(bus_type);
CREATE INDEX idx_buses_area ON buses(area);

COMMENT ON COLUMN buses.bus_type IS '1=PQ (load), 2=PV (generator), 3=Slack (reference)';

-- ============================================================
-- 3. TABLA DE LINEAS DE TRANSMISION
-- ============================================================
CREATE TABLE IF NOT EXISTS lines (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name VARCHAR(255),
    from_bus INTEGER NOT NULL,
    to_bus INTEGER NOT NULL,
    r_pu DECIMAL(12,8),
    x_pu DECIMAL(12,8),
    b_pu DECIMAL(12,8),
    length_km DECIMAL(10,2),
    status INTEGER DEFAULT 1, -- 1=active, 0=out
    created_at TIMESTAMPTZ DEFAULT NOW(),
    CHECK (from_bus != to_bus)
);

CREATE INDEX idx_lines_project ON lines(project_id);
CREATE INDEX idx_lines_from_bus ON lines(from_bus);
CREATE INDEX idx_lines_to_bus ON lines(to_bus);
CREATE INDEX idx_lines_status ON lines(status);

-- ============================================================
-- 4. TABLA DE TRANSFORMADORES (2 DEVANADOS)
-- ============================================================
CREATE TABLE IF NOT EXISTS transformers (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name VARCHAR(255),
    hv_bus INTEGER NOT NULL,
    lv_bus INTEGER NOT NULL,
    sn_mva DECIMAL(10,2),
    vn_hv_kv DECIMAL(10,2),
    vn_lv_kv DECIMAL(10,2),
    vk_percent DECIMAL(8,4),
    vkr_percent DECIMAL(8,4),
    pfe_kw DECIMAL(10,2),
    i0_percent DECIMAL(8,4),
    tap_side VARCHAR(2) DEFAULT 'HV' CHECK (tap_side IN ('HV','LV')),
    tap_pos INTEGER DEFAULT 0,
    tap_min INTEGER DEFAULT -10,
    tap_max INTEGER DEFAULT 10,
    tap_step_percent DECIMAL(8,4) DEFAULT 1.0,
    status INTEGER DEFAULT 1,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    CHECK (hv_bus != lv_bus)
);

CREATE INDEX idx_transformers_project ON transformers(project_id);
CREATE INDEX idx_transformers_hv_bus ON transformers(hv_bus);
CREATE INDEX idx_transformers_lv_bus ON transformers(lv_bus);

-- ============================================================
-- 5. TABLA DE GENERADORES
-- ============================================================
CREATE TABLE IF NOT EXISTS generators (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    bus_id UUID NOT NULL REFERENCES buses(id) ON DELETE CASCADE,
    gen_number INTEGER NOT NULL,
    name VARCHAR(255),
    p_mw DECIMAL(12,2),
    q_mvar DECIMAL(12,2),
    p_max_mw DECIMAL(12,2),
    p_min_mw DECIMAL(12,2),
    q_max_mvar DECIMAL(12,2),
    q_min_mvar DECIMAL(12,2),
    vn_kv DECIMAL(10,2),
    xd_pu DECIMAL(10,6),
    xq_pu DECIMAL(10,6),
    xd_prime_pu DECIMAL(10,6),
    xq_prime_pu DECIMAL(10,6),
    xd_double_prime_pu DECIMAL(10,6),
    xq_double_prime_pu DECIMAL(10,6),
    td0_sec DECIMAL(10,4),
    tq0_sec DECIMAL(10,4),
    td_prime_sec DECIMAL(10,4),
    tq_prime_sec DECIMAL(10,4),
    td_double_prime_sec DECIMAL(10,4),
    tq_double_prime_sec DECIMAL(10,4),
    h_sec DECIMAL(10,4), -- inertia constant
    d_percent DECIMAL(8,4), -- damping
    gen_type VARCHAR(20) DEFAULT 'sync' CHECK (gen_type IN ('sync','pv','slack')),
    status INTEGER DEFAULT 1,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(project_id, gen_number)
);

CREATE INDEX idx_generators_project ON generators(project_id);
CREATE INDEX idx_generators_bus ON generators(bus_id);
CREATE INDEX idx_generators_type ON generators(gen_type);

-- ============================================================
-- 6. TABLA DE CARGAS
-- ============================================================
CREATE TABLE IF NOT EXISTS loads (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    bus_id UUID NOT NULL REFERENCES buses(id) ON DELETE CASCADE,
    load_number INTEGER NOT NULL,
    name VARCHAR(255),
    p_mw DECIMAL(12,2),
    q_mvar DECIMAL(12,2),
    status INTEGER DEFAULT 1,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(project_id, load_number)
);

CREATE INDEX idx_loads_project ON loads(project_id);
CREATE INDEX idx_loads_bus ON loads(bus_id);

-- ============================================================
-- 7. TABLA DE COMPENSADORES SHUNT
-- ============================================================
CREATE TABLE IF NOT EXISTS shunts (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    bus_id UUID NOT NULL REFERENCES buses(id) ON DELETE CASCADE,
    name VARCHAR(255),
    p_mw DECIMAL(12,2) DEFAULT 0.0,
    q_mvar DECIMAL(12,2),
    vn_kv DECIMAL(10,2),
    step INTEGER DEFAULT 1,
    status INTEGER DEFAULT 1,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_shunts_bus ON shunts(bus_id);

-- ============================================================
-- 8. TABLA DE INTERRUPTORES / SWITCHES
-- ============================================================
CREATE TABLE IF NOT EXISTS switches (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name VARCHAR(255),
    bus_from INTEGER NOT NULL,
    bus_to INTEGER NOT NULL,
    status INTEGER DEFAULT 1, -- 1=closed, 0=open
    created_at TIMESTAMPTZ DEFAULT NOW(),
    CHECK (bus_from != bus_to)
);

CREATE INDEX idx_switches_bus_from ON switches(bus_from);
CREATE INDEX idx_switches_bus_to ON switches(bus_to);

-- ============================================================
-- 9. TABLA DE PERFILES DE CARGA (SERIES TEMPORALES)
-- ============================================================
CREATE TABLE IF NOT EXISTS load_profiles (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    bus_id UUID NOT NULL REFERENCES buses(id) ON DELETE CASCADE,
    timestamp TIMESTAMPTZ NOT NULL,
    p_mw DECIMAL(12,2),
    q_mvar DECIMAL(12,2),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_load_profiles_project ON load_profiles(project_id);
CREATE INDEX idx_load_profiles_bus ON load_profiles(bus_id);
CREATE INDEX idx_load_profiles_time ON load_profiles(timestamp);
CREATE INDEX idx_load_profiles_project_time ON load_profiles(project_id, timestamp);

-- ============================================================
-- 10. TABLA DE RESULTADOS DE FLUJO DE CARGA
-- ============================================================
CREATE TABLE IF NOT EXISTS results_power_flow (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    run_id UUID NOT NULL,
    bus_id UUID NOT NULL REFERENCES buses(id) ON DELETE CASCADE,
    vm_pu DECIMAL(8,6),
    va_deg DECIMAL(8,6),
    p_mw DECIMAL(12,2),
    q_mvar DECIMAL(12,2),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_pf_results_project ON results_power_flow(project_id);
CREATE INDEX idx_pf_results_run ON results_power_flow(run_id);
CREATE INDEX idx_pf_results_bus ON results_power_flow(bus_id);

-- ============================================================
-- 11. TABLA DE RESULTADOS DE FLUJO POR LINEA
-- ============================================================
CREATE TABLE IF NOT EXISTS results_power_flow_lines (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    run_id UUID NOT NULL,
    line_id UUID NOT NULL REFERENCES lines(id) ON DELETE CASCADE,
    p_from_mw DECIMAL(12,2),
    q_from_mvar DECIMAL(12,2),
    p_to_mw DECIMAL(12,2),
    q_to_mvar DECIMAL(12,2),
    loading_percent DECIMAL(8,4),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_pf_line_results_run ON results_power_flow_lines(run_id);

-- ============================================================
-- 12. TABLA DE RESULTADOS DE CORTOCIRCUITO
-- ============================================================
CREATE TABLE IF NOT EXISTS results_short_circuit (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    project_id UUID NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    run_id UUID NOT NULL,
    fault_type VARCHAR(20) NOT NULL CHECK (fault_type IN ('3ph','1ph','2ph','2ph-g')),
    fault_bus INTEGER NOT NULL,
    ik_ka DECIMAL(12,2),
    rk_ohm DECIMAL(12,6),
    xk_ohm DECIMAL(12,6),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_sc_results_run ON results_short_circuit(run_id);
CREATE INDEX idx_sc_results_fault ON results_short_circuit(fault_type);

-- ============================================================
-- 13. TABLA DE CONFIGURACION DEL SISTEMA
-- ============================================================
CREATE TABLE IF NOT EXISTS system_config (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    key VARCHAR(255) UNIQUE NOT NULL,
    value TEXT,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- 14. VISTAS UTILES
-- ============================================================

-- Vista resumen del sistema por proyecto
CREATE OR REPLACE VIEW v_system_summary AS
SELECT 
    p.id AS project_id,
    p.name AS project_name,
    COUNT(DISTINCT b.id) AS num_buses,
    COUNT(DISTINCT l.id) AS num_lines,
    COUNT(DISTINCT t.id) AS num_transformers,
    COUNT(DISTINCT g.id) AS num_generators,
    SUM(g.p_mw) AS total_gen_mw,
    SUM(ld.p_mw) AS total_load_mw,
    SUM(ld.q_mvar) AS total_load_mvar
FROM projects p
LEFT JOIN buses b ON b.project_id = p.id
LEFT JOIN lines l ON l.project_id = p.id AND l.status = 1
LEFT JOIN transformers t ON t.project_id = p.id AND t.status = 1
LEFT JOIN generators g ON g.project_id = p.id AND g.status = 1
LEFT JOIN loads ld ON ld.project_id = p.id AND ld.status = 1
LEFT JOIN switches s ON s.project_id = p.id
WHERE p.status = 'active'
GROUP BY p.id, p.name;

-- Vista de barras con tipo descriptivo
CREATE OR REPLACE VIEW v_buses_detail AS
SELECT 
    b.*,
    CASE b.bus_type
        WHEN 1 THEN 'PQ (Load Bus)'
        WHEN 2 THEN 'PV (Generator Bus)'
        WHEN 3 THEN 'Slack (Reference Bus)'
        ELSE 'Unknown'
    END AS bus_type_desc
FROM buses b;

-- Vista de violaciones de voltaje
CREATE OR REPLACE VIEW v_voltage_violations AS
SELECT 
    pf.project_id,
    pf.bus_id,
    b.bus_number,
    b.name AS bus_name,
    pf.vm_pu,
    b.vmin_pu,
    b.vmax_pu,
    ABS(pf.vm_pu - CASE WHEN pf.vm_pu < b.vmin_pu THEN b.vmin_pu ELSE b.vmax_pu END) AS violation_magnitude
FROM results_power_flow pf
JOIN buses b ON pf.bus_id = b.id
WHERE pf.vm_pu < b.vmin_pu OR pf.vm_pu > b.vmax_pu;

-- Vista de sobrecargas de linea
CREATE OR REPLACE VIEW v_line_overloads AS
SELECT 
    pfl.project_id,
    pfl.line_id,
    l.name AS line_name,
    l.from_bus,
    l.to_bus,
    pfl.loading_percent,
    CASE 
        WHEN pfl.loading_percent > 100 THEN 'Critical'
        WHEN pfl.loading_percent > 80 THEN 'Warning'
        ELSE 'Normal'
    END AS severity
FROM results_power_flow_lines pfl
JOIN lines l ON pfl.line_id = l.id
WHERE pfl.loading_percent > 80;

-- ============================================================
-- 15. FUNCIONES AUXILIARES
-- ============================================================

-- Funcion: Calcular perdidas totales por run
CREATE OR REPLACE FUNCTION get_total_losses(run_uuid UUID)
RETURNS TABLE(total_p_loss_mw DECIMAL, total_q_loss_mvar DECIMAL, total_buses BIGINT, total_lines BIGINT) AS $$
BEGIN
    RETURN QUERY
    SELECT 
        SUM(ABS(pfl.p_from_mw + pfl.p_to_mw)) / 2 AS total_p_loss_mw,
        SUM(ABS(pfl.q_from_mvar + pfl.q_to_mvar)) / 2 AS total_q_loss_mvar,
        COUNT(DISTINCT pf.bus_id) AS total_buses,
        COUNT(DISTINCT pfl.line_id) AS total_lines
    FROM results_power_flow pf
    LEFT JOIN results_power_flow_lines pfl ON pf.run_id = pfl.run_id
    WHERE pf.run_id = run_uuid;
END;
$$ LANGUAGE plpgsql;

-- Funcion: Crear un nuevo proyecto con valores por defecto
CREATE OR REPLACE FUNCTION create_project(
    p_name VARCHAR(255),
    p_description TEXT DEFAULT '',
    p_base_mva DECIMAL DEFAULT 100.0,
    p_frequency DECIMAL DEFAULT 60.0,
    p_standard VARCHAR DEFAULT 'IEEE'
) RETURNS UUID AS $$
DECLARE
    new_project_id UUID;
BEGIN
    INSERT INTO projects (name, description, base_mva, frequency, standard)
    VALUES (p_name, p_description, p_base_mva, p_frequency, p_standard)
    RETURNING id INTO new_project_id;
    RETURN new_project_id;
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- FIN DEL SCHEMA
-- ============================================================
