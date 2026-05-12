-- =============================================================================
-- POWSYS365 - Migration V002: Additional Performance Indices
-- Description: Adds supplementary indices for frequently queried patterns
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- Composite index for bus lookups by project and number (common query pattern
-- when importing/exporting power flow cases in PSS/E or PSS(R)SINCAL formats)
CREATE INDEX IF NOT EXISTS idx_buses_project_number ON buses(project_id, bus_number);

-- Composite index for line connectivity queries (used in topology processing,
-- island detection, and adjacency matrix construction)
CREATE INDEX IF NOT EXISTS idx_lines_from_to ON lines(from_bus, to_bus);

-- Composite index for generator filtering by bus and type (used in generation
-- dispatch views and renewable integration studies)
CREATE INDEX IF NOT EXISTS idx_generators_bus_type ON generators(bus_id, gen_type);

-- Composite index for power flow results retrieval (primary access pattern
-- for result visualization and post-processing analysis)
CREATE INDEX IF NOT EXISTS idx_results_pf_project_run ON results_power_flow(project_id, run_id);

-- Composite index for short circuit results retrieval (primary access pattern
-- for protection coordination studies and breaker sizing)
CREATE INDEX IF NOT EXISTS idx_results_sc_project_run ON results_short_circuit(project_id, run_id);

-- Additional performance indices for common analytical queries

-- Index for voltage violation queries (sorted by deviation magnitude)
CREATE INDEX IF NOT EXISTS idx_buses_vm_check ON buses(project_id, vm_pu, vmin_pu, vmax_pu);

-- Index for equipment status filtering (commonly used in N-1 contingency)
CREATE INDEX IF NOT EXISTS idx_lines_status_project ON lines(project_id, status);

-- Index for generator dispatch ordered by output
CREATE INDEX IF NOT EXISTS idx_generators_project_p ON generators(project_id, p_mw DESC);

-- Index for load profile time-series queries
CREATE INDEX IF NOT EXISTS idx_load_profiles_time_range ON load_profiles(project_id, profile_name, timestamp);

-- Index for transformer tap position tracking
CREATE INDEX IF NOT EXISTS idx_transformers_tap ON transformers(project_id, tap_nom);

-- Partial index for active equipment only (excludes out-of-service elements)
CREATE INDEX IF NOT EXISTS idx_lines_active ON lines(project_id) WHERE status = 1;
CREATE INDEX IF NOT EXISTS idx_generators_active ON generators(project_id) WHERE status = 1;
CREATE INDEX IF NOT EXISTS idx_loads_active ON loads(project_id) WHERE status = 1;
CREATE INDEX IF NOT EXISTS idx_transformers_active ON transformers(project_id) WHERE status = 1;

-- GIN index for JSONB properties (enables efficient JSON queries on flexible attributes)
CREATE INDEX IF NOT EXISTS idx_projects_metadata ON projects USING GIN (metadata);
CREATE INDEX IF NOT EXISTS idx_buses_properties ON buses USING GIN (properties);
CREATE INDEX IF NOT EXISTS idx_lines_properties ON lines USING GIN (properties);
CREATE INDEX IF NOT EXISTS idx_generators_properties ON generators USING GIN (properties);
CREATE INDEX IF NOT EXISTS idx_results_pf_violations ON results_power_flow USING GIN (violations);
CREATE INDEX IF NOT EXISTS idx_results_sc_contributions ON results_short_circuit USING GIN (contributions);

-- =============================================================================
-- END OF MIGRATION V002
-- =============================================================================