-- =============================================================================
-- POWSYS365 - Seed Data: IEEE 14 Bus Test System
-- Description: Complete IEEE 14-bus power system test case
--              Standard benchmark for power flow algorithm validation
-- Source: IEEE PES Test Systems, based on American Electric Power System
-- Base MVA: 100, Frequency: 60 Hz
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- Clean up any existing IEEE 14 data first (for idempotent seeding)
-- This ensures we can re-run the seed script without duplicates
DO $$
DECLARE
    v_project_id UUID;
BEGIN
    SELECT id INTO v_project_id FROM projects WHERE name = 'IEEE 14 Bus Test System';
    IF v_project_id IS NOT NULL THEN
        DELETE FROM results_power_flow_lines WHERE project_id = v_project_id;
        DELETE FROM results_power_flow WHERE project_id = v_project_id;
        DELETE FROM results_short_circuit WHERE project_id = v_project_id;
        DELETE FROM load_profiles WHERE project_id = v_project_id;
        DELETE FROM switches WHERE project_id = v_project_id;
        DELETE FROM shunts WHERE project_id = v_project_id;
        DELETE FROM loads WHERE project_id = v_project_id;
        DELETE FROM generators WHERE project_id = v_project_id;
        DELETE FROM transformers WHERE project_id = v_project_id;
        DELETE FROM lines WHERE project_id = v_project_id;
        DELETE FROM buses WHERE project_id = v_project_id;
        DELETE FROM projects WHERE id = v_project_id;
    END IF;
END $$;

-- =============================================================================
-- STEP 1: Create Project
-- =============================================================================

INSERT INTO projects (id, name, description, created_by, base_mva, frequency, standard, metadata, status)
VALUES (
    'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11',
    'IEEE 14 Bus Test System',
    'Standard IEEE 14-bus test system for power flow validation. Represents a portion of the American Electric Power System (in the Midwestern US) as of February 1962. Contains 5 generators, 11 loads, 20 branches (17 lines + 3 transformers).',
    'system_seed',
    100.0,
    60.0,
    'IEEE',
    '{
        "source": "IEEE PES Test Systems",
        "date": "1962-02",
        "region": "Midwestern US",
        "utility": "American Electric Power",
        "note": "Classic benchmark for power flow algorithm comparison",
        "reference": "Christie et al., The IEEE Reliability Test System-1996, IEEE Trans. Power Systems",
        "seed_version": "1.0"
    }'::jsonb,
    'active'
);

-- =============================================================================
-- STEP 2: Insert Buses
-- Bus types: 1=PQ, 2=PV, 3=Slack
-- =============================================================================

INSERT INTO buses (id, project_id, bus_number, name, vn_kv, bus_type, area, zone, latitude, longitude, vm_pu, va_deg, vmin_pu, vmax_pu, properties) VALUES
-- Bus 1: Slack bus (reference), 69 kV
('b0000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'Bus 1 - Slack', 69.0000, 3, 1, 1, 40.4406, -86.9122, 1.060000, 0.000000, 0.950000, 1.100000, '{"source_type": "slack", "area_name": "Main"}'::jsonb),

-- Bus 2: PV bus, 69 kV
('b0000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'Bus 2 - PV', 69.0000, 2, 1, 1, 40.4200, -86.8800, 1.045000, -4.982589, 0.950000, 1.100000, '{"source_type": "pv", "area_name": "Main"}'::jsonb),

-- Bus 3: PV bus, 69 kV
('b0000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'Bus 3 - PV', 69.0000, 2, 1, 1, 40.4000, -86.8500, 1.010000, -12.725100, 0.950000, 1.100000, '{"source_type": "pv", "area_name": "Main"}'::jsonb),

-- Bus 4: PQ bus, 69 kV
('b0000004-0000-0000-0000-000000000004', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 4, 'Bus 4 - PQ', 69.0000, 1, 1, 1, 40.3800, -86.8200, 1.019257, -10.312858, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Main"}'::jsonb),

-- Bus 5: PQ bus, 69 kV
('b0000005-0000-0000-0000-000000000005', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 5, 'Bus 5 - PQ', 69.0000, 1, 1, 1, 40.3600, -86.7900, 1.020150, -8.773520, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Main"}'::jsonb),

-- Bus 6: PV bus, 13.8 kV (lower voltage via transformer)
('b0000006-0000-0000-0000-000000000006', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 6, 'Bus 6 - PV', 13.8000, 2, 1, 1, 40.3400, -86.7600, 1.070000, -14.220924, 0.950000, 1.100000, '{"source_type": "pv", "area_name": "Distribution"}'::jsonb),

-- Bus 7: PQ bus, 13.8 kV
('b0000007-0000-0000-0000-000000000007', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 7, 'Bus 7 - PQ', 13.8000, 1, 1, 1, 40.3200, -86.7300, 1.062386, -13.359638, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb),

-- Bus 8: PV bus, 18 kV (generator bus)
('b0000008-0000-0000-0000-000000000008', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 8, 'Bus 8 - PV', 18.0000, 2, 1, 1, 40.3000, -86.7000, 1.090000, -13.359638, 0.950000, 1.100000, '{"source_type": "pv", "area_name": "Distribution"}'::jsonb),

-- Bus 9: PQ bus, 13.8 kV (has shunt compensation)
('b0000009-0000-0000-0000-000000000009', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 9, 'Bus 9 - PQ', 13.8000, 1, 1, 1, 40.2800, -86.6700, 1.056386, -14.938500, 0.950000, 1.100000, '{"source_type": "load", "has_shunt": true, "area_name": "Distribution"}'::jsonb),

-- Bus 10: PQ bus, 13.8 kV
('b0000010-0000-0000-0000-000000000010', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 10, 'Bus 10 - PQ', 13.8000, 1, 1, 1, 40.2600, -86.6400, 1.051372, -15.097234, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb),

-- Bus 11: PQ bus, 13.8 kV
('b0000011-0000-0000-0000-000000000011', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 11, 'Bus 11 - PQ', 13.8000, 1, 1, 1, 40.2400, -86.6100, 1.057015, -14.790599, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb),

-- Bus 12: PQ bus, 13.8 kV
('b0000012-0000-0000-0000-000000000012', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 12, 'Bus 12 - PQ', 13.8000, 1, 1, 1, 40.2200, -86.5800, 1.055470, -15.075534, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb),

-- Bus 13: PQ bus, 13.8 kV
('b0000013-0000-0000-0000-000000000013', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 13, 'Bus 13 - PQ', 13.8000, 1, 1, 1, 40.2000, -86.5500, 1.050382, -15.155971, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb),

-- Bus 14: PQ bus, 13.8 kV
('b0000014-0000-0000-0000-000000000014', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 14, 'Bus 14 - PQ', 13.8000, 1, 1, 1, 40.1800, -86.5200, 1.035828, -16.035624, 0.950000, 1.100000, '{"source_type": "load", "area_name": "Distribution"}'::jsonb);

-- =============================================================================
-- STEP 3: Insert Transmission Lines (17 lines)
-- Lines are modeled using the pi-equivalent circuit
-- Parameters: r_pu, x_pu, b_pu (charging), rate_a/b/c_mva
-- =============================================================================

INSERT INTO lines (id, project_id, line_number, name, from_bus, to_bus, length_km, r_pu, x_pu, b_pu, g_pu, rate_a_mva, rate_b_mva, rate_c_mva, status, parallel_lines, line_model, properties) VALUES

-- Line 1: Bus 1-2 (main interconnection between slack and PV)
('l0000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'L1-2 Main', 'b0000001-0000-0000-0000-000000000001', 'b0000002-0000-0000-0000-000000000002', 1.0000, 0.01938000, 0.05917000, 0.05280000, 0.00000000, 200.00, 250.00, 300.00, 1, 1, 'ACSR_795', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 2: Bus 1-5
('l0000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'L1-5', 'b0000001-0000-0000-0000-000000000001', 'b0000005-0000-0000-0000-000000000005', 1.0000, 0.05403000, 0.22304000, 0.04920000, 0.00000000, 150.00, 190.00, 230.00, 1, 1, 'ACSR_300', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 3: Bus 2-3
('l0000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'L2-3', 'b0000002-0000-0000-0000-000000000002', 'b0000003-0000-0000-0000-000000000003', 1.0000, 0.04699000, 0.19797000, 0.04380000, 0.00000000, 150.00, 190.00, 230.00, 1, 1, 'ACSR_300', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 4: Bus 2-4
('l0000004-0000-0000-0000-000000000004', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 4, 'L2-4', 'b0000002-0000-0000-0000-000000000002', 'b0000004-0000-0000-0000-000000000004', 1.0000, 0.05811000, 0.17632000, 0.03400000, 0.00000000, 150.00, 190.00, 230.00, 1, 1, 'ACSR_300', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 5: Bus 2-5
('l0000005-0000-0000-0000-000000000005', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 5, 'L2-5', 'b0000002-0000-0000-0000-000000000002', 'b0000005-0000-0000-0000-000000000005', 1.0000, 0.05695000, 0.17388000, 0.03460000, 0.00000000, 150.00, 190.00, 230.00, 1, 1, 'ACSR_300', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 6: Bus 3-4
('l0000006-0000-0000-0000-000000000006', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 6, 'L3-4', 'b0000003-0000-0000-0000-000000000003', 'b0000004-0000-0000-0000-000000000004', 1.0000, 0.06701000, 0.17103000, 0.01280000, 0.00000000, 150.00, 190.00, 230.00, 1, 1, 'ACSR_300', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 7: Bus 4-5 (parallel path between buses 4 and 5)
('l0000007-0000-0000-0000-000000000007', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 7, 'L4-5', 'b0000004-0000-0000-0000-000000000004', 'b0000005-0000-0000-0000-000000000005', 1.0000, 0.01335000, 0.04211000, 0.00000000, 0.00000000, 200.00, 250.00, 300.00, 1, 1, 'ACSR_795', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 8: Bus 7-8 (line between lower voltage buses)
('l0000008-0000-0000-0000-000000000008', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 8, 'L7-8', 'b0000007-0000-0000-0000-000000000007', 'b0000008-0000-0000-0000-000000000008', 1.0000, 0.00000000, 0.17615000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'REACTOR_EQ', '{"original_data": "IEEE 14 standard", "note": "Equivalent reactor"}'::jsonb),

-- Line 9: Bus 9-10
('l0000009-0000-0000-0000-000000000009', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 9, 'L9-10', 'b0000009-0000-0000-0000-000000000009', 'b0000010-0000-0000-0000-000000000010', 1.0000, 0.00000000, 0.11001000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 10: Bus 9-14
('l0000010-0000-0000-0000-000000000010', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 10, 'L9-14', 'b0000009-0000-0000-0000-000000000009', 'b0000014-0000-0000-0000-000000000014', 1.0000, 0.03181000, 0.08450000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 11: Bus 10-11
('l0000011-0000-0000-0000-000000000011', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 11, 'L10-11', 'b0000010-0000-0000-0000-000000000010', 'b0000011-0000-0000-0000-000000000011', 1.0000, 0.00000000, 0.19211000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 12: Bus 12-13
('l0000012-0000-0000-0000-000000000012', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 12, 'L12-13', 'b0000012-0000-0000-0000-000000000012', 'b0000013-0000-0000-0000-000000000013', 1.0000, 0.00000000, 0.14027000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 13: Bus 13-14
('l0000013-0000-0000-0000-000000000013', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 13, 'L13-14', 'b0000013-0000-0000-0000-000000000013', 'b0000014-0000-0000-0000-000000000014', 1.0000, 0.00000000, 0.25581000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 14: Bus 6-11
('l0000014-0000-0000-0000-000000000014', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 14, 'L6-11', 'b0000006-0000-0000-0000-000000000006', 'b0000011-0000-0000-0000-000000000011', 1.0000, 0.00000000, 0.19890000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 15: Bus 6-13
('l0000015-0000-0000-0000-000000000015', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 15, 'L6-13', 'b0000006-0000-0000-0000-000000000006', 'b0000013-0000-0000-0000-000000000013', 1.0000, 0.00000000, 0.13027000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 16: Bus 6-12
('l0000016-0000-0000-0000-000000000016', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 16, 'L6-12', 'b0000006-0000-0000-0000-000000000006', 'b0000012-0000-0000-0000-000000000012', 1.0000, 0.00000000, 0.12315000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Line 17: Bus 7-9
('l0000017-0000-0000-0000-000000000017', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 17, 'L7-9', 'b0000007-0000-0000-0000-000000000007', 'b0000009-0000-0000-0000-000000000009', 1.0000, 0.00000000, 0.11001000, 0.00000000, 0.00000000, 100.00, 130.00, 160.00, 1, 1, 'CABLE', '{"original_data": "IEEE 14 standard"}'::jsonb);

-- =============================================================================
-- STEP 4: Insert Transformers (3 transformers)
-- IEEE 14 has transformers between buses: 4-7, 4-9, 5-6
-- =============================================================================

INSERT INTO transformers (id, project_id, trafo_number, name, hv_bus, lv_bus, tap_nom, tap_min, tap_max, tap_step_pu, vk_percent, vkr_percent, pfe_kw, i0_percent, sn_mva, shift_deg, tap_side, trafo_type, status, properties) VALUES

-- Transformer 1: Bus 4-7 (69kV / 13.8kV step-down)
('t0000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'T4-7 69/13.8kV', 'b0000004-0000-0000-0000-000000000004', 'b0000007-0000-0000-0000-000000000007', 0.978000, 0.900000, 1.100000, 0.006250, 5.975000, 0.000000, 0.00, 0.000000, 200.00, 0.0000, 'hv', 'two_winding', 1, '{"original_data": "IEEE 14 standard", "ratio": "69/13.8kV"}'::jsonb),

-- Transformer 2: Bus 4-9 (69kV / 13.8kV step-down)
('t0000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'T4-9 69/13.8kV', 'b0000004-0000-0000-0000-000000000004', 'b0000009-0000-0000-0000-000000000009', 0.969000, 0.900000, 1.100000, 0.006250, 5.885000, 0.000000, 0.00, 0.000000, 200.00, 0.0000, 'hv', 'two_winding', 1, '{"original_data": "IEEE 14 standard", "ratio": "69/13.8kV"}'::jsonb),

-- Transformer 3: Bus 5-6 (69kV / 13.8kV step-down)
('t0000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'T5-6 69/13.8kV', 'b0000005-0000-0000-0000-000000000005', 'b0000006-0000-0000-0000-000000000006', 0.932000, 0.900000, 1.100000, 0.006250, 12.508500, 0.000000, 0.00, 0.000000, 200.00, 0.0000, 'hv', 'two_winding', 1, '{"original_data": "IEEE 14 standard", "ratio": "69/13.8kV"}'::jsonb);

-- =============================================================================
-- STEP 5: Insert Generators (5 generators)
-- =============================================================================

INSERT INTO generators (id, project_id, gen_number, bus_id, name, p_mw, q_mvar, q_min_mvar, q_max_mvar, vm_pu, sn_mva, pg_min_mw, pg_max_mw, gen_type, model, inertia_h, damping_d, xd_pu, xq_pu, xdp_pu, xqp_pu, xdpp_pu, xqpp_pu, tdp_sec, tqp_sec, tdpp_sec, tqpp_sec, status, properties) VALUES

-- Generator 1: Bus 1 (Slack) - Large steam unit
('g0000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'b0000001-0000-0000-0000-000000000001', 'Gen 1 - Steam (Slack)', 232.4, -16.9, -9999.00, 9999.00, 1.060000, 300.00, 0.0, 332.4, 'thermal', 'GENROU', 5.1480, 2.0000, 1.80000000, 1.72000000, 0.30000000, 0.65000000, 0.23000000, 0.23000000, 5.9000, 0.5000, 0.0540, 0.0540, 1, '{"original_data": "IEEE 14 standard", "fuel": "coal", "unit_type": "steam"}'::jsonb),

-- Generator 2: Bus 2 (PV) - Steam unit
('g0000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'b0000002-0000-0000-0000-000000000002', 'Gen 2 - Steam (PV)', 40.0, 43.5600, -40.00, 50.00, 1.045000, 200.00, 0.0, 140.0, 'thermal', 'GENROU', 5.0640, 2.0000, 1.80000000, 1.72000000, 0.30000000, 0.65000000, 0.23000000, 0.23000000, 5.9000, 0.5000, 0.0540, 0.0540, 1, '{"original_data": "IEEE 14 standard", "fuel": "coal", "unit_type": "steam"}'::jsonb),

-- Generator 3: Bus 3 (PV) - Steam unit
('g0000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'b0000003-0000-0000-0000-000000000003', 'Gen 3 - Steam (PV)', 0.0, 25.0750, 0.00, 40.00, 1.010000, 150.00, 0.0, 100.0, 'thermal', 'GENROU', 4.7690, 2.0000, 1.80000000, 1.72000000, 0.30000000, 0.65000000, 0.23000000, 0.23000000, 5.9000, 0.5000, 0.0540, 0.0540, 1, '{"original_data": "IEEE 14 standard", "fuel": "coal", "unit_type": "steam"}'::jsonb),

-- Generator 4: Bus 6 (PV) - Hydro unit (synchronous condenser)
('g0000004-0000-0000-0000-000000000004', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 4, 'b0000006-0000-0000-0000-000000000006', 'Gen 6 - Hydro (PV)', 0.0, 12.7300, -6.00, 24.00, 1.070000, 100.00, 0.0, 100.0, 'hydro', 'GENROU', 4.1600, 2.0000, 1.80000000, 1.72000000, 0.30000000, 0.65000000, 0.23000000, 0.23000000, 5.9000, 0.5000, 0.0540, 0.0540, 1, '{"original_data": "IEEE 14 standard", "note": "Synchronous condenser at Bus 6"}'::jsonb),

-- Generator 5: Bus 8 (PV) - Thermal/synchronous condenser
('g0000005-0000-0000-0000-000000000005', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 5, 'b0000008-0000-0000-0000-000000000008', 'Gen 8 - Condenser (PV)', 0.0, 17.6230, -6.00, 24.00, 1.090000, 100.00, 0.0, 100.0, 'thermal', 'GENROU', 4.1600, 2.0000, 1.80000000, 1.72000000, 0.30000000, 0.65000000, 0.23000000, 0.23000000, 5.9000, 0.5000, 0.0540, 0.0540, 1, '{"original_data": "IEEE 14 standard", "note": "Synchronous condenser at Bus 8"}'::jsonb);

-- =============================================================================
-- STEP 6: Insert Loads (11 loads)
-- =============================================================================

INSERT INTO loads (id, project_id, load_number, bus_id, name, p_mw, q_mvar, scaling_p, scaling_q, load_model, zip_pz, zip_pi, profile_id, status, properties) VALUES

-- Load 1: Bus 2
('ld000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'b0000002-0000-0000-0000-000000000002', 'Load Bus 2', 21.7000, 12.7000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 2: Bus 3
('ld000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'b0000003-0000-0000-0000-000000000003', 'Load Bus 3', 94.2000, 19.0000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 3: Bus 4
('ld000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'b0000004-0000-0000-0000-000000000004', 'Load Bus 4', 47.8000, -3.9000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 4: Bus 5
('ld000004-0000-0000-0000-000000000004', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 4, 'b0000005-0000-0000-0000-000000000005', 'Load Bus 5', 7.6000, 1.6000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 5: Bus 6
('ld000005-0000-0000-0000-000000000005', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 5, 'b0000006-0000-0000-0000-000000000006', 'Load Bus 6', 11.2000, 7.5000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 6: Bus 9
('ld000006-0000-0000-0000-000000000006', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 6, 'b0000009-0000-0000-0000-000000000009', 'Load Bus 9', 29.5000, 16.6000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 7: Bus 10
('ld000007-0000-0000-0000-000000000007', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 7, 'b0000010-0000-0000-0000-000000000010', 'Load Bus 10', 9.0000, 5.8000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 8: Bus 11
('ld000008-0000-0000-0000-000000000008', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 8, 'b0000011-0000-0000-0000-000000000011', 'Load Bus 11', 3.5000, 1.8000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 9: Bus 12
('ld000009-0000-0000-0000-000000000009', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 9, 'b0000012-0000-0000-0000-000000000012', 'Load Bus 12', 6.1000, 1.6000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 10: Bus 13
('ld000010-0000-0000-0000-000000000010', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 10, 'b0000013-0000-0000-0000-000000000013', 'Load Bus 13', 13.5000, 5.8000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb),

-- Load 11: Bus 14
('ld000011-0000-0000-0000-000000000011', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 11, 'b0000014-0000-0000-0000-000000000014', 'Load Bus 14', 14.9000, 5.0000, 1.0000, 1.0000, 'constant_pq', 0.0000, 0.0000, NULL, 1, '{"original_data": "IEEE 14 standard"}'::jsonb);

-- =============================================================================
-- STEP 7: Insert Shunts (1 shunt at Bus 9)
-- Shunt compensation at Bus 9: q_mvar = 19 MVAR (capacitive)
-- In the original IEEE 14 data, Bus 9 has a shunt with b_shunt = 0.19 pu
-- Q_shunt = b_pu * V^2 * S_base = 0.19 * 1.0^2 * 100 = 19 MVAR (capacitive, positive)
-- =============================================================================

INSERT INTO shunts (id, project_id, shunt_number, bus_id, name, q_mvar, vn_kv, steps, q_min_mvar, q_max_mvar, shunt_type, status, properties) VALUES
('sh000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'b0000009-0000-0000-0000-000000000009', 'Shunt Bus 9 - Capacitor', 19.0000, 13.8000, 1, 0.0000, 19.0000, 'fixed', 1, '{"b_pu": 0.19, "original_data": "IEEE 14 standard", "note": "Fixed capacitor bank providing 19 MVAR compensation"}'::jsonb);

-- =============================================================================
-- STEP 8: Insert Switches (circuit breakers for all lines and transformers)
-- Each branch gets a circuit breaker at the from_bus end
-- =============================================================================

INSERT INTO switches (id, project_id, switch_number, bus_from, bus_to, name, switch_type, state, rated_current_a, rated_voltage_kv, breaking_capacity_ka, properties) VALUES

-- Breakers for lines
('sw000001-0000-0000-0000-000000000001', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 1, 'b0000001-0000-0000-0000-000000000001', 'b0000002-0000-0000-0000-000000000002', 'CB Line 1-2', 'breaker', 1, 2000.00, 69.0000, 40.00, '{"line_id": "l0000001-0000-0000-0000-000000000001"}'::jsonb),
('sw000002-0000-0000-0000-000000000002', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 2, 'b0000001-0000-0000-0000-000000000001', 'b0000005-0000-0000-0000-000000000005', 'CB Line 1-5', 'breaker', 1, 1600.00, 69.0000, 40.00, '{"line_id": "l0000002-0000-0000-0000-000000000002"}'::jsonb),
('sw000003-0000-0000-0000-000000000003', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 3, 'b0000002-0000-0000-0000-000000000002', 'b0000003-0000-0000-0000-000000000003', 'CB Line 2-3', 'breaker', 1, 1600.00, 69.0000, 40.00, '{"line_id": "l0000003-0000-0000-0000-000000000003"}'::jsonb),
('sw000004-0000-0000-0000-000000000004', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 4, 'b0000002-0000-0000-0000-000000000002', 'b0000004-0000-0000-0000-000000000004', 'CB Line 2-4', 'breaker', 1, 1600.00, 69.0000, 40.00, '{"line_id": "l0000004-0000-0000-0000-000000000004"}'::jsonb),
('sw000005-0000-0000-0000-000000000005', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 5, 'b0000002-0000-0000-0000-000000000002', 'b0000005-0000-0000-0000-000000000005', 'CB Line 2-5', 'breaker', 1, 1600.00, 69.0000, 40.00, '{"line_id": "l0000005-0000-0000-0000-000000000005"}'::jsonb),
('sw000006-0000-0000-0000-000000000006', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 6, 'b0000003-0000-0000-0000-000000000003', 'b0000004-0000-0000-0000-000000000004', 'CB Line 3-4', 'breaker', 1, 1600.00, 69.0000, 40.00, '{"line_id": "l0000006-0000-0000-0000-000000000006"}'::jsonb),
('sw000007-0000-0000-0000-000000000007', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 7, 'b0000004-0000-0000-0000-000000000004', 'b0000005-0000-0000-0000-000000000005', 'CB Line 4-5', 'breaker', 1, 2000.00, 69.0000, 40.00, '{"line_id": "l0000007-0000-0000-0000-000000000007"}'::jsonb),
('sw000008-0000-0000-0000-000000000008', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 8, 'b0000007-0000-0000-0000-000000000007', 'b0000008-0000-0000-0000-000000000008', 'CB Line 7-8', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000008-0000-0000-0000-000000000008"}'::jsonb),
('sw000009-0000-0000-0000-000000000009', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 9, 'b0000009-0000-0000-0000-000000000009', 'b0000010-0000-0000-0000-000000000010', 'CB Line 9-10', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000009-0000-0000-0000-000000000009"}'::jsonb),
('sw000010-0000-0000-0000-000000000010', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 10, 'b0000009-0000-0000-0000-000000000009', 'b0000014-0000-0000-0000-000000000014', 'CB Line 9-14', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000010-0000-0000-0000-000000000010"}'::jsonb),
('sw000011-0000-0000-0000-000000000011', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 11, 'b0000010-0000-0000-0000-000000000010', 'b0000011-0000-0000-0000-000000000011', 'CB Line 10-11', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000011-0000-0000-0000-000000000011"}'::jsonb),
('sw000012-0000-0000-0000-000000000012', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 12, 'b0000012-0000-0000-0000-000000000012', 'b0000013-0000-0000-0000-000000000013', 'CB Line 12-13', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000012-0000-0000-0000-000000000012"}'::jsonb),
('sw000013-0000-0000-0000-000000000013', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 13, 'b0000013-0000-0000-0000-000000000013', 'b0000014-0000-0000-0000-000000000014', 'CB Line 13-14', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000013-0000-0000-0000-000000000013"}'::jsonb),
('sw000014-0000-0000-0000-000000000014', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 14, 'b0000006-0000-0000-0000-000000000006', 'b0000011-0000-0000-0000-000000000011', 'CB Line 6-11', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000014-0000-0000-0000-000000000014"}'::jsonb),
('sw000015-0000-0000-0000-000000000015', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 15, 'b0000006-0000-0000-0000-000000000006', 'b0000013-0000-0000-0000-000000000013', 'CB Line 6-13', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000015-0000-0000-0000-000000000015"}'::jsonb),
('sw000016-0000-0000-0000-000000000016', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 16, 'b0000006-0000-0000-0000-000000000006', 'b0000012-0000-0000-0000-000000000012', 'CB Line 6-12', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000016-0000-0000-0000-000000000016"}'::jsonb),
('sw000017-0000-0000-0000-000000000017', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 17, 'b0000007-0000-0000-0000-000000000007', 'b0000009-0000-0000-0000-000000000009', 'CB Line 7-9', 'breaker', 1, 800.00, 13.8000, 25.00, '{"line_id": "l0000017-0000-0000-0000-000000000017"}'::jsonb),

-- Breakers for transformers
('sw000018-0000-0000-0000-000000000018', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 18, 'b0000004-0000-0000-0000-000000000004', 'b0000007-0000-0000-0000-000000000007', 'CB Trafo 4-7', 'breaker', 1, 2000.00, 69.0000, 40.00, '{"transformer_id": "t0000001-0000-0000-0000-000000000001"}'::jsonb),
('sw000019-0000-0000-0000-000000000019', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 19, 'b0000004-0000-0000-0000-000000000004', 'b0000009-0000-0000-0000-000000000009', 'CB Trafo 4-9', 'breaker', 1, 2000.00, 69.0000, 40.00, '{"transformer_id": "t0000002-0000-0000-0000-000000000002"}'::jsonb),
('sw000020-0000-0000-0000-000000000020', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 20, 'b0000005-0000-0000-0000-000000000005', 'b0000006-0000-0000-0000-000000000006', 'CB Trafo 5-6', 'breaker', 1, 2000.00, 69.0000, 40.00, '{"transformer_id": "t0000003-0000-0000-0000-000000000003"}'::jsonb);

-- =============================================================================
-- STEP 9: Verify Seed Data
-- =============================================================================

-- Verification query to confirm all data was inserted correctly
DO $$
DECLARE
    v_project_id UUID;
    v_bus_count INTEGER;
    v_line_count INTEGER;
    v_trafo_count INTEGER;
    v_gen_count INTEGER;
    v_load_count INTEGER;
    v_shunt_count INTEGER;
    v_switch_count INTEGER;
    v_total_gen_mw DECIMAL;
    v_total_load_mw DECIMAL;
BEGIN
    SELECT id INTO v_project_id FROM projects WHERE name = 'IEEE 14 Bus Test System';
    
    SELECT COUNT(*) INTO v_bus_count FROM buses WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_line_count FROM lines WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_trafo_count FROM transformers WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_gen_count FROM generators WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_load_count FROM loads WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_shunt_count FROM shunts WHERE project_id = v_project_id;
    SELECT COUNT(*) INTO v_switch_count FROM switches WHERE project_id = v_project_id;
    SELECT COALESCE(SUM(p_mw), 0) INTO v_total_gen_mw FROM generators WHERE project_id = v_project_id AND status = 1;
    SELECT COALESCE(SUM(p_mw), 0) INTO v_total_load_mw FROM loads WHERE project_id = v_project_id AND status = 1;
    
    RAISE NOTICE '========================================';
    RAISE NOTICE 'IEEE 14 Bus Seed Data Verification';
    RAISE NOTICE '========================================';
    RAISE NOTICE 'Project ID: %', v_project_id;
    RAISE NOTICE 'Buses: % (expected: 14)', v_bus_count;
    RAISE NOTICE 'Lines: % (expected: 17)', v_line_count;
    RAISE NOTICE 'Transformers: % (expected: 3)', v_trafo_count;
    RAISE NOTICE 'Generators: % (expected: 5)', v_gen_count;
    RAISE NOTICE 'Loads: % (expected: 11)', v_load_count;
    RAISE NOTICE 'Shunts: % (expected: 1)', v_shunt_count;
    RAISE NOTICE 'Switches: % (expected: 20)', v_switch_count;
    RAISE NOTICE 'Total Generation: % MW (expected: ~272.4)', v_total_gen_mw;
    RAISE NOTICE 'Total Load: % MW (expected: ~259.1)', v_total_load_mw;
    RAISE NOTICE '========================================';
    
    -- Validate counts
    IF v_bus_count != 14 THEN
        RAISE WARNING 'Bus count mismatch: got %, expected 14', v_bus_count;
    END IF;
    IF v_line_count != 17 THEN
        RAISE WARNING 'Line count mismatch: got %, expected 17', v_line_count;
    END IF;
    IF v_trafo_count != 3 THEN
        RAISE WARNING 'Transformer count mismatch: got %, expected 3', v_trafo_count;
    END IF;
    IF v_gen_count != 5 THEN
        RAISE WARNING 'Generator count mismatch: got %, expected 5', v_gen_count;
    END IF;
    IF v_load_count != 11 THEN
        RAISE WARNING 'Load count mismatch: got %, expected 11', v_load_count;
    END IF;
    IF v_shunt_count != 1 THEN
        RAISE WARNING 'Shunt count mismatch: got %, expected 1', v_shunt_count;
    END IF;
    IF v_switch_count != 20 THEN
        RAISE WARNING 'Switch count mismatch: got %, expected 20', v_switch_count;
    END IF;
    
    IF v_bus_count = 14 AND v_line_count = 17 AND v_trafo_count = 3 AND 
       v_gen_count = 5 AND v_load_count = 11 AND v_shunt_count = 1 AND v_switch_count = 20 THEN
        RAISE NOTICE 'IEEE 14 Bus seed data: ALL CHECKS PASSED';
    ELSE
        RAISE WARNING 'IEEE 14 Bus seed data: SOME CHECKS FAILED';
    END IF;
END $$;

-- =============================================================================
-- END OF IEEE 14 BUS SEED DATA
-- =============================================================================