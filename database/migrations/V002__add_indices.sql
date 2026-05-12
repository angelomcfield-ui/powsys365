-- Migracion: Indices adicionales para rendimiento
CREATE INDEX IF NOT EXISTS idx_buses_project_number ON buses(project_id, bus_number);
CREATE INDEX IF NOT EXISTS idx_lines_from_to ON lines(from_bus, to_bus);
CREATE INDEX IF NOT EXISTS idx_generators_bus_type ON generators(bus_id, gen_type);
CREATE INDEX IF NOT EXISTS idx_results_pf_project_run ON results_power_flow(project_id, run_id);
CREATE INDEX IF NOT EXISTS idx_results_sc_project_run ON results_short_circuit(project_id, run_id);