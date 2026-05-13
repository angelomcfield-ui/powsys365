// ============================================================================
// electrolyzer.cpp
// Implementacion del modelo de electrolizador
// ============================================================================

#include "electrolyzer.h"
#include <numeric>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// Constructores
// ============================================================================

ElectrolyzerModel::ElectrolyzerModel()
    : type_(ELEC_PEM), params_(), mode_(ELEC_MODE_CONSTANT_POWER),
      powerInput_(5.0), current_(5000.0), temperature_(60.0),
      pressure_(30.0), loadFactor_(0.5), status_(1),
      operatingHours_(0.0) {
    lastResult_ = {};
}

ElectrolyzerModel::ElectrolyzerModel(ElectrolyzerType type,
                                      const ElectrolyzerParameters& params)
    : type_(type), params_(params), mode_(ELEC_MODE_CONSTANT_POWER),
      powerInput_(params.ratedPower * 0.5), current_(5000.0),
      temperature_(60.0), pressure_(30.0), loadFactor_(0.5),
      status_(1), operatingHours_(0.0) {
    lastResult_ = {};
    if (type_ == ELEC_SOEC) {
        temperature_ = params_.operatingTempSOEC;
    }
}

// ============================================================================
// Setters
// ============================================================================
void ElectrolyzerModel::setType(ElectrolyzerType type) { type_ = type; }
void ElectrolyzerModel::setParameters(const ElectrolyzerParameters& params) {
    params_ = params;
}
void ElectrolyzerModel::setPowerInput(double powerMW) {
    powerInput_ = std::max(0.0, powerMW);
    loadFactor_ = (params_.ratedPower > 1e-6)
        ? powerInput_ / params_.ratedPower : 0.0;
}
void ElectrolyzerModel::setCurrent(double currentA) {
    current_ = std::max(0.0,
        std::min(params_.currentMax, currentA));
}
void ElectrolyzerModel::setOperatingMode(ElectrolyzerMode mode) {
    mode_ = mode;
}
void ElectrolyzerModel::setTemperature(double tempC) {
    temperature_ = std::max(params_.tempMin,
                             std::min(params_.tempMax, tempC));
}
void ElectrolyzerModel::setPressure(double pressureBar) {
    pressure_ = std::max(1.0, pressureBar);
}
void ElectrolyzerModel::setStatus(int status) { status_ = status; }
void ElectrolyzerModel::setLoadFactor(double load) {
    loadFactor_ = std::max(params_.minLoad, std::min(1.0, load));
    powerInput_ = loadFactor_ * params_.ratedPower;
}

// ============================================================================
// Voltaje reversible (Nernst)
// E_rev = 1.229 - 0.0009*(T - 25) + 2.3*RT/4F * ln(P_H2^2 * P_O2 / a_H2O^2)
// Simplificado: E_rev = 1.229 - 0.0009*(T - 25)
// ============================================================================
double ElectrolyzerModel::calculateReversibleVoltage(double tempC) const {
    double E_rev = params_.vRev
                   - 0.0009 * (tempC - 25.0);
    // Correccion por presion
    double R_T = R_gas * (tempC + 273.15);
    double P_H2 = pressure_;
    double P_O2 = pressure_;
    if (P_H2 > 1.0 && P_O2 > 1.0) {
        E_rev += (R_T / (2.0 * F)) * std::log(P_H2 * std::sqrt(P_O2));
    }
    return E_rev;
}

// ============================================================================
// Sobrepotencial ohmico
// eta_ohm = I * (R_electrolyte + R_electrodes + R_membrane)
// ============================================================================
double ElectrolyzerModel::calculateOhmicOverpotential(double current) const {
    double R_total = 0.0;
    switch (type_) {
        case ELEC_ALKALINE:
            R_total = params_.diaphragmThickness * 0.001
                      / (params_.concentrationKOH * 0.001);
            break;
        case ELEC_PEM:
            R_total = params_.membraneThickness * 0.01
                      / params_.protonConductivity;
            break;
        case ELEC_SOEC:
            R_total = params_.electrolyteThickness * 0.01
                      / params_.ionicConductivity;
            break;
    }
    // Escalar al area de celda
    R_total /= (params_.cellArea * 1e-4); // Ohm/cm2
    double eta_ohm = current / params_.cellArea * R_total; // V
    return std::max(0.0, eta_ohm);
}

// ============================================================================
// Sobrepotencial de activacion (Tafel)
// eta_act = a + b * log10(I/I0)
// ============================================================================
double ElectrolyzerModel::calculateActivationOverpotential(double current) const {
    if (current < 1.0) return 0.0;
    double a, b;
    switch (type_) {
        case ELEC_ALKALINE:
            a = 0.1; b = 0.12; break;
        case ELEC_PEM:
            a = 0.05; b = 0.08; break;
        case ELEC_SOEC:
            a = 0.02; b = 0.05; break;
        default:
            a = 0.1; b = 0.1;
    }
    double j = current / params_.cellArea; // A/cm2
    double eta_act = a + b * std::log10(j);
    return std::max(0.0, eta_act);
}

// ============================================================================
// Sobrepotencial de concentracion
// eta_conc = (RT/nF) * ln(I_L / (I_L - I))
// ============================================================================
double ElectrolyzerModel::calculateConcentrationOverpotential(double current) const {
    double I_L = params_.currentMax * 1.5; // Corriente limite
    if (current >= I_L) return 0.5; // Alta limitacion
    double T_K = temperature_ + 273.15;
    double n = 2.0; // Electrones
    double eta_conc = (R_gas * T_K / (n * F))
                      * std::log(I_L / (I_L - current));
    return std::max(0.0, eta_conc);
}

// ============================================================================
// Voltaje total de celda: U = E_rev + eta_ohm + eta_act + eta_conc
// ============================================================================
double ElectrolyzerModel::calculateCellVoltage(double current) const {
    double E_rev = calculateReversibleVoltage(temperature_);
    double eta_ohm = calculateOhmicOverpotential(current);
    double eta_act = calculateActivationOverpotential(current);
    double eta_conc = calculateConcentrationOverpotential(current);

    double U = E_rev + eta_ohm + eta_act + eta_conc;
    return std::min(params_.vMax, std::max(params_.vRev, U));
}

double ElectrolyzerModel::calculateCellVoltage(double current,
                                                 double tempC) const {
    // Calculo con temperatura temporal sin modificar miembro
    double E_rev = params_.vRev - 0.0009 * (tempC - 25.0);
    double eta_ohm = calculateOhmicOverpotential(current);
    double eta_act = calculateActivationOverpotential(current);
    double eta_conc = calculateConcentrationOverpotential(current);
    (void)E_rev; // E_rev ya calculado en calculateCellVoltage(current)
    // Usamos el metodo principal y ajustamos por temperatura
    double U = calculateCellVoltage(current);
    double E_rev_nominal = params_.vRev - 0.0009 * (temperature_ - 25.0);
    double E_rev_new = params_.vRev - 0.0009 * (tempC - 25.0);
    return U - E_rev_nominal + E_rev_new;
}

// ============================================================================
// Corriente dado voltaje (inverso)
// ============================================================================
double ElectrolyzerModel::calculateCurrent(double cellVoltage) const {
    if (cellVoltage <= calculateReversibleVoltage(temperature_)) {
        return 0.0;
    }
    // Metodo iterativo: busqueda binaria
    double I_lo = 0.0, I_hi = params_.currentMax;
    for (int i = 0; i < 50; ++i) {
        double I_mid = (I_lo + I_hi) / 2.0;
        double U_mid = calculateCellVoltage(I_mid);
        if (U_mid < cellVoltage) {
            I_lo = I_mid;
        } else {
            I_hi = I_mid;
        }
        if (std::abs(U_mid - cellVoltage) < 1e-6) break;
    }
    return (I_lo + I_hi) / 2.0;
}

// ============================================================================
// Eficiencia de Faraday
// eta_F = f(i) = 1 - k * exp(j/j0) / j
// ============================================================================
double ElectrolyzerModel::calculateFaradayEfficiency(double currentDensity) const {
    if (currentDensity < 1e-6) return params_.faradayEfficiencyNominal;
    double j = currentDensity;
    double j_ref = 1.0; // A/cm2
    double k = params_.faradayCurrentFactor;
    double eta_F = 1.0 - k * std::exp(-j / j_ref);
    switch (type_) {
        case ELEC_ALKALINE:
            eta_F *= (1.0 - params_.gasCrossover);
            break;
        case ELEC_PEM:
            eta_F *= 0.995;
            break;
        case ELEC_SOEC:
            eta_F *= 0.99;
            break;
    }
    return std::max(0.5, std::min(params_.faradayEfficiencyNominal, eta_F));
}

// ============================================================================
// Produccion de H2
// n_H2 = eta_F * I * N_cells / (n * F)  [mol/s]
// Q_H2 = n_H2 * V_molar * 3600  [Nm3/h]
// ============================================================================
double ElectrolyzerModel::calculateH2FlowRate(double powerMW) const {
    if (status_ == 0 || powerMW < 1e-6) return 0.0;

    double P_elec = powerMW * (1.0 - params_.auxPowerFraction) * 1e6; // W
    double U_cell = calculateCellVoltage(current_);
    if (U_cell < 1e-6) return 0.0;

    double I = P_elec / (params_.numCells * U_cell);
    I = std::min(params_.currentMax, I);

    double j = I / params_.cellArea; // A/cm2
    double eta_F = calculateFaradayEfficiency(j);

    // n_H2 = eta_F * I * N / (2 * F) [mol/s]
    double n_mol_s = eta_F * I * params_.numCells / (2.0 * F);
    double Q_nm3_h = n_mol_s * V_molar_STP * 3600.0 / 1000.0; // Nm3/h

    return Q_nm3_h;
}

double ElectrolyzerModel::calculateH2MassFlow(double flowRateNm3h) const {
    // rho = M / V_molar
    double rho_kg_nm3 = M_H2 / V_molar_STP; // kg/Nm3
    return flowRateNm3h * rho_kg_nm3; // kg/h
}

// ============================================================================
// Consumo de agua
// H2O -> H2 + 0.5 O2
// 1 mol H2O = 1 mol H2
// Volumen = n * 18 mL/mol
// ============================================================================
double ElectrolyzerModel::calculateWaterConsumption(double h2FlowNm3h) const {
    double n_mol_h = h2FlowNm3h * 1000.0 / V_molar_STP; // mol/h
    double M_water = 18.015; // g/mol
    double mass_water = n_mol_h * M_water; // g/h
    return mass_water / 1000.0; // L/h (1 g = 1 mL)
}

// ============================================================================
// Produccion de O2
// ============================================================================
double ElectrolyzerModel::calculateO2Production(double h2FlowNm3h) const {
    // 1 mol H2 -> 0.5 mol O2
    return h2FlowNm3h * 0.5; // Nm3/h
}

// ============================================================================
// Calor generado
// Q = P_elec - (n_H2 * LHV) = P_elec * (1 - eta)
// ============================================================================
double ElectrolyzerModel::calculateHeatGenerated(double powerMW) const {
    double P_elec = powerMW * 1e6; // W
    double Q_h2 = calculateH2FlowRate(powerMW) / 3600.0
                  * 1000.0 / V_molar_STP * LHV_H2 * 1e6; // W
    return (P_elec - Q_h2) / 1000.0; // kW
}

// ============================================================================
// Eficiencia termica
// ============================================================================
double ElectrolyzerModel::calculateThermalEfficiency() const {
    double U = calculateCellVoltage(current_);
    double E_rev = calculateReversibleVoltage(temperature_);
    if (U < 1e-6) return 0.0;
    return (E_rev / U) * 100.0; // %
}

// ============================================================================
// Eficiencia a carga parcial
// ============================================================================
double ElectrolyzerModel::calculateEfficiencyAtLoad(double load) const {
    if (load < params_.minLoad) return 0.0;
    // Eficiencia tipica vs carga: menor a cargas muy bajas
    double baseEff = params_.efficiencyLHV;
    if (load < 0.3) {
        baseEff *= (0.7 + load);
    } else if (load > 0.8) {
        baseEff *= (1.0 - 0.05 * (load - 0.8) / 0.2);
    }
    return baseEff * 100.0; // %
}

// ============================================================================
// Calculo completo de produccion
// ============================================================================
H2ProductionResult ElectrolyzerModel::calculateH2Production() {
    return calculateH2Production(powerInput_);
}

H2ProductionResult ElectrolyzerModel::calculateH2Production(double powerMW) {
    H2ProductionResult result = {};
    if (status_ == 0 || powerMW < params_.minLoad * params_.ratedPower) {
        result.status = 0;
        lastResult_ = result;
        return result;
    }

    // Calcular corriente
    double P_elec_W = powerMW * (1.0 - params_.auxPowerFraction) * 1e6;
    double U_cell = params_.vNominal; // Inicial
    double I = P_elec_W / (params_.numCells * U_cell);

    // Iterar para convergencia
    for (int iter = 0; iter < 20; ++iter) {
        U_cell = calculateCellVoltage(I);
        double I_new = P_elec_W / (params_.numCells * U_cell);
        if (std::abs(I_new - I) < 1.0) {
            I = I_new;
            break;
        }
        I = I_new;
    }
    I = std::min(params_.currentMax, I);
    current_ = I;

    // Sobrepotenciales
    double E_rev = calculateReversibleVoltage(temperature_);
    double eta_ohm = calculateOhmicOverpotential(I);
    double eta_act = calculateActivationOverpotential(I);
    double eta_conc = calculateConcentrationOverpotential(I);
    U_cell = E_rev + eta_ohm + eta_act + eta_conc;

    // Eficiencia de Faraday
    double j = I / params_.cellArea;
    double eta_F = calculateFaradayEfficiency(j);

    // Produccion H2
    double n_mol_s = eta_F * I * params_.numCells / (2.0 * F);
    double Q_h2 = n_mol_s * V_molar_STP * 3600.0 / 1000.0;

    // Masico
    double m_h2 = calculateH2MassFlow(Q_h2);

    // Eficiencias
    double P_in_total = powerMW * 1e6; // W
    double P_h2 = m_h2 * LHV_H2 * 1e6 / 3600.0; // W (LHV)
    double P_h2_hhv = m_h2 * HHV_H2 * 1e6 / 3600.0; // W (HHV)

    double eff_LHV = (P_in_total > 1e-6)
        ? (P_h2 / P_in_total) * 100.0 : 0.0;
    double eff_HHV = (P_in_total > 1e-6)
        ? (P_h2_hhv / P_in_total) * 100.0 : 0.0;

    // Agua y O2
    double waterCons = calculateWaterConsumption(Q_h2);
    double o2Prod = calculateO2Production(Q_h2);

    // Calor
    double heatGen = calculateHeatGenerated(powerMW);

    result.h2FlowRate = Q_h2;
    result.h2MassFlow = m_h2;
    result.powerConsumption = powerMW;
    result.efficiencyLHV = eff_LHV;
    result.efficiencyHHV = eff_HHV;
    result.cellVoltage = U_cell;
    result.cellCurrent = I;
    result.currentDensity = j;
    result.electrolyteTemp = temperature_;
    result.ohmicOverpotential = eta_ohm;
    result.activationOverpotential = eta_act;
    result.concentrationOverpotential = eta_conc;
    result.faradayEfficiency = eta_F * 100.0;
    result.waterConsumption = waterCons;
    result.o2Production = o2Prod;
    result.heatGenerated = heatGen;
    result.status = 1;

    lastResult_ = result;
    return result;
}

// ============================================================================
// Curva I-U
// ============================================================================
std::vector<std::pair<double, double>> ElectrolyzerModel::getIVCurve(
    int numPoints) {
    std::vector<std::pair<double, double>> curve;
    curve.reserve(numPoints);

    double I_max = params_.currentMax;
    for (int i = 0; i < numPoints; ++i) {
        double I = I_max * i / (numPoints - 1);
        double U = calculateCellVoltage(I);
        curve.emplace_back(I / 1000.0, U); // kA vs V
    }
    return curve;
}

// ============================================================================
// Curva potencia vs produccion
// ============================================================================
std::vector<std::pair<double, double>> ElectrolyzerModel::getPowerProductionCurve(
    int numPoints) {
    std::vector<std::pair<double, double>> curve;
    curve.reserve(numPoints);

    for (int i = 0; i < numPoints; ++i) {
        double load = params_.minLoad
                      + (1.0 - params_.minLoad) * i / (numPoints - 1);
        double P = load * params_.ratedPower;
        double Q = calculateH2FlowRate(P);
        curve.emplace_back(P, Q);
    }
    return curve;
}

// ============================================================================
// Degradacion
// ============================================================================
double ElectrolyzerModel::calculateDegradationFactor(
    double operatingHours) const {
    // Degradacion tipica: 1-2% por 1000h
    double rate = (type_ == ELEC_PEM) ? 0.015 : 0.01;
    double degradation = rate * operatingHours / 1000.0;
    return std::max(0.5, 1.0 - degradation);
}

// ============================================================================
// Temperatura
// ============================================================================
double ElectrolyzerModel::calculateTemperatureRise(double powerMW,
                                                    double dt) const {
    double Q_gen = calculateHeatGenerated(powerMW); // kW
    double Q_loss = params_.heatLossCoefficient
                    * (temperature_ - 25.0); // kW
    double dT = (Q_gen - Q_loss) * dt
                / (params_.heatCapacity * 1000.0); // C
    return dT;
}

} // namespace powsys365
