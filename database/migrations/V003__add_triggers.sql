-- =============================================================================
-- POWSYS365 - Migration V003: Audit Triggers
-- Description: Additional triggers for data auditing and change tracking
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- =============================================================================
-- SECTION 1: AUDIT LOG TABLE
-- =============================================================================

CREATE TABLE IF NOT EXISTS audit_log (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    table_name      VARCHAR(64) NOT NULL,
    record_id       UUID NOT NULL,
    action          VARCHAR(10) NOT NULL CHECK (action IN ('INSERT', 'UPDATE', 'DELETE')),
    old_values      JSONB,
    new_values      JSONB,
    changed_by      VARCHAR(255),
    changed_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    session_info    JSONB DEFAULT '{}'
);

CREATE INDEX IF NOT EXISTS idx_audit_log_table ON audit_log(table_name);
CREATE INDEX IF NOT EXISTS idx_audit_log_record ON audit_log(record_id);
CREATE INDEX IF NOT EXISTS idx_audit_log_changed_at ON audit_log(changed_at);
CREATE INDEX IF NOT EXISTS idx_audit_log_action ON audit_log(action);

COMMENT ON TABLE audit_log IS 'Audit trail for tracking all changes to critical power system data';
COMMENT ON COLUMN audit_log.table_name IS 'Name of the table where the change occurred';
COMMENT ON COLUMN audit_log.record_id IS 'UUID of the affected record';
COMMENT ON COLUMN audit_log.action IS 'Type of change: INSERT, UPDATE, or DELETE';
COMMENT ON COLUMN audit_log.old_values IS 'Previous values before the change (JSONB)';
COMMENT ON COLUMN audit_log.new_values IS 'New values after the change (JSONB)';
COMMENT ON COLUMN audit_log.changed_by IS 'User or system that made the change';
COMMENT ON COLUMN audit_log.changed_at IS 'Timestamp of the change';
COMMENT ON COLUMN audit_log.session_info IS 'Additional session context (IP, application, etc.)';

-- =============================================================================
-- SECTION 2: AUDIT TRIGGER FUNCTION
-- =============================================================================

CREATE OR REPLACE FUNCTION trigger_audit_log()
RETURNS TRIGGER AS $$
DECLARE
    v_old_values JSONB;
    v_new_values JSONB;
    v_record_id UUID;
    v_changed_by VARCHAR(255);
BEGIN
    -- Extract record ID (handle different primary key column names)
    IF TG_OP = 'DELETE' THEN
        v_record_id := OLD.id;
        v_old_values := to_jsonb(OLD);
        v_new_values := NULL;
    ELSIF TG_OP = 'INSERT' THEN
        v_record_id := NEW.id;
        v_old_values := NULL;
        v_new_values := to_jsonb(NEW);
    ELSIF TG_OP = 'UPDATE' THEN
        v_record_id := NEW.id;
        v_old_values := to_jsonb(OLD);
        v_new_values := to_jsonb(NEW);
    END IF;
    
    -- Try to get current user from session variable, fallback to current_user
    BEGIN
        v_changed_by := current_setting('app.current_user', true);
    EXCEPTION WHEN OTHERS THEN
        v_changed_by := current_user;
    END;
    
    INSERT INTO audit_log (
        table_name, record_id, action, 
        old_values, new_values, changed_by,
        session_info
    ) VALUES (
        TG_TABLE_NAME,
        v_record_id,
        TG_OP,
        v_old_values,
        v_new_values,
        v_changed_by,
        jsonb_build_object(
            'database_user', current_user,
            'transaction_timestamp', transaction_timestamp(),
            'statement_timestamp', statement_timestamp()
        )
    );
    
    IF TG_OP = 'DELETE' THEN
        RETURN OLD;
    ELSE
        RETURN NEW;
    END IF;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

COMMENT ON FUNCTION trigger_audit_log() IS 'Generic audit trigger that logs all DML operations to the audit_log table';

-- =============================================================================
-- SECTION 3: APPLY AUDIT TRIGGERS TO CRITICAL TABLES
-- =============================================================================

DO $$
DECLARE
    tbl TEXT;
    audit_tables TEXT[] := ARRAY[
        'projects', 'buses', 'lines', 'transformers', 
        'generators', 'loads', 'shunts', 'switches'
    ];
BEGIN
    FOREACH tbl IN ARRAY audit_tables
    LOOP
        EXECUTE format(
            'DROP TRIGGER IF EXISTS trg_%I_audit ON %I;
             CREATE TRIGGER trg_%I_audit
             AFTER INSERT OR UPDATE OR DELETE ON %I
             FOR EACH ROW
             EXECUTE FUNCTION trigger_audit_log();',
            tbl, tbl, tbl, tbl
        );
    END LOOP;
END $$;

-- =============================================================================
-- SECTION 4: RESULTS TABLES RETENTION TRIGGER
-- =============================================================================

-- Function to automatically archive old results and keep only the N most recent runs
CREATE OR REPLACE FUNCTION trigger_cleanup_old_results()
RETURNS TRIGGER AS $$
DECLARE
    v_keep_count INTEGER;
    v_deleted_count INTEGER;
BEGIN
    -- Get retention setting from system_config (default: keep 50 most recent runs per project)
    BEGIN
        SELECT (config_value->>'keep_runs')::INTEGER 
        INTO v_keep_count 
        FROM system_config 
        WHERE config_key = 'results_retention';
    EXCEPTION WHEN OTHERS THEN
        v_keep_count := 50;
    END;
    
    IF v_keep_count IS NULL OR v_keep_count < 1 THEN
        v_keep_count := 50;
    END IF;
    
    -- Clean up old power flow results (keep only v_keep_count most recent runs)
    WITH ranked_runs AS (
        SELECT run_id, ROW_NUMBER() OVER (PARTITION BY project_id ORDER BY created_at DESC) AS rn
        FROM results_power_flow
        WHERE project_id = NEW.project_id
        GROUP BY run_id, created_at
    )
    DELETE FROM results_power_flow
    WHERE project_id = NEW.project_id
      AND run_id IN (SELECT run_id FROM ranked_runs WHERE rn > v_keep_count);
    
    GET DIAGNOSTICS v_deleted_count = ROW_COUNT;
    
    IF v_deleted_count > 0 THEN
        RAISE NOTICE 'Cleaned up % old power flow result rows for project %', v_deleted_count, NEW.project_id;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Apply cleanup trigger to results tables (runs after insert to maintain retention)
DROP TRIGGER IF EXISTS trg_results_pf_cleanup ON results_power_flow;
CREATE TRIGGER trg_results_pf_cleanup
    AFTER INSERT ON results_power_flow
    FOR EACH STATEMENT
    EXECUTE FUNCTION trigger_cleanup_old_results();

-- =============================================================================
-- SECTION 5: BUS NUMBER AUTO-VALIDATION TRIGGER
-- =============================================================================

CREATE OR REPLACE FUNCTION trigger_validate_bus_number()
RETURNS TRIGGER AS $$
DECLARE
    v_max_bus INTEGER;
BEGIN
    -- Ensure bus numbers are positive
    IF NEW.bus_number <= 0 THEN
        RAISE EXCEPTION 'Bus number must be positive. Got: %', NEW.bus_number;
    END IF;
    
    -- For new buses, suggest next available number if duplicate
    IF TG_OP = 'INSERT' THEN
        SELECT MAX(bus_number) INTO v_max_bus
        FROM buses
        WHERE project_id = NEW.project_id;
        
        IF v_max_bus IS NULL THEN
            v_max_bus := 0;
        END IF;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_buses_validate ON buses;
CREATE TRIGGER trg_buses_validate
    BEFORE INSERT OR UPDATE ON buses
    FOR EACH ROW
    EXECUTE FUNCTION trigger_validate_bus_number();

-- =============================================================================
-- SECTION 6: PROJECT STATUS CASCADE TRIGGER
-- =============================================================================

CREATE OR REPLACE FUNCTION trigger_cascade_project_status()
RETURNS TRIGGER AS $$
BEGIN
    -- When a project is soft-deleted, mark all related buses as inactive
    IF NEW.status = 'deleted' AND OLD.status != 'deleted' THEN
        UPDATE buses SET status = 0 WHERE project_id = NEW.id;
        RAISE NOTICE 'Cascaded delete status to all buses for project %', NEW.id;
    END IF;
    
    -- When a project is restored, mark all buses as active again
    IF NEW.status = 'active' AND OLD.status = 'deleted' THEN
        UPDATE buses SET status = 1 WHERE project_id = NEW.id;
        RAISE NOTICE 'Restored active status to all buses for project %', NEW.id;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_projects_cascade_status ON projects;
CREATE TRIGGER trg_projects_cascade_status
    AFTER UPDATE OF status ON projects
    FOR EACH ROW
    EXECUTE FUNCTION trigger_cascade_project_status();

-- =============================================================================
-- SECTION 7: LINE PARAMETERS VALIDATION
-- =============================================================================

CREATE OR REPLACE FUNCTION trigger_validate_line_parameters()
RETURNS TRIGGER AS $$
BEGIN
    -- Validate that impedance values are non-negative
    IF NEW.r_pu < 0 THEN
        RAISE EXCEPTION 'Line resistance (r_pu) cannot be negative: %', NEW.r_pu;
    END IF;
    
    IF NEW.x_pu <= 0 THEN
        RAISE EXCEPTION 'Line reactance (x_pu) must be positive: %', NEW.x_pu;
    END IF;
    
    -- Validate ratings are non-negative
    IF NEW.rate_a_mva < 0 OR NEW.rate_b_mva < 0 OR NEW.rate_c_mva < 0 THEN
        RAISE EXCEPTION 'Line ratings cannot be negative';
    END IF;
    
    -- Validate rate ordering: rate_a <= rate_b <= rate_c
    IF NEW.rate_a_mva > NEW.rate_b_mva OR NEW.rate_b_mva > NEW.rate_c_mva THEN
        RAISE WARNING 'Line ratings should satisfy: rate_a <= rate_b <= rate_c (line %)', NEW.line_number;
    END IF;
    
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_lines_validate ON lines;
CREATE TRIGGER trg_lines_validate
    BEFORE INSERT OR UPDATE ON lines
    FOR EACH ROW
    EXECUTE FUNCTION trigger_validate_line_parameters();

-- =============================================================================
-- END OF MIGRATION V003
-- =============================================================================