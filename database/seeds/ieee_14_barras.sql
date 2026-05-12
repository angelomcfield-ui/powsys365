-- ============================================================
-- Red de Prueba: IEEE 14 Barras
-- Sistema de referencia estandar para testing
-- Base MVA: 100, Frecuencia: 60 Hz
-- ============================================================

-- Crear proyecto
DO $$
DECLARE
    proj_id UUID;
    bus_ids UUID[14];
    line_ids UUID[20];
    trafo_ids UUID[6];
BEGIN
    -- Crear proyecto
    INSERT INTO projects (name, description, base_mva, frequency, standard)
    VALUES ('IEEE 14 Bus', 'Standard IEEE 14 bus test system', 100.0, 60.0, 'IEEE')
    RETURNING id INTO proj_id;

    -- Insertar barras
    INSERT INTO buses (project_id, bus_number, bus_type, name, v_base_kv, vm_pu, va_deg, vmax_pu, vmin_pu)
    VALUES
        (proj_id, 1, 3, 'Bus 1', 69.0, 1.06, 0.0, 1.1, 0.9),
        (proj_id, 2, 2, 'Bus 2', 69.0, 1.045, -4.98, 1.1, 0.9),
        (proj_id, 3, 2, 'Bus 3', 69.0, 1.01, -12.72, 1.1, 0.9),
        (proj_id, 4, 1, 'Bus 4', 69.0, 1.019, -10.33, 1.1, 0.9),
        (proj_id, 5, 1, 'Bus 5', 69.0, 1.02, -8.78, 1.1, 0.9),
        (proj_id, 6, 2, 'Bus 6', 13.8, 1.07, -14.22, 1.1, 0.9),
        (proj_id, 7, 1, 'Bus 7', 13.8, 1.062, -13.37, 1.1, 0.9),
        (proj_id, 8, 2, 'Bus 8', 18.0, 1.09, -13.36, 1.1, 0.9),
        (proj_id, 9, 1, 'Bus 9', 13.8, 1.056, -14.94, 1.1, 0.9),
        (proj_id, 10, 1, 'Bus 10', 13.8, 1.051, -15.1, 1.1, 0.9),
        (proj_id, 11, 1, 'Bus 11', 13.8, 1.057, -14.79, 1.1, 0.9),
        (proj_id, 12, 1, 'Bus 12', 13.8, 1.055, -15.07, 1.1, 0.9),
        (proj_id, 13, 1, 'Bus 13', 13.8, 1.05, -15.16, 1.1, 0.9),
        (proj_id, 14, 2, 'Bus 14', 13.8, 1.036, -16.04, 1.1, 0.9)
    RETURNING id INTO bus_ids[1], bus_ids[2], bus_ids[3], bus_ids[4], bus_ids[5], bus_ids[6], bus_ids[7], bus_ids[8], bus_ids[9], bus_ids[10], bus_ids[11], bus_ids[12], bus_ids[13], bus_ids[14];

    -- Insertar generadores
    INSERT INTO generators (project_id, bus_id, gen_number, name, p_mw, q_mvar, p_max_mw, p_min_mw, q_max_mvar, q_min_mvar, vn_kv, xd_pu, xq_pu, xd_prime_pu, xq_prime_pu, xd_double_prime_pu, xq_double_prime_pu, td0_sec, tq0_sec, td_prime_sec, tq_prime_sec, td_double_prime_sec, tq_double_prime_sec, h_sec, d_percent, gen_type, status)
    VALUES
        (proj_id, bus_ids[1], 1, 'Gen 1', 232.4, -16.9, 332.4, 0.0, 10.0, -10.0, 24.0, 0.146, 0.0969, 0.0608, 0.0969, 0.0239, 0.0969, 8.96, 0.31, 0.0, 0.0, 0.0, 0.0, 6.54, 2.0, 'slack', 1),
        (proj_id, bus_ids[2], 1, 'Gen 2', 40.0, 42.4, 140.0, 0.0, 50.0, -40.0, 24.0, 0.8958, 0.8645, 0.1198, 0.1969, 0.0597, 0.1969, 6.56, 0.535, 0.0, 0.0, 0.0, 0.0, 4.28, 2.0, 'pv', 1),
        (proj_id, bus_ids[3], 1, 'Gen 3', 0.0, 23.4, 100.0, 0.0, 40.0, 0.0, 24.0, 1.3125, 1.2578, 0.1813, 0.25, 0.0879, 0.25, 5.89, 0.6, 0.0, 0.0, 0.0, 0.0, 4.92, 2.0, 'pv', 1),
        (proj_id, bus_ids[6], 1, 'Gen 6', 0.0, 12.2, 100.0, 0.0, 24.0, -6.0, 12.0, 1.2578, 1.1719, 0.1813, 0.25, 0.0879, 0.25, 5.89, 0.6, 0.0, 0.0, 0.0, 0.0, 4.92, 2.0, 'pv', 1),
        (proj_id, bus_ids[8], 1, 'Gen 8', 0.0, 17.4, 100.0, 0.0, 24.0, -6.0, 18.0, 1.2578, 1.1719, 0.1813, 0.25, 0.0879, 0.25, 5.89, 0.6, 0.0, 0.0, 0.0, 0.0, 4.92, 2.0, 'pv', 1);

    -- Insertar cargas
    INSERT INTO loads (project_id, bus_id, load_number, name, p_mw, q_mvar, status)
    VALUES
        (proj_id, bus_ids[2], 1, 'Load 2', 21.7, 12.7, 1),
        (proj_id, bus_ids[3], 1, 'Load 3', 94.2, 19.0, 1),
        (proj_id, bus_ids[4], 1, 'Load 4', 47.8, -3.9, 1),
        (proj_id, bus_ids[5], 1, 'Load 5', 7.6, 1.6, 1),
        (proj_id, bus_ids[6], 1, 'Load 6', 11.2, 7.5, 1),
        (proj_id, bus_ids[9], 1, 'Load 9', 29.5, 16.6, 1),
        (proj_id, bus_ids[10], 1, 'Load 10', 9.0, 5.8, 1),
        (proj_id, bus_ids[11], 1, 'Load 11', 3.5, 1.8, 1),
        (proj_id, bus_ids[12], 1, 'Load 12', 6.1, 1.6, 1),
        (proj_id, bus_ids[13], 1, 'Load 13', 13.5, 5.8, 1),
        (proj_id, bus_ids[14], 1, 'Load 14', 14.9, 5.0, 1);

    -- Insertar lineas
    INSERT INTO lines (project_id, name, from_bus, to_bus, r_pu, x_pu, b_pu, length_km, status)
    VALUES
        (proj_id, 'Line 1-2', 1, 2, 0.01938, 0.05917, 0.0528, 0.0, 1),
        (proj_id, 'Line 1-5', 1, 5, 0.05403, 0.22304, 0.0492, 0.0, 1),
        (proj_id, 'Line 2-3', 2, 3, 0.04699, 0.19797, 0.0438, 0.0, 1),
        (proj_id, 'Line 2-4', 2, 4, 0.05811, 0.17632, 0.034, 0.0, 1),
        (proj_id, 'Line 2-5', 2, 5, 0.05695, 0.17388, 0.0346, 0.0, 1),
        (proj_id, 'Line 3-4', 3, 4, 0.06701, 0.17103, 0.0128, 0.0, 1),
        (proj_id, 'Line 4-5', 4, 5, 0.01335, 0.04211, 0.0, 0.0, 1),
        (proj_id, 'Line 4-7', 4, 7, 0.0, 0.20912, 0.0, 0.0, 1),
        (proj_id, 'Line 4-9', 4, 9, 0.0, 0.55618, 0.0, 0.0, 1),
        (proj_id, 'Line 5-6', 5, 6, 0.0, 0.25202, 0.0, 0.0, 1),
        (proj_id, 'Line 6-11', 6, 11, 0.09498, 0.1989, 0.0, 0.0, 1),
        (proj_id, 'Line 6-12', 6, 12, 0.12291, 0.25581, 0.0, 0.0, 1),
        (proj_id, 'Line 6-13', 6, 13, 0.06615, 0.13027, 0.0, 0.0, 1),
        (proj_id, 'Line 7-8', 7, 8, 0.0, 0.17615, 0.0, 0.0, 1),
        (proj_id, 'Line 7-9', 7, 9, 0.0, 0.11001, 0.0, 0.0, 1),
        (proj_id, 'Line 9-10', 9, 10, 0.03181, 0.0845, 0.0, 0.0, 1),
        (proj_id, 'Line 9-14', 9, 14, 0.12711, 0.27038, 0.0, 0.0, 1),
        (proj_id, 'Line 10-11', 10, 11, 0.08205, 0.19207, 0.0, 0.0, 1),
        (proj_id, 'Line 12-13', 12, 13, 0.22092, 0.19988, 0.0, 0.0, 1),
        (proj_id, 'Line 13-14', 13, 14, 0.17093, 0.34802, 0.0, 0.0, 1);

    -- Insertar transformadores
    INSERT INTO transformers (project_id, name, hv_bus, lv_bus, sn_mva, vn_hv_kv, vn_lv_kv, vk_percent, vkr_percent, pfe_kw, i0_percent, tap_side, tap_pos, tap_min, tap_max, tap_step_percent, status)
    VALUES
        (proj_id, 'Trafo 4-7', 4, 7, 100.0, 69.0, 13.8, 10.0, 0.0, 0.0, 0.0, 'HV', 0, -10, 10, 1.0, 1),
        (proj_id, 'Trafo 4-9', 4, 9, 100.0, 69.0, 13.8, 10.0, 0.0, 0.0, 0.0, 'HV', 0, -10, 10, 1.0, 1),
        (proj_id, 'Trafo 5-6', 5, 6, 100.0, 69.0, 13.8, 10.0, 0.0, 0.0, 0.0, 'HV', 0, -10, 10, 1.0, 1);

    RAISE NOTICE 'IEEE 14 Bus system seeded successfully with project ID: %', proj_id;
END $$;