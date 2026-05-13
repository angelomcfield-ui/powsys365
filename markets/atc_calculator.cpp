/**
 * @file atc_calculator.cpp
 * @brief Implementacion del calculador ATC/TTC para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "atc_calculator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

ATCCalculator::ATCCalculator() = default;
ATCCalculator::~ATCCalculator() = default;

// ============================================================================
// Configuracion
// ============================================================================

void ATCCalculator::setSystemData(const OPFInputData& data) {
    m_opfData = data;
    m_atcResults.clear();
}

// ============================================================================
// Calcular ATC entre dos barras
// ============================================================================

ATCResult ATCCalculator::calculateATC(int fromBus, int toBus,
    double trmPercent, double cbmPercent) {

    ATCResult result;
    result.fromBus = fromBus;
    result.toBus = toBus;

    // 1. Calcular TTC
    result.ttc = calculateTTC_N1(fromBus, toBus);
    if (result.ttc < 0.0) result.ttc = 0.0;

    // 2. Calcular TRM
    result.trm = calculateTRM(result.ttc, trmPercent);

    // 3. Calcular CBM
    result.cbm = calculateCBM(result.ttc, cbmPercent);

    // 4. Calcular ETC
    result.etc = calculateETC();

    // 5. ATC = TTC - TRM - CBM - ETC
    result.atc = result.ttc - result.trm - result.cbm - result.etc;
    if (result.atc < 0.0) result.atc = 0.0;

    // Flujo en caso base
    result.baseCaseFlow = m_opfData.totalGeneration;

    // Verificar seguridad N-1
    result.n1Secure = checkN1Security(fromBus, toBus, result.ttc * 0.5);

    // Almacenar resultado
    m_atcResults[{fromBus, toBus}] = result;

    return result;
}

// ============================================================================
// Calcular TTC
// ============================================================================

double ATCCalculator::calculateTTC(int fromBus, int toBus) const {
    (void)fromBus;  // Par�metros usados en version extendida
    (void)toBus;

    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    if (numLines == 0) return 0.0;

    // TTC = min(limit_k - |flow_k|) para todas las lineas k
    double minMargin = 1e300;

    for (int line = 0; line < numLines; ++line) {
        double flow = std::abs(m_opfData.lineFlows(line));
        double limit = m_opfData.lineLimits(line);

        if (limit <= 0.0) continue;

        double margin = limit - flow;
        if (margin < minMargin) {
            minMargin = margin;
        }
    }

    return (minMargin < 1e299) ? std::max(0.0, minMargin) : 0.0;
}

// ============================================================================
// Calcular TTC con consideracion N-1
// ============================================================================

double ATCCalculator::calculateTTC_N1(int fromBus, int toBus) const {
    // TTC en caso base
    double ttcBase = calculateTTC(fromBus, toBus);

    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    if (numLines == 0) return ttcBase;

    // Para cada contingencia N-1, calcular el TTC resultante
    double ttcN1 = ttcBase;

    for (int lineOut = 0; lineOut < numLines; ++lineOut) {
        // Simular contingencia: linea 'lineOut' fuera de servicio
        Eigen::VectorXd postContingencyFlows = simulateContingency(lineOut);

        // Calcular TTC post-contingencia
        double ttcContingency = 1e300;
        for (int line = 0; line < numLines; ++line) {
            if (line == lineOut) continue;  // La linea fallada no cuenta

            double limit = m_opfData.lineLimits(line);
            if (limit <= 0.0) continue;

            double flow = std::abs(postContingencyFlows(line));
            double margin = limit - flow;

            if (margin < ttcContingency) {
                ttcContingency = margin;
            }
        }

        if (ttcContingency < 1e299) {
            ttcN1 = std::min(ttcN1, ttcContingency);
        }
    }

    return std::max(0.0, ttcN1);
}

// ============================================================================
// Simular contingencia (falla de linea)
// ============================================================================

Eigen::VectorXd ATCCalculator::simulateContingency(int lineOut) const {
    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    Eigen::VectorXd postFlows = m_opfData.lineFlows;

    if (lineOut < 0 || lineOut >= numLines) return postFlows;

    // Aplicar LODF: flow_post_k = flow_pre_k + LODF_kl * flow_pre_l
    Eigen::VectorXd lodf = calculateLODFVector(lineOut);

    double flowOut = m_opfData.lineFlows(lineOut);

    for (int line = 0; line < numLines; ++line) {
        if (line == lineOut) {
            postFlows(line) = 0.0;  // La linea fallada no transporta flujo
        } else {
            postFlows(line) += lodf(line) * flowOut;
        }
    }

    return postFlows;
}

// ============================================================================
// Calcular vector LODF para una contingencia
// ============================================================================

Eigen::VectorXd ATCCalculator::calculateLODFVector(int lineOut) const {
    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    Eigen::VectorXd lodf = Eigen::VectorXd::Zero(numLines);

    if (lineOut < 0 || lineOut >= numLines) return lodf;

    // LODF simplificado usando reactancia
    double xOut = m_opfData.lineReactance(lineOut);
    if (xOut < 1e-10) return lodf;

    int fromOut = static_cast<int>(m_opfData.lineFromBus(lineOut));
    int toOut = static_cast<int>(m_opfData.lineToBus(lineOut));

    for (int line = 0; line < numLines; ++line) {
        if (line == lineOut) continue;

        double xLine = m_opfData.lineReactance(line);
        if (xLine < 1e-10) continue;

        int fromLine = static_cast<int>(m_opfData.lineFromBus(line));
        int toLine = static_cast<int>(m_opfData.lineToBus(line));

        // LODF aproximado usando la formula de distribucion lineal
        // LODF_kl = (X_k * (delta_k)) / (X_l * (1 - X_l/X_total))
        double lodfVal = 0.0;

        // Verificar si las lineas comparten nodos
        if (fromLine == fromOut || fromLine == toOut ||
            toLine == fromOut || toLine == toOut) {
            // Lineas conectadas: hay redistribucion
            lodfVal = (xLine / xOut) * 0.1;  // Factor simplificado
        }

        lodf(line) = lodfVal;
    }

    return lodf;
}

// ============================================================================
// Verificar limites de lineas
// ============================================================================

bool ATCCalculator::checkLineLimits(const Eigen::VectorXd& flows,
    std::vector<std::pair<int, double>>& violations) const {

    violations.clear();
    bool allWithinLimits = true;

    int numLines = static_cast<int>(flows.size());
    for (int line = 0; line < numLines; ++line) {
        double limit = m_opfData.lineLimits(line);
        if (limit <= 0.0) continue;

        double flow = std::abs(flows(line));
        if (flow > limit) {
            violations.emplace_back(line, flow - limit);
            allWithinLimits = false;
        }
    }

    return allWithinLimits;
}

// ============================================================================
// Verificar seguridad N-1
// ============================================================================

bool ATCCalculator::checkN1Security(int fromBus, int toBus,
    double transferAmount) const {

    (void)fromBus;
    (void)toBus;
    (void)transferAmount;

    int numLines = static_cast<int>(m_opfData.lineFlows.size());

    for (int lineOut = 0; lineOut < numLines; ++lineOut) {
        Eigen::VectorXd postFlows = simulateContingency(lineOut);

        std::vector<std::pair<int, double>> violations;
        if (!checkLineLimits(postFlows, violations)) {
            return false;  // Al menos una contingencia viola limites
        }
    }

    return true;
}

// ============================================================================
// TRM
// ============================================================================

double ATCCalculator::calculateTRM(double ttc, double percent) const {
    return ttc * std::max(0.0, std::min(percent, 0.25));
}

// ============================================================================
// CBM
// ============================================================================

double ATCCalculator::calculateCBM(double ttc, double percent) const {
    return ttc * std::max(0.0, std::min(percent, 0.15));
}

// ============================================================================
// ETC
// ============================================================================

double ATCCalculator::calculateETC() const {
    // ETC = compromisos existentes (generacion programada)
    // Aproximacion: 80% de la generacion total ya esta comprometida
    return m_opfData.totalGeneration * 0.8;
}

// ============================================================================
// ATC para todos los pares
// ============================================================================

std::map<std::pair<int, int>, ATCResult>
ATCCalculator::calculateATCForAllPairs() {
    m_atcResults.clear();

    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses <= 1) return m_atcResults;

    // Calcular para pares significativos (origen-destino)
    for (int from = 0; from < numBuses; ++from) {
        for (int to = 0; to < numBuses; ++to) {
            if (from == to) continue;

            // Solo pares con generacion disponible -> carga
            ATCResult result = calculateATC(from, to);

            if (result.atc > 1.0) {  // Solo pares con ATC significativo
                m_atcResults[{from, to}] = result;
            }
        }
    }

    return m_atcResults;
}

// ============================================================================
// Getters
// ============================================================================

const std::map<std::pair<int, int>, ATCResult>&
ATCCalculator::getResults() const {
    return m_atcResults;
}

// ============================================================================
// Reporte
// ============================================================================

std::string ATCCalculator::generateATCReport() const {
    std::stringstream report;

    report << "========================================\n";
    report << "REPORTE DE ATC (AVAILABLE TRANSFER CAPABILITY)\n";
    report << "========================================\n\n";

    report << "Parametros:\n";
    report << "  Barras: " << m_opfData.busVoltages.size() << "\n";
    report << "  Lineas: " << m_opfData.lineFlows.size() << "\n";
    report << "  Generacion total: " << std::fixed << std::setprecision(1)
           << m_opfData.totalGeneration << " MW\n\n";

    report << "Resultados ATC:\n";
    report << "---------------------------------------------------------------\n";
    report << "From  | To    |  TTC   |  TRM   |  CBM   |  ETC   |  ATC   | N-1\n";
    report << "---------------------------------------------------------------\n";

    for (const auto& [pair, result] : m_atcResults) {
        report << std::setw(5) << pair.first << " | "
               << std::setw(5) << pair.second << " | "
               << std::setw(6) << std::setprecision(1) << result.ttc << " | "
               << std::setw(6) << result.trm << " | "
               << std::setw(6) << result.cbm << " | "
               << std::setw(6) << result.etc << " | "
               << std::setw(6) << result.atc << " | "
               << (result.n1Secure ? "OK" : "FAIL") << "\n";
    }

    // Resumen
    double totalATC = 0.0;
    double maxATC = 0.0;
    int n1SecureCount = 0;

    for (const auto& [pair, result] : m_atcResults) {
        (void)pair;
        totalATC += result.atc;
        if (result.atc > maxATC) maxATC = result.atc;
        if (result.n1Secure) n1SecureCount++;
    }

    report << "\nResumen:\n";
    report << "  Pares con ATC: " << m_atcResults.size() << "\n";
    report << "  ATC total: " << std::setprecision(1) << totalATC << " MW\n";
    report << "  ATC maximo: " << maxATC << " MW\n";
    if (!m_atcResults.empty()) {
        report << "  Seguridad N-1: " << n1SecureCount << "/"
               << m_atcResults.size() << " pares seguros\n";
    }

    return report.str();
}

} // namespace powsys365
