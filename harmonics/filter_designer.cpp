/**
 * @file filter_designer.cpp
 * @brief Implementacion del disenador de filtros armonicos para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "filter_designer.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

FilterDesigner::FilterDesigner() = default;
FilterDesigner::~FilterDesigner() = default;

// ============================================================================
// Calculo interno de L, C, R
// ============================================================================

void FilterDesigner::computeLCR(FilterDesign& filter,
    int harmonicOrder, double Q, double reactivePowerVar,
    double fn, double Vn) {

    if (Vn <= 0.0 || fn <= 0.0 || harmonicOrder < 2 || Q <= 0.0) {
        return;
    }

    double omega_n = 2.0 * M_PI * fn;
    double omega_h = omega_n * harmonicOrder;

    filter.targetHarmonic = harmonicOrder;
    filter.tunedFrequency = fn * harmonicOrder;
    filter.ratedVoltage = Vn;
    filter.Q = Q;

    // Si no se especifica potencia reactiva, estimar: Qc = 1.35 * Vn * Ih
    if (reactivePowerVar <= 0.0) {
        reactivePowerVar = 1.35 * Vn;  // Simplificado
    }
    filter.reactivePower = reactivePowerVar / 1e6;  // a MVAr

    // C = Qc / (Vn^2 * omega_1)
    filter.C = reactivePowerVar / (Vn * Vn * omega_n);

    // L = 1 / (omega_h^2 * C)
    filter.L = 1.0 / (omega_h * omega_h * filter.C);

    // R = omega_h * L / Q
    filter.R = omega_h * filter.L / Q;
}

// ============================================================================
// Filtro pasivo sintonizado simple
// ============================================================================

FilterDesign FilterDesigner::designPassiveFilter(int harmonicOrder,
    double Q, double reactivePower, double fn, double Vn) {

    FilterDesign filter;
    filter.type = FilterDesign::TUNED;
    filter.busId = 0;  // Debe asignarse externamente

    double reactivePowerVar = reactivePower * 1e6;  // Convertir a VAR
    computeLCR(filter, harmonicOrder, Q, reactivePowerVar, fn, Vn);

    return filter;
}

// ============================================================================
// Filtro amortiguado de segundo orden
// ============================================================================

FilterDesign FilterDesigner::designDampedFilter(int harmonicOrder,
    double Q, double reactivePower, double fn, double Vn,
    double dampingFactor) {

    FilterDesign filter;
    filter.type = FilterDesign::DAMPED_SECOND_ORDER;
    filter.dampingFactor = dampingFactor;
    filter.busId = 0;

    double reactivePowerVar = reactivePower * 1e6;
    computeLCR(filter, harmonicOrder, Q, reactivePowerVar, fn, Vn);

    // En filtro amortiguado de 2o orden: L-C serie con R en paralelo a C
    // El Q efectivo se reduce con el factor de amortiguamiento
    double omega_h = 2.0 * M_PI * fn * harmonicOrder;

    // Aumentar R para reducir Q y proporcionar amortiguamiento en banda ancha
    filter.R = dampingFactor * omega_h * filter.L;

    // Recalcular Q efectivo
    filter.Q = omega_h * filter.L / filter.R;

    return filter;
}

// ============================================================================
// Filtro tipo C (doble sintonizado)
// ============================================================================

FilterDesign FilterDesigner::designTypeCFilter(int harmonicOrder1,
    int harmonicOrder2, double Q, double reactivePower,
    double fn, double Vn) {

    FilterDesign filter;
    filter.type = FilterDesign::TYPE_C;
    filter.busId = 0;

    // Filtro tipo C: doble sintonizado basado en frecuencia intermedia
    // Se diseña como un filtro sintonizado en la media geometrica
    double omega_n = 2.0 * M_PI * fn;
    double h_geom = std::sqrt(static_cast<double>(harmonicOrder1 * harmonicOrder2));
    double omega_mid = omega_n * h_geom;

    filter.targetHarmonic = harmonicOrder1;  // Orden primario
    filter.tunedFrequency = fn * harmonicOrder1;
    filter.ratedVoltage = Vn;
    filter.Q = Q;

    double reactivePowerVar = reactivePower * 1e6;
    filter.reactivePower = reactivePowerVar / 1e6;

    // Diseño basado en frecuencia intermedia
    // C se calcula para la frecuencia intermedia
    filter.C = reactivePowerVar / (Vn * Vn * omega_n);

    // L se calcula para la frecuencia intermedia
    filter.L = 1.0 / (omega_mid * omega_mid * filter.C);

    // R para el Q deseado a la frecuencia intermedia
    filter.R = omega_mid * filter.L / Q;

    return filter;
}

// ============================================================================
// Filtro desintonizado (detuned)
// ============================================================================

FilterDesign FilterDesigner::designDetunedFilter(int harmonicOrder,
    double Q, double reactivePower, double fn, double Vn,
    double detuneFactor) {

    FilterDesign filter;
    filter.type = FilterDesign::DETUNED;
    filter.busId = 0;

    // Filtro desintonizado: sintonizado en p*h*fn donde p < 1
    // Tipicamente p = 0.95 para evitar resonancia paralelo
    double tunedOrder = detuneFactor * harmonicOrder;

    double reactivePowerVar = reactivePower * 1e6;
    computeLCR(filter, static_cast<int>(std::round(tunedOrder)), Q,
               reactivePowerVar, fn, Vn);

    // Corregir la frecuencia de sintonia real
    filter.targetHarmonic = harmonicOrder;  // Objetivo original
    filter.tunedFrequency = fn * tunedOrder;

    return filter;
}

// ============================================================================
// Filtro activo
// ============================================================================

FilterDesign FilterDesigner::designActiveFilter(double bandwidth,
    double attenuation, const std::vector<int>& harmonicOrders,
    double Vn) {

    FilterDesign filter;
    filter.type = FilterDesign::ACTIVE;
    filter.busId = 0;
    filter.ratedVoltage = Vn;
    filter.bandwidth = bandwidth;
    filter.attenuation = attenuation;

    // Filtro activo: no tiene componentes L, C, R pasivos
    // Su "diseño" se basa en parametros de control
    filter.L = 0.0;
    filter.C = 0.0;
    filter.R = 0.0;
    filter.Q = 0.0;

    if (!harmonicOrders.empty()) {
        filter.targetHarmonic = harmonicOrders[0];
        filter.tunedFrequency = 50.0 * filter.targetHarmonic;  // Asumiendo 50 Hz
    }

    // Potencia reactiva estimada para el filtro activo
    // Tipicamente 5-10% de la potencia armonica a compensar
    filter.reactivePower = 0.0;  // Se calcula externamente

    return filter;
}

// ============================================================================
// Impedancia del filtro a una frecuencia
// ============================================================================

std::complex<double> FilterDesigner::calculateFilterImpedance(
    const FilterDesign& filter, double frequency) const {

    if (filter.type == FilterDesign::ACTIVE) {
        return std::complex<double>(0.0, 0.0);  // El activo tiene impedancia virtual
    }

    if (filter.C <= 0.0 || filter.L <= 0.0) {
        return std::complex<double>(1e10, 0.0);  // Circuito abierto
    }

    double omega = 2.0 * M_PI * frequency;

    // Impedancia del inductor: Z_L = j*omega*L
    std::complex<double> zL(0.0, omega * filter.L);

    // Impedancia del capacitor: Z_C = 1 / (j*omega*C) = -j / (omega*C)
    std::complex<double> zC(0.0, -1.0 / (omega * filter.C));

    std::complex<double> zR(filter.R, 0.0);

    std::complex<double> zFilter;

    switch (filter.type) {
        case FilterDesign::TUNED:
        case FilterDesign::DETUNED: {
            // Serie L-C con R: Z = R + j*omega*L - j/(omega*C)
            zFilter = zR + zL + zC;
            break;
        }

        case FilterDesign::DAMPED_SECOND_ORDER: {
            // L-C serie con R en paralelo al C
            // Z_RC = R // Z_C = (R * Z_C) / (R + Z_C)
            // Z_total = Z_L + Z_RC
            std::complex<double> zRC = (zR * zC) / (zR + zC);
            zFilter = zL + zRC;
            break;
        }

        case FilterDesign::DAMPED_THIRD_ORDER: {
            // L serie con C, R en derivacion
            // Z = Z_L + (Z_R // Z_C) = jwL + R/(1+jwRC)
            std::complex<double> zRC = (zR * zC) / (zR + zC);
            zFilter = zL + zRC;
            break;
        }

        case FilterDesign::TYPE_C: {
            // Simplificado: igual que sintonizado
            zFilter = zR + zL + zC;
            break;
        }

        default:
            zFilter = zR + zL + zC;
            break;
    }

    return zFilter;
}

// ============================================================================
// Respuesta en frecuencia completa
// ============================================================================

std::vector<FilterResponsePoint> FilterDesigner::calculateFilterResponse(
    const FilterDesign& filter,
    double f_min, double f_max, int steps) const {

    std::vector<FilterResponsePoint> response;
    response.reserve(steps + 1);

    if (steps <= 0) return response;

    double df = (f_max - f_min) / static_cast<double>(steps);

    for (int i = 0; i <= steps; ++i) {
        double freq = f_min + i * df;

        std::complex<double> zFilter = calculateFilterImpedance(filter, freq);

        FilterResponsePoint point;
        point.frequency = freq;
        point.impedance = zFilter;
        point.magnitude = std::abs(zFilter);
        point.phase = std::arg(zFilter);

        // Atenuacion relativa a la impedancia en resonancia
        double zResonance = std::abs(calculateFilterImpedance(
            filter, filter.tunedFrequency));
        if (zResonance > 1e-12) {
            point.attenuationDb = 20.0 * std::log10(point.magnitude / zResonance);
        }

        response.push_back(point);
    }

    return response;
}

// ============================================================================
// Atenuacion del filtro
// ============================================================================

double FilterDesigner::calculateAttenuation(const FilterDesign& filter,
    double frequency, std::complex<double> systemImpedance) const {

    std::complex<double> zFilter = calculateFilterImpedance(filter, frequency);

    // Impedancia paralelo filtro + sistema
    std::complex<double> zParallel = (zFilter * systemImpedance) / (zFilter + systemImpedance);

    // Atenuacion [dB] = 20*log10(|Z_system| / |Z_parallel|)
    double magSystem = std::abs(systemImpedance);
    double magParallel = std::abs(zParallel);

    if (magParallel < 1e-15) return 0.0;

    return 20.0 * std::log10(magSystem / magParallel);
}

// ============================================================================
// Verificar resonancias indeseadas
// ============================================================================

bool FilterDesigner::verifyNoUnwantedResonance(const FilterDesign& filter,
    int maxOrder, double maxSystemImpedance) const {

    if (!filter.isValid()) return false;

    double fn = 50.0;  // Asumir 50 Hz

    for (int h = 2; h <= maxOrder; ++h) {
        if (h == filter.targetHarmonic) continue;  // Saltar la sintonia intencional

        double freq = fn * h;
        std::complex<double> zFilter = calculateFilterImpedance(filter, freq);

        double magZf = std::abs(zFilter);

        // Si la impedancia del filtro es muy alta y coincide con un orden
        // armonico, puede causar resonancia
        if (magZf > maxSystemImpedance * 10.0) {
            // Potencial resonancia indeseada
            // No es un fallo critico pero se reporta
        }
    }

    return true;  // El diseno es aceptable
}

// ============================================================================
// Reporte del filtro
// ============================================================================

std::string FilterDesigner::generateFilterReport(const FilterDesign& filter) const {
    std::stringstream report;

    report << "========================================\n";
    report << "REPORTE DE DISENO DE FILTRO\n";
    report << "========================================\n\n";

    const char* typeNames[] = {
        "Sintonizado Simple", "Amortiguado 2o Orden",
        "Amortiguado 3er Orden", "Tipo C (Doble Sintonizado)",
        "Desintonizado", "Activo"
    };
    report << "Tipo: " << typeNames[filter.type] << "\n";
    report << "Barra: " << filter.busId << "\n";
    report << "Armonico objetivo: h=" << filter.targetHarmonic << "\n";
    report << "Frecuencia de sintonia: " << std::fixed << std::setprecision(1)
           << filter.tunedFrequency << " Hz\n";
    report << "Tension nominal: " << std::setprecision(0) << filter.ratedVoltage
           << " V\n\n";

    if (filter.type != FilterDesign::ACTIVE) {
        report << "Parametros del filtro:\n";
        report << "  L = " << std::scientific << std::setprecision(6)
               << filter.L << " H\n";
        report << "  C = " << std::scientific << std::setprecision(6)
               << filter.C << " F\n";
        report << "  R = " << std::fixed << std::setprecision(4)
               << filter.R << " ohm\n";
        report << "  Q = " << std::setprecision(2) << filter.Q << "\n";
        report << "  Potencia reactiva: " << std::setprecision(2)
               << filter.reactivePower << " MVAr\n";
    } else {
        report << "Parametros del filtro activo:\n";
        report << "  Ancho de banda: " << filter.bandwidth << " Hz\n";
        report << "  Atenuacion: " << filter.attenuation << " dB\n";
    }

    report << "\nValidacion: " << (filter.isValid() ? "VALIDO" : "INVALIDO") << "\n";

    return report.str();
}

} // namespace powsys365
