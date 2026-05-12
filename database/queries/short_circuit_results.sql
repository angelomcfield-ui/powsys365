-- =============================================================================
-- POWSYS365 - Short Circuit Results Queries
-- Description: Queries for short circuit (fault) analysis results
-- Supports IEC 60909 and IEEE C37 calculation standards
-- License: 1A2B-3C4D-5E6F-7G8H by XNOX L.L.C
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Q1: Get complete short circuit results for a specific run
-- Parameters: project_id, run_id
-- ---------------------------------------------------------------------------
-- name: get_sc_results_by_run
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.fault_type,
    rsc.ik_ka,
    rsc.ip_ka,
    rsc.ib_ka,
    rsc.sk_mva,
    rsc.rx_ratio,
    rsc.kappa,
    rsc.z1_pu,
    rsc.z0_pu,
    rsc.z2_pu,
    rsc.contributions
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
ORDER BY b.bus_number, rsc.fault_type;

-- ---------------------------------------------------------------------------
-- Q2: Get maximum fault current summary per bus (all fault types)
-- Shows the worst-case fault scenario for each bus
-- ---------------------------------------------------------------------------
-- name: get_max_fault_currents
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    MAX(rsc.ik_ka) AS max_ik_ka,
    MAX(rsc.ip_ka) AS max_ip_ka,
    MAX(rsc.sk_mva) AS max_sk_mva,
    (SELECT rsc2.fault_type FROM results_short_circuit rsc2 
     WHERE rsc2.bus_id = b.id AND rsc2.run_id = :run_id 
     ORDER BY rsc2.ik_ka DESC LIMIT 1) AS worst_fault_type,
    (SELECT rsc3.ik_ka FROM results_short_circuit rsc3 
     WHERE rsc3.bus_id = b.id AND rsc3.run_id = :run_id 
     ORDER BY rsc3.ik_ka DESC LIMIT 1) AS worst_ik_ka
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
GROUP BY b.id, b.bus_number, b.name, b.vn_kv
ORDER BY max_ik_ka DESC;

-- ---------------------------------------------------------------------------
-- Q3: Get three-phase fault results (most common study)
-- Used for breaker sizing and relay coordination
-- ---------------------------------------------------------------------------
-- name: get_three_phase_faults
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.ik_ka AS steady_state_current_ka,
    rsc.ip_ka AS peak_current_ka,
    rsc.ib_ka AS breaking_current_ka,
    rsc.sk_mva AS short_circuit_power,
    rsc.rx_ratio,
    rsc.kappa AS peak_factor,
    rsc.z1_pu,
    ROUND((rsc.ik_ka * b.vn_kv * SQRT(3))::NUMERIC, 4) AS three_phase_fault_mva,
    rsc.contributions
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
  AND rsc.fault_type = 'three_phase'
ORDER BY rsc.ik_ka DESC;

-- ---------------------------------------------------------------------------
-- Q4: Get breaker sizing recommendations
-- Calculates required interrupting rating for each bus
-- ---------------------------------------------------------------------------
-- name: get_breaker_sizing
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv AS rated_voltage_kv,
    rsc.ik_ka,
    rsc.ip_ka,
    CEIL(rsc.ik_ka::NUMERIC / 5.0) * 5.0 AS recommended_breaking_current_ka,
    CEIL(rsc.ik_ka::NUMERIC / 5.0) * 5.0 * 1.25 AS rated_breaking_capacity_ka,
    rsc.sk_mva,
    CEIL(rsc.sk_mva::NUMERIC / 100.0) * 100.0 AS recommended_breaker_mva,
    CASE 
        WHEN rsc.ik_ka <= 10 THEN 'Low capacity breaker (<10kA)'
        WHEN rsc.ik_ka <= 25 THEN 'Medium capacity breaker (10-25kA)'
        WHEN rsc.ik_ka <= 50 THEN 'High capacity breaker (25-50kA)'
        ELSE 'Ultra-high capacity breaker (>50kA)'
    END AS breaker_category,
    rsc.fault_type
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
  AND rsc.fault_type = 'three_phase'
ORDER BY rsc.ik_ka DESC;

-- ---------------------------------------------------------------------------
-- Q5: Get single-line-to-ground fault results
-- Important for ground relay settings and grounding system design
-- ---------------------------------------------------------------------------
-- name: get_single_phase_ground_faults
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.ik_ka,
    rsc.ip_ka,
    rsc.sk_mva,
    rsc.z1_pu,
    rsc.z0_pu,
    rsc.z2_pu,
    ROUND((rsc.z0_pu / NULLIF(rsc.z1_pu, 0))::NUMERIC, 6) AS z0_z1_ratio,
    rsc.kappa,
    rsc.contributions
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
  AND rsc.fault_type = 'single_phase'
ORDER BY rsc.ik_ka DESC;

-- ---------------------------------------------------------------------------
-- Q6: Get fault current contribution analysis
-- Parses the JSON contributions field to show contributions by source
-- ---------------------------------------------------------------------------
-- name: get_fault_contributions
SELECT 
    b.bus_number,
    b.name AS bus_name,
    rsc.fault_type,
    rsc.ik_ka AS total_ik_ka,
    jsonb_pretty(rsc.contributions) AS contributions_formatted,
    rsc.contributions->>'generators' AS gen_contributions,
    rsc.contributions->>'lines' AS line_contributions,
    rsc.contributions->>'transformers' AS trafo_contributions,
    rsc.contributions->>'motors' AS motor_contributions
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
ORDER BY rsc.ik_ka DESC;

-- ---------------------------------------------------------------------------
-- Q7: Get R/X ratio analysis for all faulted buses
-- Important for DC component decay and peak current calculation
-- ---------------------------------------------------------------------------
-- name: get_rx_ratio_analysis
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.fault_type,
    rsc.ik_ka,
    rsc.rx_ratio,
    rsc.kappa,
    CASE 
        WHEN rsc.rx_ratio < 0.1 THEN 'Predominantly reactive network'
        WHEN rsc.rx_ratio < 0.3 THEN 'Typical transmission network'
        WHEN rsc.rx_ratio < 1.0 THEN 'Resistive network'
        ELSE 'Highly resistive network'
    END AS network_characterization,
    -- DC time constant: tau = L/R = X/(omega*R) = (X/R)/(2*pi*f)
    ROUND((rsc.rx_ratio / (2 * pi() * 60.0))::NUMERIC, 6) AS dc_time_constant_60hz_sec,
    ROUND((rsc.rx_ratio / (2 * pi() * 50.0))::NUMERIC, 6) AS dc_time_constant_50hz_sec
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
  AND rsc.fault_type = 'three_phase'
ORDER BY rsc.rx_ratio DESC;

-- ---------------------------------------------------------------------------
-- Q8: Get symmetrical components summary
-- Shows positive, negative, and zero sequence impedances
-- ---------------------------------------------------------------------------
-- name: get_symmetrical_components
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.z1_pu AS z1_positive_seq,
    rsc.z2_pu AS z2_negative_seq,
    rsc.z0_pu AS z0_zero_seq,
    ROUND(((rsc.z1_pu + rsc.z2_pu + rsc.z0_pu) / 3.0)::NUMERIC, 10) AS z_avg_pu,
    ROUND((rsc.z0_pu / NULLIF(rsc.z1_pu, 0))::NUMERIC, 4) AS z0_z1_ratio,
    ROUND((rsc.z2_pu / NULLIF(rsc.z1_pu, 0))::NUMERIC, 4) AS z2_z1_ratio,
    CASE 
        WHEN rsc.z0_pu > rsc.z1_pu * 3 THEN 'Solidly grounded system (Z0 >> Z1)'
        WHEN rsc.z0_pu > rsc.z1_pu THEN 'Effectively grounded system'
        WHEN rsc.z0_pu > rsc.z1_pu * 0.5 THEN 'Low impedance grounded'
        ELSE 'Ungrounded or resonant grounded'
    END AS grounding_characteristic
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
  AND rsc.fault_type = 'three_phase'
ORDER BY b.bus_number;

-- ---------------------------------------------------------------------------
-- Q9: Get bus ranking by fault level (MVA)
-- Useful for determining bus strengths and selecting voltage levels
-- ---------------------------------------------------------------------------
-- name: get_fault_level_ranking
SELECT 
    b.bus_number,
    b.name AS bus_name,
    b.vn_kv,
    rsc.fault_type,
    rsc.sk_mva,
    rsc.ik_ka,
    DENSE_RANK() OVER (PARTITION BY rsc.fault_type ORDER BY rsc.sk_mva DESC) AS fault_level_rank,
    CASE 
        WHEN rsc.sk_mva >= 10000 THEN 'Very strong bus (>10 GVA)'
        WHEN rsc.sk_mva >= 5000 THEN 'Strong bus (5-10 GVA)'
        WHEN rsc.sk_mva >= 1000 THEN 'Medium bus (1-5 GVA)'
        WHEN rsc.sk_mva >= 500 THEN 'Weak bus (0.5-1 GVA)'
        ELSE 'Very weak bus (<0.5 GVA)'
    END AS bus_strength
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
ORDER BY rsc.sk_mva DESC;

-- ---------------------------------------------------------------------------
-- Q10: Compare fault currents across different fault types
-- Shows which fault type produces the highest current at each bus
-- ---------------------------------------------------------------------------
-- name: get_fault_type_comparison
SELECT 
    b.bus_number,
    b.name AS bus_name,
    MAX(CASE WHEN rsc.fault_type = 'three_phase' THEN rsc.ik_ka END) AS ik_3ph_ka,
    MAX(CASE WHEN rsc.fault_type = 'single_phase' THEN rsc.ik_ka END) AS ik_1ph_ka,
    MAX(CASE WHEN rsc.fault_type = 'phase_to_phase' THEN rsc.ik_ka END) AS ik_2ph_ka,
    MAX(CASE WHEN rsc.fault_type = 'two_phase_ground' THEN rsc.ik_ka END) AS ik_2ph_g_ka,
    GREATEST(
        MAX(CASE WHEN rsc.fault_type = 'three_phase' THEN rsc.ik_ka END),
        MAX(CASE WHEN rsc.fault_type = 'single_phase' THEN rsc.ik_ka END),
        MAX(CASE WHEN rsc.fault_type = 'phase_to_phase' THEN rsc.ik_ka END),
        MAX(CASE WHEN rsc.fault_type = 'two_phase_ground' THEN rsc.ik_ka END)
    ) AS max_ik_ka,
    (
        SELECT rsc2.fault_type FROM results_short_circuit rsc2 
        WHERE rsc2.bus_id = b.id AND rsc2.run_id = :run_id
        ORDER BY rsc2.ik_ka DESC LIMIT 1
    ) AS dominant_fault_type
FROM results_short_circuit rsc
JOIN buses b ON b.id = rsc.bus_id
WHERE rsc.project_id = :project_id
  AND rsc.run_id = :run_id
GROUP BY b.id, b.bus_number, b.name
ORDER BY max_ik_ka DESC;

-- =============================================================================
-- END OF SHORT CIRCUIT RESULTS QUERIES
-- =============================================================================