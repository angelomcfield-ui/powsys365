/**
 * @file resonance_analyzer.cpp
 * @brief Implementacion del analizador de resonancia para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "resonance_analyzer.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ResonanceAnalyzer::ResonanceAnalyzer() = default;
ResonanceAnalyzer::~ResonanceAnalyzer() = default;

// ============================================================================
// Analisis completo de resonancias
// ============================================================================

std::map<int, std::vector<ResonanceAnalysis>>
ResonanceAnalyzer::analyze(const FrequencyScanner& scanner,
                           const std::vector<int>& harmonicOrders) {
    m_analyses.clear();
    m_resonancePoints = scanner.detectAllResonances();

    for (const auto& [busId, points] : m_resonancePoints) {
        std::vector<ResonanceAnalysis> busAnalyses;
        for (const auto& point : points) {
            ResonanceAnalysis analysis = assessRiskDetailed(point, harmonicOrders);
            busAnalyses.push_back(analysis);
        }
        if (!busAnalyses.empty()) {
            m_analyses[busId] = busAnalyses;
        }
    }

    return m_analyses;
}

// ============================================================================
// Obtener puntos de resonancia
// ============================================================================

std::map<int, std::vector<ResonancePoint>>
ResonanceAnalyzer::getResonancePoints() const {
    return m_resonancePoints;
}

// ============================================================================
// Evaluacion de riesgo
// ============================================================================

ResonanceRisk ResonanceAnalyzer::assessRisk(const ResonancePoint& resonance,
    const std::vector<int>& activeHarmonics) const {

    double proximity = calculateSourceProximity(resonance.harmonicOrder, activeHarmonics);

    // Criterios segun IEEE 519 y buenas practicas:
    // - Q > 30 y cerca de fuente activa: HIGH
    // - Q > 10 o proximidad media: MEDIUM
    // - Q <= 10 y lejos: LOW

    if (resonance.qFactor > 30.0 && proximity > 0.8) {
        return ResonanceRisk::HIGH;
    }

    if (resonance.qFactor > 30.0 && proximity > 0.3) {
        return ResonanceRisk::HIGH;
    }

    if (resonance.qFactor > 10.0 || proximity > 0.3) {
        return ResonanceRisk::MEDIUM;
    }

    if (resonance.impedanceMagnitude > 10.0 && proximity > 0.5) {
        return ResonanceRisk::MEDIUM;
    }

    return ResonanceRisk::LOW;
}

// ============================================================================
// Evaluacion detallada de riesgo
// ============================================================================

ResonanceAnalysis ResonanceAnalyzer::assessRiskDetailed(
    const ResonancePoint& resonance,
    const std::vector<int>& activeHarmonics) const {

    ResonanceAnalysis analysis;
    analysis.frequency = resonance.frequency;
    analysis.harmonicOrder = resonance.harmonicOrder;
    analysis.qFactor = resonance.qFactor;
    analysis.impedanceMagnitude = resonance.impedanceMagnitude;
    analysis.proximityToSource = calculateSourceProximity(
        resonance.harmonicOrder, activeHarmonics);
    analysis.voltageAmplification = calculateVoltageAmplification(
        resonance.qFactor, 0.05);  // Zs ~ 0.05 pu tipico

    analysis.riskLevel = assessRisk(resonance, activeHarmonics);

    // Descripcion del riesgo
    std::stringstream desc;
    desc << "Resonancia ";
    if (resonance.type == ResonancePoint::PARALLEL) {
        desc << "PARALELO";
    } else {
        desc << "SERIE";
    }
    desc << " en f=" << std::fixed << std::setprecision(1) << resonance.frequency
         << " Hz (h=" << resonance.harmonicOrder << ")";
    desc << ", |Z|=" << std::setprecision(2) << resonance.impedanceMagnitude << " pu";
    desc << ", Q=" << std::setprecision(1) << resonance.qFactor;

    if (analysis.proximityToSource > 0.5) {
        desc << ". PROXIMO a fuente armonica activa.";
    }

    if (analysis.voltageAmplification > 2.0) {
        desc << " Amplificacion de tension > "
             << std::setprecision(1) << analysis.voltageAmplification << "x.";
    }

    analysis.riskDescription = desc.str();

    return analysis;
}

// ============================================================================
// Calcular proximidad a fuentes armonicas
// ============================================================================

double ResonanceAnalyzer::calculateSourceProximity(
    int resonanceOrder, const std::vector<int>& activeHarmonics) const {

    if (activeHarmonics.empty()) return 0.0;

    double maxProximity = 0.0;
    for (int h : activeHarmonics) {
        if (h < 2) continue;
        // Proximidad: 1.0 cuando coincide exactamente, decrece con la distancia
        double diff = std::abs(static_cast<double>(resonanceOrder) - static_cast<double>(h));
        double proximity = std::exp(-diff * diff / 2.0);  // Gaussiana
        maxProximity = std::max(maxProximity, proximity);
    }

    return maxProximity;
}

// ============================================================================
// Calcular amplificacion de tension
// ============================================================================

double ResonanceAnalyzer::calculateVoltageAmplification(double qFactor,
    double sourceImpedance) const {

    // En resonancia paralelo: V_h = Q * Z_s * I_h
    // Factor de amplificacion = Q * |Z_s| / |Z_s| = Q (aproximado)
    // Mas precisamente: V_amplification = Q / (1 + Q * Z_s / Z_res)
    if (qFactor < 1e-6 || sourceImpedance < 1e-12) return 0.0;

    return qFactor * sourceImpedance;
}

// ============================================================================
// Convertir riesgo a string
// ============================================================================

std::string ResonanceAnalyzer::riskToString(ResonanceRisk risk) {
    switch (risk) {
        case ResonanceRisk::LOW:    return "LOW";
        case ResonanceRisk::MEDIUM: return "MEDIUM";
        case ResonanceRisk::HIGH:   return "HIGH";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Generar reporte
// ============================================================================

std::string ResonanceAnalyzer::generateReport() const {
    std::stringstream report;

    report << "========================================\n";
    report << "REPORTE DE ANALISIS DE RESONANCIA\n";
    report << "========================================\n\n";

    auto dist = getRiskDistribution();
    report << "Resumen de Riesgos:\n";
    report << "  LOW:    " << dist[ResonanceRisk::LOW] << " resonancias\n";
    report << "  MEDIUM: " << dist[ResonanceRisk::MEDIUM] << " resonancias\n";
    report << "  HIGH:   " << dist[ResonanceRisk::HIGH] << " resonancias\n";
    report << "  TOTAL:  " << getTotalResonanceCount() << " resonancias\n\n";

    report << "Detalle por Barra:\n";
    report << "----------------------------------------\n";

    for (const auto& [busId, analyses] : m_analyses) {
        report << "Barra " << busId << ":\n";
        for (const auto& a : analyses) {
            report << "  f=" << std::fixed << std::setprecision(1) << a.frequency
                   << " Hz (h=" << a.harmonicOrder << ")";
            report << " |Z|=" << std::setprecision(2) << a.impedanceMagnitude << " pu";
            report << " Q=" << std::setprecision(1) << a.qFactor;
            report << " Riesgo: " << riskToString(a.riskLevel) << "\n";
            report << "    " << a.riskDescription << "\n";
        }
        report << "\n";
    }

    if (hasHighRiskResonances()) {
        report << "*** ALERTA: Se detectaron resonancias de ALTO RIESGO ***\n";
        report << "Se recomienda instalar filtros pasivos o activos.\n";
    }

    return report.str();
}

// ============================================================================
// Obtener analisis por barra
// ============================================================================

std::vector<ResonanceAnalysis> ResonanceAnalyzer::getAnalysisForBus(int busId) const {
    auto it = m_analyses.find(busId);
    if (it != m_analyses.end()) {
        return it->second;
    }
    return {};
}

// ============================================================================
// Verificar si hay riesgos altos
// ============================================================================

bool ResonanceAnalyzer::hasHighRiskResonances() const {
    for (const auto& [busId, analyses] : m_analyses) {
        for (const auto& a : analyses) {
            if (a.riskLevel == ResonanceRisk::HIGH) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Conteo de resonancias
// ============================================================================

size_t ResonanceAnalyzer::getTotalResonanceCount() const {
    size_t count = 0;
    for (const auto& [busId, analyses] : m_analyses) {
        count += analyses.size();
    }
    return count;
}

std::map<ResonanceRisk, size_t> ResonanceAnalyzer::getRiskDistribution() const {
    std::map<ResonanceRisk, size_t> dist;
    dist[ResonanceRisk::LOW] = 0;
    dist[ResonanceRisk::MEDIUM] = 0;
    dist[ResonanceRisk::HIGH] = 0;

    for (const auto& [busId, analyses] : m_analyses) {
        for (const auto& a : analyses) {
            dist[a.riskLevel]++;
        }
    }

    return dist;
}

} // namespace powsys365
