// ============================================================================
// bess.cpp
// Implementacion del modelo BESS
// ============================================================================

#include "bess.h"
#include <numeric>
#include <limits>

namespace powsys365 {

// ============================================================================
// Constructores
// ============================================================================
BESSModel::BESSModel()
    : params_(), mode_(BESS_MODE_STANDBY), soc_(0.5), stateOfHealth_(1.0),
      Pout_(0.0), Qout_(0.0), Pdc_(0.0), Vdc_(params_.dcVoltageNominal),
      Pref_(0.0), Qref_(0.0), Vterminal_(1.0), frequency_(60.0),
      temperature_(25.0), status_(1), socPrevious_(0.5), cycleRecording_(false),
      socMinCurrent_(0.1), socMaxCurrent_(0.9) {}

BESSModel::BESSModel(const BESSParameters& params)
    : params_(params), mode_(BESS_MODE_STANDBY), soc_(params.socInitial),
      stateOfHealth_(1.0), Pout_(0.0), Qout_(0.0), Pdc_(0.0),
      Vdc_(params.dcVoltageNominal), Pref_(0.0), Qref_(0.0),
      Vterminal_(1.0), frequency_(60.0), temperature_(25.0), status_(1),
      socPrevious_(params.socInitial), cycleRecording_(false),
      socMinCurrent_(params.socMin), socMaxCurrent_(params.socMax) {
    updateSOCLimits();
}

// ============================================================================
// Setters
// ============================================================================
void BESSModel::setParameters(const BESSParameters& params) {
    params_ = params;
    updateSOCLimits();
}

void BESSModel::setSOC(double soc) {
    soc_ = std::max(socMinCurrent_, std::min(socMaxCurrent_, soc));
}

void BESSModel::setSOCPercent(double socPct) {
    setSOC(socPct / 100.0);
}

void BESSModel::setOperatingMode(BESSOperatingMode mode) {
    mode_ = mode;
}

void BESSModel::setPowerReference(double P) {
    Pref_ = P;
}

void BESSModel::setReactiveReference(double Q) {
    Qref_ = Q;
}

void BESSModel::setDroopCoefficient(double droop) {
    params_.droopCoefficient = std::max(0.01, std::min(0.2, droop));
}

void BESSModel::setVirtualInertiaH(double H) {
    params_.virtualInertiaH = std::max(0.5, std::min(10.0, H));
}

void BESSModel::setTerminalVoltage(double Vpu) {
    Vterminal_ = Vpu;
}

void BESSModel::setFrequency(double fHz) {
    frequency_ = fHz;
}

void BESSModel::setTemperature(double tempC) {
    temperature_ = tempC;
}

// ============================================================================
// Actualizar limites de SOC considerando degradacion
// ============================================================================
void BESSModel::updateSOCLimits() {
    socMinCurrent_ = params_.socMin;
    socMaxCurrent_ = params_.socMax;
}

// ============================================================================
// Energia almacenada actual
// ============================================================================
double BESSModel::getEnergyStored() const {
    return params_.energyCapacity * soc_ * stateOfHealth_;
}

// ============================================================================
// Perdidas de conversion DC-AC
// ============================================================================
double BESSModel::calculateConverterLosses(double P) const {
    double absP = std::abs(P);
    if (absP < 1e-6) return 0.0;
    // Perdidas tipicas: 2-4% a potencia nominal
    double lossRatio = 0.02 + 0.02 * (absP / params_.ratedPower);
    return absP * std::min(0.05, lossRatio);
}

// ============================================================================
// Autodescarga
// ============================================================================
double BESSModel::calculateSelfDischarge(double dt) const {
    // Autodescarga en MWh
    return params_.energyCapacity * params_.selfDischargeRate * dt / 3600.0;
}

// ============================================================================
// Potencia maxima de carga (negativa)
// ============================================================================
double BESSModel::getMaxChargePower() const {
    double Pcharge = -params_.ratedPower * params_.chargeCrateMax;
    if (soc_ >= socMaxCurrent_) {
        Pcharge = 0.0; // SOC max: no cargar mas
    } else {
        // Limitar por energia disponible para cargar
        double Eavailable = params_.energyCapacity
                            * (socMaxCurrent_ - soc_);
        double PmaxByEnergy = -Eavailable * 3600.0; // Aprox. MW
        Pcharge = std::max(Pcharge, PmaxByEnergy);
    }
    return Pcharge;
}

// ============================================================================
// Potencia maxima de descarga (positiva)
// ============================================================================
double BESSModel::getMaxDischargePower() const {
    double Pdischarge = params_.ratedPower * params_.dischargeCrateMax;
    if (soc_ <= socMinCurrent_) {
        Pdischarge = 0.0; // SOC min: no descargar mas
    } else {
        // Limitar por energia disponible
        double Eavailable = params_.energyCapacity
                            * (soc_ - socMinCurrent_);
        double PmaxByEnergy = Eavailable * 3600.0; // Aprox. MW
        Pdischarge = std::min(Pdischarge, PmaxByEnergy);
    }
    return Pdischarge;
}

// ============================================================================
// Carga
// ============================================================================
double BESSModel::charge(double powerMW) {
    if (status_ == 0 || soc_ >= socMaxCurrent_) return 0.0;
    double PchargeMax = getMaxChargePower(); // Negativo
    double P_requested = -std::abs(powerMW); // Asegurar negativo
    double P_actual = std::max(PchargeMax, P_requested); // Limitar

    // Eficiencia de carga
    double eff = params_.chargeEfficiency;
    double Pdc = P_actual * eff; // Pdc negativo = carga
    double dE = -Pdc; // Energia positiva que entra (MW -> convertir)

    // Actualizar SOC
    double deltaSOC = std::abs(P_actual) * eff
                      / (params_.energyCapacity * 3600.0); // Por segundo
    // Nota: el SOC se actualiza en step() con dt

    Pout_ = P_actual; // Negativo = carga
    return P_actual;  // MW (negativo)
}

// ============================================================================
// Descarga
// ============================================================================
double BESSModel::discharge(double powerMW) {
    if (status_ == 0 || soc_ <= socMinCurrent_) return 0.0;
    double PdischargeMax = getMaxDischargePower(); // Positivo
    double P_requested = std::abs(powerMW); // Asegurar positivo
    double P_actual = std::min(PdischargeMax, P_requested); // Limitar

    Pout_ = P_actual; // Positivo = descarga
    return P_actual;  // MW (positivo)
}

// ============================================================================
// Potencia disponible
// ============================================================================
AvailablePower BESSModel::calculateAvailablePower() const {
    AvailablePower avail;
    avail.PchargeMax = getMaxChargePower();
    avail.PdischargeMax = getMaxDischargePower();
    avail.Eavailable = params_.energyCapacity * soc_ * stateOfHealth_;

    if (avail.PdischargeMax > 1e-6) {
        avail.timeToEmpty = avail.Eavailable / avail.PdischargeMax;
    } else {
        avail.timeToEmpty = std::numeric_limits<double>::infinity();
    }

    double E_to_full = params_.energyCapacity
                       * (socMaxCurrent_ - soc_) * stateOfHealth_;
    if (std::abs(avail.PchargeMax) > 1e-6) {
        avail.timeToFull = E_to_full / std::abs(avail.PchargeMax);
    } else {
        avail.timeToFull = std::numeric_limits<double>::infinity();
    }

    return avail;
}

// ============================================================================
// Registro de ciclos (rainflow counting simplificado)
// ============================================================================
void BESSModel::recordCycle(double socMin, double socMax) {
    if (socMax <= socMin) return;

    double dod = socMax - socMin; // Depth of Discharge
    CycleData cycle;
    cycle.dod = dod;
    cycle.socMin = socMin;
    cycle.socMax = socMax;
    // Ciclos equivalentes: metodo rainflow basico
    cycle.cycles = 1.0;
    // Degradacion por ciclo: model de Wohler (Arrhenius)
    // N = a * exp(-b * DoD) -> degradacion proporcional a DoD^1.5
    cycle.throughput = dod * params_.energyCapacity * stateOfHealth_;
    cycleHistory_.push_back(cycle);
}

// ============================================================================
// Degradacion ciclica
// ============================================================================
double BESSModel::calculateCycleDegradation() const {
    double totalDegradation = 0.0;
    for (const auto& cycle : cycleHistory_) {
        // Modelo de degradacion: d = k * DoD^1.5 * ciclos
        totalDegradation += params_.cycleDegradationFactor
                            * std::pow(cycle.dod, 1.5) * cycle.cycles;
    }
    return std::min(0.5, totalDegradation); // Max 50% degradacion
}

// ============================================================================
// Degradacion por calendario
// ============================================================================
double BESSModel::calculateCalendarDegradation(double years) const {
    // Modelo Arrhenius simplificado
    double T_ref = 25.0;
    double T = temperature_;
    double Ea = 0.1; // eV - energia de activacion
    double k = 8.617e-5; // eV/K
    double tempFactor = std::exp(-Ea / k * (1.0 / (T + 273.15)
                                             - 1.0 / (T_ref + 273.15)));
    return params_.calendarDegradationRate * years * tempFactor;
}

// ============================================================================
// Degradacion total
// ============================================================================
double BESSModel::calculateTotalDegradation(double years) const {
    double d_cycle = calculateCycleDegradation();
    double d_calendar = calculateCalendarDegradation(years);
    return std::min(0.8, d_cycle + d_calendar);
}

// ============================================================================
// Capacidad remanente
// ============================================================================
double BESSModel::getRemainingCapacity() const {
    return params_.energyCapacity * stateOfHealth_;
}

// ============================================================================
// Eficiencia actual
// ============================================================================
double BESSModel::getEfficiency() const {
    if (std::abs(Pout_) < 1e-6) return 1.0;
    double losses = calculateConverterLosses(Pout_);
    if (Pout_ > 0) {
        // Descarga: Pdc = Pout + perdidas
        return params_.dischargeEfficiency * (1.0 - losses / Pout_);
    } else {
        // Carga: Pdc = Pout - perdidas (Pout negativo)
        return params_.chargeEfficiency * (1.0 - losses / std::abs(Pout_));
    }
}

// ============================================================================
// Droop response
// ============================================================================
double BESSModel::calculateDroopResponse(double deltaF) const {
    // deltaF en pu: deltaF = (f - f0) / f0
    if (std::abs(deltaF) < params_.frequencyDeadband / 60.0) return 0.0;
    return -deltaF / params_.droopCoefficient * params_.ratedPower;
}

// ============================================================================
// Virtual inertia response
// ============================================================================
double BESSModel::calculateVirtualInertiaResponse(double df_dt) const {
    double K_vi = 2.0 * params_.virtualInertiaH;
    return -K_vi * df_dt * params_.ratedPower;
}

// ============================================================================
// Frequency regulation
// ============================================================================
double BESSModel::calculateFrequencyRegulationPower(double fHz,
                                                     double df_dt) const {
    double f0 = 60.0;
    double deltaF = (fHz - f0) / f0; // pu
    double P_droop = calculateDroopResponse(deltaF);
    double P_inertia = calculateVirtualInertiaResponse(df_dt);
    double P_total = P_droop + P_inertia + Pref_;

    // Limitar potencia
    double PchargeMax = getMaxChargePower();
    double PdischargeMax = getMaxDischargePower();
    return std::max(PchargeMax, std::min(PdischargeMax, P_total));
}

// ============================================================================
// FFR - Fast Frequency Response
// ============================================================================
double BESSModel::calculateFFR(double df_dt, double delta_f) const {
    double K_ffr = 10.0; // Ganancia tipica
    double P_ffr = K_ffr * delta_f * params_.ratedPower;
    if (df_dt < -0.5) {
        P_ffr *= 1.5; // Boost durante caida rapida
    }
    double PdischargeMax = getMaxDischargePower();
    double PchargeMax = getMaxChargePower();
    return std::max(PchargeMax, std::min(PdischargeMax, P_ffr));
}

// ============================================================================
// Peak shaving
// ============================================================================
double BESSModel::calculatePeakShavingPower(double loadMW,
                                             double thresholdMW) const {
    if (loadMW <= thresholdMW) return 0.0;
    double P_shave = loadMW - thresholdMW;
    double PdischargeMax = getMaxDischargePower();
    return std::min(PdischargeMax, P_shave);
}

// ============================================================================
// Voltage support
// ============================================================================
double BESSModel::calculateVoltageSupportQ(double Vpu) const {
    if (Vpu < 0.95) {
        // Voltaje bajo: inyectar Q (capacitivo)
        double Qmax = params_.ratedPower * 0.44; // Aprox PF = 0.9
        return Qmax * (0.95 - Vpu) / 0.05;
    } else if (Vpu > 1.05) {
        // Voltaje alto: absorber Q (inductivo)
        double Qmax = -params_.ratedPower * 0.44;
        return Qmax * (Vpu - 1.05) / 0.05;
    }
    return 0.0;
}

// ============================================================================
// Black start
// ============================================================================
bool BESSModel::canBlackStart() const {
    return params_.blackStartCapable
           && soc_ > 0.3
           && status_ == 1
           && stateOfHealth_ > 0.5;
}

double BESSModel::getBlackStartPower() const {
    if (!canBlackStart()) return 0.0;
    return std::min(params_.blackStartPower,
                    params_.ratedPower * 0.2);
}

// ============================================================================
// Step - simulacion temporal
// ============================================================================
void BESSModel::step(double dt, double fHz, double Vpu) {
    if (status_ == 0) {
        Pout_ = 0.0;
        Qout_ = 0.0;
        return;
    }

    frequency_ = fHz;
    Vterminal_ = Vpu;

    // Calcular potencia segun modo de operacion
    double P_cmd = 0.0;

    switch (mode_) {
        case BESS_MODE_STANDBY:
            P_cmd = 0.0;
            Qout_ = 0.0;
            break;

        case BESS_MODE_FREQUENCY_REGULATION:
        case BESS_MODE_SPINNING_RESERVE: {
            double f0 = 60.0;
            double deltaF = (fHz - f0) / f0;
            P_cmd = calculateFrequencyRegulationPower(fHz, 0.0);
            Qout_ = calculateVoltageSupportQ(Vpu);
            break;
        }

        case BESS_MODE_VOLTAGE_SUPPORT:
            P_cmd = Pref_;
            Qout_ = calculateVoltageSupportQ(Vpu);
            break;

        case BESS_MODE_PEAK_SHAVING:
        case BESS_MODE_ENERGY_ARBITRAGE:
        case BESS_MODE_RAMP_RATE_CONTROL:
            P_cmd = Pref_;
            Qout_ = Qref_;
            break;

        case BESS_MODE_BLACK_START:
            if (canBlackStart()) {
                P_cmd = getBlackStartPower();
            } else {
                P_cmd = 0.0;
            }
            Qout_ = 0.0;
            break;
    }

    // Limites de potencia
    double PchargeMax = getMaxChargePower();
    double PdischargeMax = getMaxDischargePower();
    Pout_ = std::max(PchargeMax, std::min(PdischargeMax, P_cmd));

    // Perdidas
    double losses = calculateConverterLosses(Pout_);

    // Actualizar SOC
    if (Pout_ > 1e-6) {
        // Descarga
        double energyOut = Pout_ * dt / 3600.0; // MWh
        double effLoss = energyOut
                         * (1.0 - params_.dischargeEfficiency);
        soc_ -= (energyOut + effLoss)
                / (params_.energyCapacity * stateOfHealth_);
    } else if (Pout_ < -1e-6) {
        // Carga
        double energyIn = std::abs(Pout_) * dt / 3600.0; // MWh
        double effLoss = energyIn
                         * (1.0 - params_.chargeEfficiency);
        soc_ += (energyIn - effLoss)
                / (params_.energyCapacity * stateOfHealth_);
    }

    // Autodescarga
    soc_ -= params_.selfDischargeRate * dt / 3600.0;

    // Limitar SOC
    soc_ = std::max(socMinCurrent_, std::min(socMaxCurrent_, soc_));

    // Deteccion de ciclos (simplificado)
    double socDelta = soc_ - socPrevious_;
    if (std::abs(socDelta) > 0.01) {
        if (!cycleRecording_ && socDelta < 0) {
            // Inicio de descarga
            cycleRecording_ = true;
        } else if (cycleRecording_ && socDelta > 0) {
            // Cambio de tendencia: ciclo detectado
            recordCycle(soc_, socPrevious_);
            cycleRecording_ = false;
        }
    }
    socPrevious_ = soc_;

    // Actualizar degradacion
    stateOfHealth_ = 1.0 - calculateTotalDegradation(0.0);

    // Voltaje DC proporcional a SOC
    double socNorm = (soc_ - socMinCurrent_)
                     / (socMaxCurrent_ - socMinCurrent_);
    Vdc_ = params_.dcVoltageMin
           + socNorm * (params_.dcVoltageMax - params_.dcVoltageMin);
}

} // namespace powsys365
