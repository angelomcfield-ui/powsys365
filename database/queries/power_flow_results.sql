-- =============================================================================
-- POWSYS365 - Power Flow Results Queries
-- Description: Common queries for power flow (load flow) analysis results
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Q1: Get complete power flow results for a specific run
-- Parameters: project_id, run_id
-- Returns: All bus results with voltage and power injection data
-- ---------------------------------------------------------------------------
-- name: get_pf_results_by_run
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    CASE b.bus_type 
        WHEN 1 THEN 'PQ'
        WHEN 2 THEN 'PV'
        WHEN 3 THEN 'Slack'
    END AS bus_type,
    rpf.vm_pu,
    rpf.va_deg,
    rpf.p_injected_mw,
    rpf.q_injected_mvar,
    rpf.p_gen_mw,
    rpf.q_gen_mvar,
    rpf.p_load_mw,
    rpf.q_load_mvar,
    rpf.violations
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
ORDER BY b.bus_number;

-- ---------------------------------------------------------------------------
-- Q2: Get voltage profile (magnitude vs bus number)
-- Useful for voltage profile plots and identifying weak buses
-- ---------------------------------------------------------------------------
-- name: get_voltage_profile
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rpf.vm_pu,
    b.vmin_pu,
    b.vmax_pu,
    ROUND((rpf.vm_pu * b.vn_kv)::NUMERIC, 4) AS vm_kv,
    ROUND(((rpf.vm_pu - 1.0) * 100)::NUMERIC, 4) AS voltage_deviation_percent,
    CASE 
        WHEN rpf.vm_pu < b.vmin_pu THEN 'UNDERVOLTAGE'
        WHEN rpf.vm_pu > b.vmax_pu THEN 'OVERVOLTAGE'
        ELSE 'WITHIN LIMITS'
    END AS voltage_status
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
ORDER BY b.bus_number;

-- ---------------------------------------------------------------------------
-- Q3: Get generation summary for a power flow run
-- Shows total generation, slack bus generation, and generation by type
-- ---------------------------------------------------------------------------
-- name: get_generation_summary
SELECT 
    CASE b.bus_type 
        WHEN 3 THEN 'Slack Bus'
        WHEN 2 THEN 'PV Bus'
        ELSE 'Other'
    END AS gen_category,
    COUNT(DISTINCT g.id) AS num_generators,
    COALESCE(SUM(rpf.p_gen_mw), 0) AS total_p_gen_mw,
    COALESCE(SUM(rpf.q_gen_mvar), 0) AS total_q_gen_mvar,
    ROUND(AVG(rpf.vm_pu)::NUMERIC, 6) AS avg_voltage_pu,
    MIN(rpf.vm_pu) AS min_voltage_pu,
    MAX(rpf.vm_pu) AS max_voltage_pu
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
LEFT JOIN generators g ON g.bus_id = b.id AND g.status = 1
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
  AND rpf.p_gen_mw > 0
GROUP BY 
    CASE b.bus_type 
        WHEN 3 THEN 'Slack Bus'
        WHEN 2 THEN 'PV Bus'
        ELSE 'Other'
    END
ORDER BY total_p_gen_mw DESC;

-- ---------------------------------------------------------------------------
-- Q4: Get total system losses for a power flow run
-- Uses the get_total_losses() function
-- ---------------------------------------------------------------------------
-- name: get_system_losses
SELECT * FROM get_total_losses(:project_id, :run_id);

-- ---------------------------------------------------------------------------
-- Q5: Get detailed line flow results with loading percentages
-- ---------------------------------------------------------------------------
-- name: get_line_flow_results
SELECT 
    l.line_number,
    COALESCE(l.name, 'Line ' || l.line_number) AS line_name,
    fb.bus_number AS from_bus,
    fb.name AS from_bus_name,
    tb.bus_number AS to_bus,
    tb.name AS to_bus_name,
    l.length_km,
    l.rate_a_mva,
    rpf.p_from_mw,
    rpf.q_from_mvar,
    rpf.p_to_mw,
    rpf.q_to_mvar,
    rpf.s_mva,
    rpf.loading_percent,
    rpf.p_loss_mw,
    rpf.q_loss_mvar,
    rpf.current_ka,
    CASE 
        WHEN rpf.loading_percent > 100 THEN 'OVERLOAD'
        WHEN rpf.loading_percent > 80 THEN 'WARNING'
        ELSE 'NORMAL'
    END AS loading_status
FROM results_power_flow_lines rpf
JOIN lines l ON l.id = rpf.line_id
JOIN buses fb ON fb.id = l.from_bus
JOIN buses tb ON tb.id = l.to_bus
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
ORDER BY rpf.loading_percent DESC;

-- ---------------------------------------------------------------------------
-- Q6: Get overloaded lines (>100% of rate_a)
-- ---------------------------------------------------------------------------
-- name: get_overloaded_lines
SELECT 
    l.line_number,
    l.name AS line_name,
    fb.bus_number AS from_bus,
    tb.bus_number AS to_bus,
    l.rate_a_mva,
    rpf.s_mva,
    rpf.loading_percent,
    rpf.p_loss_mw,
    rpf.current_ka
FROM results_power_flow_lines rpf
JOIN lines l ON l.id = rpf.line_id
JOIN buses fb ON fb.id = l.from_bus
JOIN buses tb ON tb.id = l.to_bus
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
  AND rpf.loading_percent > 100
ORDER BY rpf.loading_percent DESC;

-- ---------------------------------------------------------------------------
-- Q7: Get power balance (generation vs load vs losses)
-- ---------------------------------------------------------------------------
-- name: get_power_balance
SELECT 
    'Generation' AS category,
    COALESCE(SUM(rpf.p_gen_mw), 0) AS p_mw,
    COALESCE(SUM(rpf.q_gen_mvar), 0) AS q_mvar
FROM results_power_flow rpf
WHERE rpf.project_id = :project_id AND rpf.run_id = :run_id
UNION ALL
SELECT 
    'Load' AS category,
    COALESCE(SUM(rpf.p_load_mw), 0) AS p_mw,
    COALESCE(SUM(rpf.q_load_mvar), 0) AS q_mvar
FROM results_power_flow rpf
WHERE rpf.project_id = :project_id AND rpf.run_id = :run_id
UNION ALL
SELECT 
    'Losses' AS category,
    COALESCE(SUM(rpfl.p_loss_mw), 0) AS p_mw,
    COALESCE(SUM(rpfl.q_loss_mvar), 0) AS q_mvar
FROM results_power_flow_lines rpfl
WHERE rpfl.project_id = :project_id AND rpfl.run_id = :run_id
UNION ALL
SELECT 
    'Net Balance' AS category,
    (
        COALESCE((SELECT SUM(rpf2.p_gen_mw) FROM results_power_flow rpf2 
                  WHERE rpf2.project_id = :project_id AND rpf2.run_id = :run_id), 0)
        - COALESCE((SELECT SUM(rpf3.p_load_mw) FROM results_power_flow rpf3 
                    WHERE rpf3.project_id = :project_id AND rpf3.run_id = :run_id), 0)
        - COALESCE((SELECT SUM(rpfl2.p_loss_mw) FROM results_power_flow_lines rpfl2 
                    WHERE rpfl2.project_id = :project_id AND rpfl2.run_id = :run_id), 0)
    ) AS p_mw,
    (
        COALESCE((SELECT SUM(rpf4.q_gen_mvar) FROM results_power_flow rpf4 
                  WHERE rpf4.project_id = :project_id AND rpf4.run_id = :run_id), 0)
        - COALESCE((SELECT SUM(rpf5.q_load_mvar) FROM results_power_flow rpf5 
                    WHERE rpf5.project_id = :project_id AND rpf5.run_id = :run_id), 0)
        - COALESCE((SELECT SUM(rpfl3.q_loss_mvar) FROM results_power_flow_lines rpfl3 
                    WHERE rpfl3.project_id = :project_id AND rpfl3.run_id = :run_id), 0)
    ) AS q_mvar;

-- ---------------------------------------------------------------------------
-- Q8: Get buses with violations
-- ---------------------------------------------------------------------------
-- name: get_bus_violations
SELECT 
    b.bus_number,
    b.name AS bus_name,
    rpf.vm_pu,
    b.vmin_pu,
    b.vmax_pu,
    rpf.violations
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
  AND jsonb_array_length(rpf.violations) > 0
ORDER BY b.bus_number;

-- ---------------------------------------------------------------------------
-- Q9: Get convergence summary for a run
-- Shows key metrics to assess solution quality
-- ---------------------------------------------------------------------------
-- name: get_convergence_summary
SELECT 
    :project_id AS project_id,
    :run_id AS run_id,
    COUNT(DISTINCT b.id) AS total_buses,
    COUNT(DISTINCT CASE WHEN rpf.vm_pu >= b.vmin_pu AND rpf.vm_pu <= b.vmax_pu THEN b.id END) AS buses_within_limits,
    COUNT(DISTINCT CASE WHEN rpf.vm_pu < b.vmin_pu OR rpf.vm_pu > b.vmax_pu THEN b.id END) AS buses_with_violations,
    MIN(rpf.vm_pu) AS min_vm_pu,
    MAX(rpf.vm_pu) AS max_vm_pu,
    AVG(rpf.vm_pu) AS avg_vm_pu,
    MIN(rpf.va_deg) AS min_va_deg,
    MAX(rpf.va_deg) AS max_va_deg,
    COALESCE((SELECT SUM(p_gen_mw) FROM results_power_flow 
              WHERE project_id = :project_id AND run_id = :run_id), 0) AS total_generation_mw,
    COALESCE((SELECT SUM(p_load_mw) FROM results_power_flow 
              WHERE project_id = :project_id AND run_id = :run_id), 0) AS total_load_mw,
    COALESCE((SELECT SUM(p_loss_mw) FROM results_power_flow_lines 
              WHERE project_id = :project_id AND run_id = :run_id), 0) AS total_losses_mw,
    ROUND(
        (COALESCE((SELECT SUM(p_loss_mw) FROM results_power_flow_lines 
                   WHERE project_id = :project_id AND run_id = :run_id), 0)
         / NULLIF(COALESCE((SELECT SUM(p_gen_mw) FROM results_power_flow 
                            WHERE project_id = :project_id AND run_id = :run_id), 0), 0) * 100)::NUMERIC, 
        4
    ) AS loss_percent
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
WHERE rpf.project_id = :project_id AND rpf.run_id = :run_id;

-- ---------------------------------------------------------------------------
-- Q10: Get reactive power reserve margins at PV buses
-- ---------------------------------------------------------------------------
-- name: get_reactive_reserve
SELECT 
    b.bus_number,
    b.name AS bus_name,
    g.gen_type,
    g.p_mw AS scheduled_p_mw,
    g.q_mw AS scheduled_q_mvar,
    g.q_min_mvar,
    g.q_max_mvar,
    rpf.q_gen_mvar AS actual_q_gen_mvar,
    (g.q_max_mvar - rpf.q_gen_mvar) AS q_reserve_up_mvar,
    (rpf.q_gen_mvar - g.q_min_mvar) AS q_reserve_down_mvar,
    ROUND(
        (CASE WHEN g.q_max_mvar != g.q_min_mvar 
              THEN (rpf.q_gen_mvar - g.q_min_mvar) / (g.q_max_mvar - g.q_min_mvar) * 100
              ELSE 0 
         END)::NUMERIC, 4
    ) AS q_utilization_percent
FROM results_power_flow rpf
JOIN buses b ON b.id = rpf.bus_id
JOIN generators g ON g.bus_id = b.id AND g.status = 1
WHERE rpf.project_id = :project_id
  AND rpf.run_id = :run_id
  AND b.bus_type = 2  -- PV buses only
ORDER BY b.bus_number;

-- =============================================================================
-- END OF POWER FLOW RESULTS QUERIES
-- =============================================================================