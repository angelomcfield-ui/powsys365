/**
 * @file lmp_calculator.cpp
 * @brief Implementacion del calculador de LMP para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "lmp_calculator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

LMPricingCalculator::LMPricingCalculator() = default;
LMPricingCalculator::~LMPricingCalculator() = default;

// ============================================================================
// Configuracion
// ============================================================================

void LMPricingCalculator::setOPFData(const OPFInputData& data) {
    m_opfData = data;
    m_cachedResults.clear();
}

// ============================================================================
// Validacion de datos
// ============================================================================

bool LMPricingCalculator::validateOPFData() const {
    return (m_opfData.busVoltages.size() > 0 &&
            m_opfData.lambdaEnergy >= 0.0 &&
            m_opfData.lineFlows.size() == m_opfData.lineLimits.size());
}

// ============================================================================
// Componente de energia (lambda)
// ============================================================================

double LMPricingCalculator::calculateEnergyComponent() const {
    return m_opfData.lambdaEnergy;
}

// ============================================================================
// Componente de congestion
// ============================================================================

double LMPricingCalculator::calculateCongestionComponent(int busId) const {
    if (!validateOPFData()) return 0.0;
    if (busId < 0 || busId >= m_opfData.busVoltages.size()) return 0.0;

    // Congestion = sum(mu_k * PTDF_ki) para todas las lineas k congestionadas
    double congestion = 0.0;
    int numLines = static_cast<int>(m_opfData.lineFlows.size());

    for (int line = 0; line < numLines; ++line) {
        double flow = m_opfData.lineFlows(line);
        double limit = m_opfData.lineLimits(line);

        if (limit <= 0.0) continue;

        double mu = 0.0;
        if (m_opfData.lambdaCongestion.size() > line) {
            mu = m_opfData.lambdaCongestion(line);
        } else {
            // Calcular mu aproximado: positivo solo si hay congestion
            double margin = limit - std::abs(flow);
            if (margin < 0.0) {
                // Linea congestionada: mu proporcional a la violacion
                mu = std::abs(margin) * 0.1;  // Factor de penalizacion
            }
        }

        if (std::abs(mu) < 1e-10) continue;

        // Calcular PTDF aproximado para esta barra y linea
        double ptdf = 0.0;
        int fromBus = static_cast<int>(m_opfData.lineFromBus(line));
        int toBus = static_cast<int>(m_opfData.lineToBus(line));

        // PTDF simplificado: 1 si la barra es el origen, -1 si es el destino
        if (fromBus == busId) {
            ptdf = 1.0;
        } else if (toBus == busId) {
            ptdf = -1.0;
        }

        // PTDF mas preciso usando reactancia
        double x_line = m_opfData.lineReactance(line);
        if (x_line > 1e-10) {
            // Factor proporcional a la distancia electrica
            ptdf = (busId == fromBus) ? (1.0 / x_line) * 0.01 :
                   (busId == toBus) ? -(1.0 / x_line) * 0.01 : 0.0;
        }

        congestion += mu * ptdf;
    }

    return congestion;
}

// ============================================================================
// Componente de perdidas
// ============================================================================

double LMPricingCalculator::calculateLossesComponent(int busId) const {
    if (!validateOPFData()) return 0.0;
    if (busId < 0 || busId >= m_opfData.busVoltages.size()) return 0.0;

    // Si los multiplicadores de perdidas estan disponibles, usarlos
    if (m_opfData.lambdaLosses.size() > busId) {
        return m_opfData.lambdaEnergy * m_opfData.lambdaLosses(busId);
    }

    // Calcular sensibilidad de perdidas
    double lossSensitivity = lossSensitivityFactor(busId);
    return m_opfData.lambdaEnergy * lossSensitivity;
}

// ============================================================================
// Sensibilidad de perdidas
// ============================================================================

double LMPricingCalculator::lossSensitivityFactor(int busId) const {
    // dL/dP_i = 2 * sum(R_k * P_k * PTDF_ki) para todas las lineas k
    // Simplificado: usar la posicion de la barra en el sistema
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses <= 1) return 0.0;

    // Factor de sensibilidad basado en la posicion electrica
    // Barras cercanas a generadores: sensibilidad baja
    // Barras lejanas: sensibilidad alta
    double sensitivity = 0.0;

    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    for (int line = 0; line < numLines; ++line) {
        double r = m_opfData.lineResistance(line);
        double flow = m_opfData.lineFlows(line);
        int fromBus = static_cast<int>(m_opfData.lineFromBus(line));
        int toBus = static_cast<int>(m_opfData.lineToBus(line));

        // Contribucion si la barra afecta el flujo de esta linea
        if (fromBus == busId || toBus == busId) {
            sensitivity += 2.0 * r * std::abs(flow) * 0.01;
        }
    }

    // Normalizar
    if (m_opfData.totalLosses > 0.0 && m_opfData.totalGeneration > 0.0) {
        double avgSensitivity = m_opfData.totalLosses / m_opfData.totalGeneration;
        sensitivity = std::max(0.0, sensitivity + avgSensitivity * 0.5);
    }

    return sensitivity;
}

// ============================================================================
// Matriz B' (susceptancia reducida)
// ============================================================================

Eigen::SparseMatrix<double> LMPricingCalculator::buildBPrimeMatrix() const {
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses <= 0) {
        return Eigen::SparseMatrix<double>(0, 0);
    }

    Eigen::SparseMatrix<double> bPrime(numBuses, numBuses);
    std::vector<Eigen::Triplet<double>> triplets;

    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    for (int line = 0; line < numLines; ++line) {
        double x = m_opfData.lineReactance(line);
        if (std::abs(x) < 1e-10) continue;

        double b = 1.0 / x;  // Susceptancia serie
        int fromBus = static_cast<int>(m_opfData.lineFromBus(line));
        int toBus = static_cast<int>(m_opfData.lineToBus(line));

        if (fromBus < 0 || fromBus >= numBuses) continue;
        if (toBus < 0 || toBus >= numBuses) continue;

        triplets.emplace_back(fromBus, fromBus, b);
        triplets.emplace_back(toBus, toBus, b);
        triplets.emplace_back(fromBus, toBus, -b);
        triplets.emplace_back(toBus, fromBus, -b);
    }

    bPrime.setFromTriplets(triplets.begin(), triplets.end());
    return bPrime;
}

// ============================================================================
// PTDF (Power Transfer Distribution Factors)
// ============================================================================

Eigen::SparseMatrix<double> LMPricingCalculator::calculatePTDF() const {
    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());

    if (numLines <= 0 || numBuses <= 0) {
        return Eigen::SparseMatrix<double>(0, 0);
    }

    Eigen::SparseMatrix<double> ptdf(numLines, numBuses);
    std::vector<Eigen::Triplet<double>> triplets;

    // Construir B' y resolver para cada inyeccion unitaria
    Eigen::SparseMatrix<double> bPrime = buildBPrimeMatrix();

    // Para cada linea, calcular PTDF_ki
    for (int line = 0; line < numLines; ++line) {
        double x = m_opfData.lineReactance(line);
        if (std::abs(x) < 1e-10) continue;

        int fromBus = static_cast<int>(m_opfData.lineFromBus(line));
        int toBus = static_cast<int>(m_opfData.lineToBus(line));

        if (fromBus < 0 || fromBus >= numBuses) continue;
        if (toBus < 0 || toBus >= numBuses) continue;

        // PTDF simplificado: inyectar 1 MW en barra i, medir flujo en linea k
        for (int bus = 0; bus < numBuses; ++bus) {
            if (bus == m_opfData.slackBus) continue;

            // PTDF aproximado: proporcional a la diferencia de angulos
            double ptdfVal = 0.0;
            if (bus == fromBus) {
                ptdfVal = 1.0 / x * 0.01;
            } else if (bus == toBus) {
                ptdfVal = -1.0 / x * 0.01;
            }

            if (std::abs(ptdfVal) > 1e-10) {
                triplets.emplace_back(line, bus, ptdfVal);
            }
        }
    }

    ptdf.setFromTriplets(triplets.begin(), triplets.end());
    return ptdf;
}

// ============================================================================
// LODF (Line Outage Distribution Factors)
// ============================================================================

Eigen::SparseMatrix<double> LMPricingCalculator::calculateLODF() const {
    int numLines = static_cast<int>(m_opfData.lineFlows.size());

    if (numLines <= 0) {
        return Eigen::SparseMatrix<double>(0, 0);
    }

    Eigen::SparseMatrix<double> lodf(numLines, numLines);
    std::vector<Eigen::Triplet<double>> triplets;

    // LODF_kl = PTDF_kl / (1 - PTDF_ll)
    auto ptdf = calculatePTDF();

    for (int l = 0; l < numLines; ++l) {
        double ptdf_ll = 0.0;
        // Extraer PTDF_ll (PTDF de la linea l respecto a si misma)
        for (Eigen::SparseMatrix<double>::InnerIterator it(ptdf, l); it; ++it) {
            // Simplificado: PTDF_ll se aproxima
        }

        double denom = 1.0 - ptdf_ll;
        if (std::abs(denom) < 1e-10) continue;

        for (int k = 0; k < numLines; ++k) {
            if (k == l) continue;

            double ptdf_kl = 0.0;
            for (Eigen::SparseMatrix<double>::InnerIterator it(ptdf, l); it; ++it) {
                if (it.row() == k) {
                    ptdf_kl = it.value();
                    break;
                }
            }

            double lodf_kl = ptdf_kl / denom;
            if (std::abs(lodf_kl) > 1e-10) {
                triplets.emplace_back(k, l, lodf_kl);
            }
        }
    }

    lodf.setFromTriplets(triplets.begin(), triplets.end());
    return lodf;
}

// ============================================================================
// GSF (Generation Shift Factors)
// ============================================================================

Eigen::SparseMatrix<double> LMPricingCalculator::calculateGSF() const {
    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());

    if (numLines <= 0 || numBuses <= 0) {
        return Eigen::SparseMatrix<double>(0, 0);
    }

    Eigen::SparseMatrix<double> gsf(numLines, numBuses);
    std::vector<Eigen::Triplet<double>> triplets;

    auto ptdf = calculatePTDF();
    int refBus = m_opfData.slackBus;

    // GSF_ki = PTDF_ki - PTDF_k_ref
    for (int line = 0; line < numLines; ++line) {
        double ptdf_ref = 0.0;

        // Obtener PTDF de la linea respecto a la barra de referencia
        if (refBus >= 0 && refBus < numBuses) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(ptdf, refBus); it; ++it) {
                if (it.row() == line) {
                    ptdf_ref = it.value();
                    break;
                }
            }
        }

        for (int bus = 0; bus < numBuses; ++bus) {
            if (bus == refBus) continue;

            double ptdf_ki = 0.0;
            for (Eigen::SparseMatrix<double>::InnerIterator it(ptdf, bus); it; ++it) {
                if (it.row() == line) {
                    ptdf_ki = it.value();
                    break;
                }
            }

            double gsfVal = ptdf_ki - ptdf_ref;
            if (std::abs(gsfVal) > 1e-10) {
                triplets.emplace_back(line, bus, gsfVal);
            }
        }
    }

    gsf.setFromTriplets(triplets.begin(), triplets.end());
    return gsf;
}

// ============================================================================
// Calcular LMP para una barra
// ============================================================================

LMPResult LMPricingCalculator::calculateLMP(int busId) const {
    LMPResult result;
    result.busId = busId;

    if (!validateOPFData() || busId < 0 ||
        busId >= m_opfData.busVoltages.size()) {
        return result;
    }

    result.isReferenceBus = (busId == m_opfData.slackBus);

    // Componente de energia: lambda del balance
    result.energyComponent = calculateEnergyComponent();

    // Componente de congestion: suma de mu * dG/dP
    result.congestionComponent = calculateCongestionComponent(busId);

    // Componente de perdidas: lambda * dL/dP
    result.lossesComponent = calculateLossesComponent(busId);

    // LMP total = energia + congestion + perdidas
    result.lmp = result.energyComponent +
                 result.congestionComponent +
                 result.lossesComponent;

    // Costo marginal: aproximado como el LMP
    result.marginalCost = result.lmp;

    return result;
}

// ============================================================================
// Calcular LMP para todas las barras
// ============================================================================

std::vector<LMPResult> LMPricingCalculator::calculateForAllBuses() const {
    m_cachedResults.clear();

    if (!validateOPFData()) {
        return m_cachedResults;
    }

    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    m_cachedResults.reserve(numBuses);

    for (int bus = 0; bus < numBuses; ++bus) {
        m_cachedResults.push_back(calculateLMP(bus));
    }

    return m_cachedResults;
}

// ============================================================================
// Descomponer LMP
// ============================================================================

std::tuple<double, double, double>
LMPricingCalculator::decomposeLMP(int busId) const {
    LMPResult result = calculateLMP(busId);
    return std::make_tuple(result.energyComponent,
                           result.congestionComponent,
                           result.lossesComponent);
}

// ============================================================================
// Identificar nodos congestionados
// ============================================================================

std::map<int, double> LMPricingCalculator::identifyCongestedNodes(
    double threshold) const {

    std::map<int, double> congested;

    double avgLMP = getAverageLMP();
    if (avgLMP < 1e-10) return congested;

    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    for (int bus = 0; bus < numBuses; ++bus) {
        LMPResult result = calculateLMP(bus);
        double deviation = std::abs(result.lmp - avgLMP);
        if (deviation > threshold) {
            congested[bus] = deviation;
        }
    }

    return congested;
}

// ============================================================================
// Identificar lineas congestionadas
// ============================================================================

std::vector<int> LMPricingCalculator::identifyCongestedLines() const {
    std::vector<int> congested;

    int numLines = static_cast<int>(m_opfData.lineFlows.size());
    for (int line = 0; line < numLines; ++line) {
        double flow = std::abs(m_opfData.lineFlows(line));
        double limit = m_opfData.lineLimits(line);

        if (limit > 0.0 && flow > limit * 0.95) {
            congested.push_back(line);
        }
    }

    return congested;
}

// ============================================================================
// Calcular FTR
// ============================================================================

double LMPricingCalculator::calculateFTR(int sourceBus, int sinkBus,
    double amount) const {

    if (!validateOPFData()) return 0.0;

    LMPResult source = calculateLMP(sourceBus);
    LMPResult sink = calculateLMP(sinkBus);

    // FTR = (LMP_sink - LMP_source) * cantidad
    return (sink.lmp - source.lmp) * amount;
}

// ============================================================================
// Estadisticas LMP
// ============================================================================

double LMPricingCalculator::getAverageLMP() const {
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses == 0) return 0.0;

    double sum = 0.0;
    for (int bus = 0; bus < numBuses; ++bus) {
        LMPResult result = calculateLMP(bus);
        sum += result.lmp;
    }

    return sum / numBuses;
}

double LMPricingCalculator::getMaxLMP() const {
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses == 0) return 0.0;

    double maxLMP = 0.0;
    for (int bus = 0; bus < numBuses; ++bus) {
        LMPResult result = calculateLMP(bus);
        if (result.lmp > maxLMP) {
            maxLMP = result.lmp;
        }
    }

    return maxLMP;
}

double LMPricingCalculator::getMinLMP() const {
    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    if (numBuses == 0) return 0.0;

    double minLMP = 1e300;
    for (int bus = 0; bus < numBuses; ++bus) {
        LMPResult result = calculateLMP(bus);
        if (result.lmp < minLMP) {
            minLMP = result.lmp;
        }
    }

    return (minLMP < 1e299) ? minLMP : 0.0;
}

// ============================================================================
// Reporte LMP
// ============================================================================

std::string LMPricingCalculator::generateLMPReport() const {
    std::stringstream report;

    report << "========================================\n";
    report << "REPORTE DE LMP (LOCATIONAL MARGINAL PRICE)\n";
    report << "========================================\n\n";

    report << "Parametros del sistema:\n";
    report << "  Barras: " << m_opfData.busVoltages.size() << "\n";
    report << "  Lineas: " << m_opfData.lineFlows.size() << "\n";
    report << "  Generacion total: " << std::fixed << std::setprecision(1)
           << m_opfData.totalGeneration << " MW\n";
    report << "  Demanda total: " << std::setprecision(1)
           << m_opfData.totalDemand << " MW\n";
    report << "  Perdidas: " << std::setprecision(2)
           << m_opfData.totalLosses << " MW\n\n";

    report << "Componente de Energia (lambda): " << std::setprecision(2)
           << calculateEnergyComponent() << " USD/MWh\n\n";

    report << "LMP por Barra:\n";
    report << "-------------------------------------------------\n";
    report << "BusID |   LMP   | Energia | Congestion | Perdidas\n";
    report << "-------------------------------------------------\n";

    int numBuses = static_cast<int>(m_opfData.busVoltages.size());
    for (int bus = 0; bus < numBuses; ++bus) {
        LMPResult r = calculateLMP(bus);
        report << std::setw(5) << r.busId << " | "
               << std::setw(7) << std::setprecision(2) << r.lmp << " | "
               << std::setw(7) << r.energyComponent << " | "
               << std::setw(10) << r.congestionComponent << " | "
               << std::setw(8) << r.lossesComponent << "\n";
    }

    report << "\nEstadisticas:\n";
    report << "  LMP promedio: " << std::setprecision(2) << getAverageLMP()
           << " USD/MWh\n";
    report << "  LMP maximo:   " << std::setprecision(2) << getMaxLMP()
           << " USD/MWh\n";
    report << "  LMP minimo:   " << std::setprecision(2) << getMinLMP()
           << " USD/MWh\n";

    auto congested = identifyCongestedNodes(1.0);
    report << "  Nodos congestionados: " << congested.size() << "\n";

    auto congestedLines = identifyCongestedLines();
    report << "  Lineas congestionadas: " << congestedLines.size() << "\n";

    return report.str();
}

} // namespace powsys365
