-- =============================================================================
-- POWSYS365 - System Summary Queries
-- Description: High-level system overview and summary queries
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Q1: Complete system summary using the materialized view
-- Shows generation, load, losses, and equipment counts per project
-- ---------------------------------------------------------------------------
-- name: get_system_overview
SELECT 
    project_id,
    project_name,
    base_mva,
    frequency,
    standard,
    project_status,
    total_buses,
    total_lines,
    total_transformers,
    total_generators,
    total_loads,
    total_shunts,
    total_switches,
    total_gen_mw,
    total_gen_mvar,
    total_load_mw,
    total_load_mvar,
    gen_load_balance_mw,
    slack_buses,
    pv_buses,
    pq_buses,
    project_created,
    project_updated
FROM v_system_summary
WHERE project_id = :project_id;

-- ---------------------------------------------------------------------------
-- Q2: Equipment inventory summary
-- Count of all equipment types per project with status breakdown
-- ---------------------------------------------------------------------------
-- name: get_equipment_inventory
SELECT 
    'Buses' AS equipment_type,
    COUNT(*) AS total,
    COUNT(*) FILTER (WHERE status = 1) AS active,
    COUNT(*) FILTER (WHERE status = 0) AS inactive,
    COUNT(*) FILTER (WHERE bus_type = 3) AS slack,
    COUNT(*) FILTER (WHERE bus_type = 2) AS pv,
    COUNT(*) FILTER (WHERE bus_type = 1) AS pq
FROM buses WHERE project_id = :project_id
UNION ALL
SELECT 
    'Lines',
    COUNT(*),
    COUNT(*) FILTER (WHERE status = 1),
    COUNT(*) FILTER (WHERE status = 0),
    NULL, NULL, NULL
FROM lines WHERE project_id = :project_id
UNION ALL
SELECT 
    'Transformers',
    COUNT(*),
    COUNT(*) FILTER (WHERE status = 1),
    COUNT(*) FILTER (WHERE status = 0),
    NULL, NULL, NULL
FROM transformers WHERE project_id = :project_id
UNION ALL
SELECT 
    'Generators',
    COUNT(*),
    COUNT(*) FILTER (WHERE status = 1),
    COUNT(*) FILTER (WHERE status = 0),
    NULL, NULL, NULL
FROM generators WHERE project_id = :project_id
UNION ALL
SELECT 
    'Loads',
    COUNT(*),
    COUNT(*) FILTER (WHERE status = 1),
    COUNT(*) FILTER (WHERE status = 0),
    NULL, NULL, NULL
FROM loads WHERE project_id = :project_id
UNION ALL
SELECT 
    'Shunts',
    COUNT(*),
    COUNT(*) FILTER (WHERE status = 1),
    COUNT(*) FILTER (WHERE status = 0),
    NULL, NULL, NULL
FROM shunts WHERE project_id = :project_id
UNION ALL
SELECT 
    'Switches',
    COUNT(*),
    COUNT(*) FILTER (WHERE state = 1),
    COUNT(*) FILTER (WHERE state = 0),
    NULL, NULL, NULL
FROM switches WHERE project_id = :project_id;

-- ---------------------------------------------------------------------------
-- Q3: Generation mix breakdown
-- Shows generation capacity by type (thermal, hydro, wind, solar, etc.)
-- ---------------------------------------------------------------------------
-- name: get_generation_mix
SELECT 
    g.gen_type,
    COUNT(*) AS num_units,
    COALESCE(SUM(g.p_mw), 0) AS total_p_mw,
    COALESCE(SUM(g.q_mvar), 0) AS total_q_mvar,
    COALESCE(SUM(g.sn_mva), 0) AS total_capacity_mva,
    COALESCE(SUM(g.pg_max_mw), 0) AS total_max_capacity_mw,
    ROUND(AVG(g.vm_pu)::NUMERIC, 4) AS avg_voltage_setpoint,
    COALESCE(SUM(g.p_mw), 0) / NULLIF(SUM(SUM(g.p_mw)) OVER (), 0) * 100 AS percent_of_total
FROM generators g
WHERE g.project_id = :project_id
  AND g.status = 1
GROUP BY g.gen_type
ORDER BY total_p_mw DESC;

-- ---------------------------------------------------------------------------
-- Q4: Voltage level summary
-- Groups buses by nominal voltage and shows statistics
-- ---------------------------------------------------------------------------
-- name: get_voltage_level_summary
SELECT 
    vn_kv AS nominal_voltage_kv,
    COUNT(*) AS num_buses,
    COUNT(*) FILTER (WHERE bus_type = 3) AS num_slack,
    COUNT(*) FILTER (WHERE bus_type = 2) AS num_pv,
    COUNT(*) FILTER (WHERE bus_type = 1) AS num_pq,
    ROUND(AVG(vm_pu)::NUMERIC, 6) AS avg_vm_pu,
    MIN(vm_pu) AS min_vm_pu,
    MAX(vm_pu) AS max_vm_pu,
    COALESCE(SUM(ld.p_mw), 0) AS total_load_mw,
    COALESCE(SUM(g.p_mw), 0) AS total_gen_mw
FROM buses b
LEFT JOIN generators g ON g.bus_id = b.id AND g.status = 1
LEFT JOIN loads ld ON ld.bus_id = b.id AND ld.status = 1
WHERE b.project_id = :project_id
GROUP BY vn_kv
ORDER BY vn_kv DESC;

-- ---------------------------------------------------------------------------
-- Q5: Area and zone summary
-- Shows system statistics broken down by control area and zone
-- ---------------------------------------------------------------------------
-- name: get_area_zone_summary
SELECT 
    b.area,
    b.zone,
    COUNT(*) AS num_buses,
    COALESCE(SUM(g.p_mw), 0) AS total_gen_mw,
    COALESCE(SUM(g.q_mvar), 0) AS total_gen_mvar,
    COALESCE(SUM(ld.p_mw), 0) AS total_load_mw,
    COALESCE(SUM(ld.q_mvar), 0) AS total_load_mvar,
    COALESCE(SUM(g.p_mw), 0) - COALESCE(SUM(ld.p_mw), 0) AS net_mw,
    COALESCE(SUM(g.q_mvar), 0) - COALESCE(SUM(ld.q_mvar), 0) AS net_mvar,
    ROUND(AVG(b.vm_pu)::NUMERIC, 6) AS avg_voltage_pu,
    MIN(b.vm_pu) AS min_voltage_pu,
    MAX(b.vm_pu) AS max_voltage_pu
FROM buses b
LEFT JOIN generators g ON g.bus_id = b.id AND g.status = 1
LEFT JOIN loads ld ON ld.bus_id = b.id AND ld.status = 1
WHERE b.project_id = :project_id
GROUP BY b.area, b.zone
ORDER BY b.area, b.zone;

-- ---------------------------------------------------------------------------
-- Q6: Line loading summary
-- Shows loading statistics for all lines in the project
-- ---------------------------------------------------------------------------
-- name: get_line_loading_summary
SELECT 
    l.line_number,
    COALESCE(l.name, 'Line ' || l.line_number) AS line_name,
    fb.bus_number AS from_bus,
    tb.bus_number AS to_bus,
    l.length_km,
    l.rate_a_mva,
    l.rate_b_mva,
    l.status,
    fb.vn_kv AS from_voltage_kv,
    tb.vn_kv AS to_voltage_kv,
    CASE 
        WHEN fb.vn_kv = tb.vn_kv THEN 'Same voltage'
        ELSE 'Different voltage'
    END AS voltage_note
FROM lines l
JOIN buses fb ON fb.id = l.from_bus
JOIN buses tb ON tb.id = l.to_bus
WHERE l.project_id = :project_id
ORDER BY l.line_number;

-- ---------------------------------------------------------------------------
-- Q7: Transformer summary
-- Shows all transformers with tap positions and ratings
-- ---------------------------------------------------------------------------
-- name: get_transformer_summary
SELECT 
    t.trafo_number,
    COALESCE(t.name, 'Trafo ' || t.trafo_number) AS trafo_name,
    hb.bus_number AS hv_bus_number,
    hb.vn_kv AS hv_voltage_kv,
    lb.bus_number AS lv_bus_number,
    lb.vn_kv AS lv_voltage_kv,
    t.tap_nom AS current_tap,
    t.tap_min,
    t.tap_max,
    t.tap_step_pu,
    t.sn_mva,
    t.vk_percent,
    t.shift_deg,
    t.tap_side,
    t.status,
    hb.vn_kv / lb.vn_kv AS nominal_ratio
FROM transformers t
JOIN buses hb ON hb.id = t.hv_bus
JOIN buses lb ON lb.id = t.lv_bus
WHERE t.project_id = :project_id
ORDER BY t.trafo_number;

-- ---------------------------------------------------------------------------
-- Q8: Load summary by bus
-- Shows all loads organized by connected bus
-- ---------------------------------------------------------------------------
-- name: get_load_summary
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    COUNT(l.id) AS num_loads,
    COALESCE(SUM(l.p_mw), 0) AS total_p_mw,
    COALESCE(SUM(l.q_mvar), 0) AS total_q_mvar,
    COALESCE(SUM(l.p_mw * l.scaling_p), 0) AS scaled_p_mw,
    COALESCE(SUM(l.q_mvar * l.scaling_q), 0) AS scaled_q_mvar,
    ROUND(AVG(l.scaling_p)::NUMERIC, 4) AS avg_scaling_p,
    ROUND(AVG(l.scaling_q)::NUMERIC, 4) AS avg_scaling_q
FROM buses b
LEFT JOIN loads l ON l.bus_id = b.id AND l.status = 1
WHERE b.project_id = :project_id
GROUP BY b.id, b.bus_number, b.name, b.vn_kv
ORDER BY b.bus_number;

-- ---------------------------------------------------------------------------
-- Q9: Shunt compensation summary
-- Shows all shunt devices and total compensation per bus
-- ---------------------------------------------------------------------------
-- name: get_shunt_summary
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    s.shunt_number,
    s.name AS shunt_name,
    s.shunt_type,
    s.q_mvar,
    s.steps,
    s.q_min_mvar,
    s.q_max_mvar,
    s.status,
    CASE 
        WHEN s.q_mvar > 0 THEN 'Capacitive (+Q)'
        WHEN s.q_mvar < 0 THEN 'Inductive (-Q)'
        ELSE 'Neutral'
    END AS compensation_type
FROM shunts s
JOIN buses b ON b.id = s.bus_id
WHERE s.project_id = :project_id
ORDER BY b.bus_number, s.shunt_number;

-- ---------------------------------------------------------------------------
-- Q10: System topology summary
-- Shows network connectivity metrics
-- ---------------------------------------------------------------------------
-- name: get_topology_summary
SELECT 
    p.name AS project_name,
    (SELECT COUNT(*) FROM buses WHERE project_id = :project_id) AS total_buses,
    (SELECT COUNT(*) FROM lines WHERE project_id = :project_id AND status = 1) AS active_lines,
    (SELECT COUNT(*) FROM transformers WHERE project_id = :project_id AND status = 1) AS active_transformers,
    (SELECT COUNT(*) FROM switches WHERE project_id = :project_id AND state = 1) AS closed_switches,
    (SELECT COUNT(DISTINCT from_bus) + COUNT(DISTINCT to_bus) 
     FROM lines WHERE project_id = :project_id AND status = 1) AS connected_buses_lines,
    (SELECT COUNT(*) FROM buses b 
     WHERE b.project_id = :project_id 
       AND NOT EXISTS (SELECT 1 FROM lines l 
                       WHERE (l.from_bus = b.id OR l.to_bus = b.id) 
                         AND l.status = 1)) AS isolated_buses,
    (SELECT ROUND(AVG(length_km)::NUMERIC, 4) 
     FROM lines WHERE project_id = :project_id AND status = 1) AS avg_line_length_km,
    (SELECT ROUND(SUM(length_km)::NUMERIC, 4) 
     FROM lines WHERE project_id = :project_id AND status = 1) AS total_line_length_km,
    (SELECT COUNT(*) 
     FROM buses b 
     WHERE b.project_id = :project_id 
       AND b.latitude IS NOT NULL 
       AND b.longitude IS NOT NULL) AS geolocated_buses
FROM projects p
WHERE p.id = :project_id;

-- =============================================================================
-- END OF SYSTEM SUMMARY QUERIES
-- =============================================================================