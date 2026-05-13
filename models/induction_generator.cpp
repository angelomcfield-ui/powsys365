// ============================================================================
// induction_generator.cpp
// Implementacion del modelo de generador de induccion
// ============================================================================

#include "induction_generator.h"
#include <numeric>
#include <limits>

namespace powsys365 {

// Constantes
constexpr double IND_PI = 3.14159265358979323846;

// ============================================================================
// Constructores
// ============================================================================
InductionGeneratorModel::InductionGeneratorModel()
    : params_(), slip_(0.02), terminalVoltage_(1.0),
      frequency_(50.0), status_(1) {
    lastResult_ = {};
}

InductionGeneratorModel::InductionGeneratorModel(
    const InductionGeneratorParameters& params)
    : params_(params), slip_(0.02), terminalVoltage_(1.0),
      frequency_(50.0), status_(1) {
    lastResult_ = {};
}

// ============================================================================
// Setters
// ============================================================================
void InductionGeneratorModel::setParameters(
    const InductionGeneratorParameters& params) {
    params_ = params;
}

void InductionGeneratorModel::setSlip(double slip) {
    slip_ = std::max(-1.0, std::min(1.0, slip));
}

void InductionGeneratorModel::setSpeed(double speedRpm) {
    double ns = getSynchronousSpeed();
    if (std::abs(ns) < 1e-6) {
        slip_ = 0.0;
    } else {
        slip_ = (ns - speedRpm) / ns;
    }
}

void InductionGeneratorModel::setMechanicalPower(double Pmech) {
    slip_ = solveSlipFromPower(Pmech);
}

void InductionGeneratorModel::setTerminalVoltage(double Vpu) {
    terminalVoltage_ = Vpu;
}

void InductionGeneratorModel::setFrequency(double fHz) {
    frequency_ = fHz;
    // Actualizar velocidad sincrona
    params_.baseFrequency = fHz;
}

void InductionGeneratorModel::setStatus(int status) { status_ = status; }

// ============================================================================
// Velocidad sincrona
// ============================================================================
double InductionGeneratorModel::getSynchronousSpeed() const {
    // ns = 120 * f / P [rpm]
    return 120.0 * frequency_ / params_.numPoles;
}

double InductionGeneratorModel::getSynchronousSpeedRad() const {
    return 2.0 * IND_PI * getSynchronousSpeed() / 60.0;
}

double InductionGeneratorModel::getActualSpeed(double slip) const {
    return getSynchronousSpeed() * (1.0 - slip);
}

// ============================================================================
// Impedancias del circuito equivalente
// Z = (R1 + jX1) + ((R2/s + jX2) || jXm)
// ============================================================================
double InductionGeneratorModel::calculateRotorImpedance(double slip) const {
    double s = slip;
    if (std::abs(s) < 1e-10) s = (s >= 0) ? 1e-10 : -1e-10;
    double R2_s = params_.R2 / s;
    double Z2 = std::sqrt(R2_s * R2_s + params_.X2 * params_.X2);
    return Z2;
}

double InductionGeneratorModel::calculateTheveninVoltage() const {
    // Vth = V1 * jXm / (R1 + j(X1 + Xm))
    double denom = std::sqrt(params_.R1 * params_.R1
                              + (params_.X1 + params_.Xm)
                                * (params_.X1 + params_.Xm));
    if (denom < 1e-10) return terminalVoltage_;
    return terminalVoltage_ * params_.Xm / denom;
}

double InductionGeneratorModel::calculateTheveninImpedance() const {
    // Zth = jXm || (R1 + jX1)
    double Zm = params_.Xm;
    double Z1 = std::sqrt(params_.R1 * params_.R1
                           + params_.X1 * params_.X1);
    // Zth = Zm * Z1 / (Zm + Z1) - aproximacion
    return Zm * Z1 / (Zm + Z1);
}

double InductionGeneratorModel::calculateImpedance(double slip) const {
    double s = slip;
    if (std::abs(s) < 1e-10) s = (s >= 0) ? 1e-10 : -1e-10;

    // Z = (R1 + jX1) + (R2/s + jX2) || jXm
    double R2_s = params_.R2 / s;
    double a = R2_s;
    double b = params_.X2;
    double c = 0.0; // Rm = 0
    double d = params_.Xm;

    // (a + jb) || (c + jd) = ((a+jb)*(c+jd)) / ((a+c) + j(b+d))
    double denom = (a + c) * (a + c) + (b + d) * (b + d);
    if (denom < 1e-10) return std::numeric_limits<double>::infinity();

    double R_parallel = (a * c - b * d) * (a + c) + (a * d + b * c) * (b + d);
    double X_parallel = (a * d + b * c) * (a + c) - (a * c - b * d) * (b + d);
    R_parallel /= denom;
    X_parallel /= denom;

    double R_total = params_.R1 + R_parallel;
    double X_total = params_.X1 + X_parallel;
    return std::sqrt(R_total * R_total + X_total * X_total);
}

// Corriente estator: I1 = V1 / |Z_total|
double InductionGeneratorModel::calculateStatorCurrent(double slip) const {
    double Z = calculateImpedance(slip);
    if (Z < 1e-10) return std::numeric_limits<double>::infinity();
    return terminalVoltage_ / Z;
}

// Corriente rotor: I2 = I1 * jXm / (R2/s + j(X2 + Xm))
double InductionGeneratorModel::calculateRotorCurrent(double slip) const {
    double s = slip;
    if (std::abs(s) < 1e-10) s = (s >= 0) ? 1e-10 : -1e-10;
    double I1 = calculateStatorCurrent(s);
    double Z_branch = std::sqrt(params_.R2 * params_.R2 / (s * s)
                                  + (params_.X2 + params_.Xm)
                                    * (params_.X2 + params_.Xm));
    if (Z_branch < 1e-10) return 0.0;
    return I1 * params_.Xm / Z_branch;
}

// Corriente magnetizante: Im = I1 - I2 (aproximado)
double InductionGeneratorModel::calculateMagnetizingCurrent(double slip) const {
    (void)slip;
    double I1 = calculateStatorCurrent(slip_);
    double I2 = calculateRotorCurrent(slip_);
    return std::sqrt(std::abs(I1 * I1 - I2 * I2));
}

// ============================================================================
// Torque electromagnetico
// Te = 3 * Vth^2 * (R2/s) / (ws * ((Rth + R2/s)^2 + (Xth + X2)^2))
// En pu: Te = Vth^2 * (R2/s) / ((Rth + R2/s)^2 + (Xth + X2)^2)
// ============================================================================
double InductionGeneratorModel::calculateTorque(double slip) const {
    double s = slip;
    if (std::abs(s) < 1e-10) {
        // Limit approaching zero
        s = 1e-10;
    }
    double Vth = calculateTheveninVoltage();
    double Zth = calculateTheveninImpedance();
    double Rth = params_.R1;
    double Xth = params_.X1 * params_.Xm
                 / (params_.X1 + params_.Xm); // Approx

    double R2_s = params_.R2 / s;
    double denom = (Rth + R2_s) * (Rth + R2_s)
                   + (Xth + params_.X2) * (Xth + params_.X2);
    if (denom < 1e-10) return 0.0;

    double Te = Vth * Vth * R2_s / denom; // pu
    return Te;
}

// Torque en Nm
double InductionGeneratorModel::calculateTorqueNm(double slip) const {
    double Te_pu = calculateTorque(slip);
    double T_base = params_.baseMVA * 1e6
                    / getSynchronousSpeedRad(); // Nm base
    return Te_pu * T_base;
}

// Torque maximo
double InductionGeneratorModel::calculateMaxTorque() const {
    double Vth = calculateTheveninVoltage();
    double Xth = params_.X1 * params_.Xm
                 / (params_.X1 + params_.Xm);
    double Rth = params_.R1;
    double denom = 2.0 * (Rth
                           + std::sqrt(Rth * Rth
                                        + (Xth + params_.X2)
                                          * (Xth + params_.X2)));
    if (denom < 1e-10) return 0.0;
    return Vth * Vth / denom;
}

double InductionGeneratorModel::calculateSlipAtMaxTorque() const {
    double Xth = params_.X1 * params_.Xm
                 / (params_.X1 + params_.Xm);
    double Rth = params_.R1;
    double s_max = params_.R2
                   / std::sqrt(Rth * Rth
                                + (Xth + params_.X2)
                                  * (Xth + params_.X2));
    return s_max;
}

double InductionGeneratorModel::calculateStartingTorque() const {
    return calculateTorque(1.0); // s = 1 (rotor bloqueado)
}

double InductionGeneratorModel::calculatePullUpTorque() const {
    // Tipicamente 80% del torque maximo
    return calculateMaxTorque() * 0.8;
}

// ============================================================================
// Potencia
// ============================================================================
double InductionGeneratorModel::calculateActivePower(double slip) const {
    double Te = calculateTorque(slip);
    // P = Te * ws * (1-s)
    double P_pu = Te * (1.0 - slip);
    return P_pu * params_.ratedPower; // MW
}

double InductionGeneratorModel::calculateReactivePower(double slip) const {
    double I1 = calculateStatorCurrent(slip);
    double Z = calculateImpedance(slip);
    if (Z < 1e-10) return 0.0;
    double R_total = terminalVoltage_ / I1;
    // Q = S * sin(phi) = V * I * X / |Z|
    double X_total = std::sqrt(std::abs(Z * Z - R_total * R_total));
    double Q_pu = terminalVoltage_ * I1 * X_total / Z;
    return Q_pu * params_.ratedPower; // MVAr
}

double InductionGeneratorModel::calculateAirGapPower(double slip) const {
    double s = slip;
    if (std::abs(s) < 1e-10) s = 1e-10;
    double I2 = calculateRotorCurrent(s);
    double Pag = I2 * I2 * params_.R2 / s; // pu
    return Pag * params_.ratedPower; // MW
}

double InductionGeneratorModel::calculateRotorLosses(double slip) const {
    double Pag = calculateAirGapPower(slip);
    double s = std::abs(slip);
    return Pag * s; // MW
}

double InductionGeneratorModel::calculateStatorLosses(double slip) const {
    double I1 = calculateStatorCurrent(slip);
    return I1 * I1 * params_.R1 * params_.ratedPower; // MW
}

double InductionGeneratorModel::calculateEfficiency(double slip) const {
    double Pout = calculateActivePower(slip);
    double P_loss = calculateRotorLosses(slip)
                    + calculateStatorLosses(slip)
                    + calculateMechanicalLosses(
                        getActualSpeed(slip))
                    + calculateIronLosses(slip);
    double Pin = Pout + P_loss;
    if (Pin < 1e-6) return 0.0;
    return (Pout / Pin) * 100.0;
}

double InductionGeneratorModel::calculatePowerFactor(double slip) const {
    double I1 = calculateStatorCurrent(slip);
    double Z = calculateImpedance(slip);
    if (I1 < 1e-10 || Z < 1e-10) return 1.0;
    // PF = R / |Z|
    double R2_s = params_.R2 / std::max(1e-10, std::abs(slip));
    // R_total aproximado
    double R_magnetic = params_.R1;
    double R_branch = (R2_s * params_.Xm * params_.Xm)
                      / (R2_s * R2_s
                         + (params_.X2 + params_.Xm)
                           * (params_.X2 + params_.Xm));
    double R_total = params_.R1 + R_branch;
    return R_total / Z;
}

// ============================================================================
// Perdidas
// ============================================================================
double InductionGeneratorModel::calculateMechanicalLosses(double speedRpm) const {
    double omega = 2.0 * IND_PI * speedRpm / 60.0;
    return params_.frictionCoeff * omega * omega * params_.ratedPower;
}

double InductionGeneratorModel::calculateIronLosses(double slip) const {
    return params_.ironLossFactor * (1.0 + std::abs(slip))
           * params_.ratedPower;
}

// ============================================================================
// Corrientes de arranque
// ============================================================================
double InductionGeneratorModel::calculateStartingCurrent() const {
    return calculateStatorCurrent(1.0); // s = 1
}

double InductionGeneratorModel::calculateStartingCurrentK() const {
    double I_start = calculateStartingCurrent();
    double I_rated = calculateStatorCurrent(slip_);
    if (I_rated < 1e-10) return 0.0;
    return I_start / I_rated;
}

double InductionGeneratorModel::calculateLockedRotorCurrent() const {
    return calculateStartingCurrent(); // s = 1 es rotor bloqueado
}

double InductionGeneratorModel::calculateInrushCurrent() const {
    // Corriente de inrush: 5-8 veces la corriente de arranque por saturacion
    double I_start = calculateStartingCurrent();
    return I_start * 6.0;
}

// ============================================================================
// Curva torque-slip
// ============================================================================
std::vector<TorqueSlipPoint>
InductionGeneratorModel::getTorqueSlipCurve(int numPoints) {
    std::vector<TorqueSlipPoint> curve;
    curve.reserve(numPoints);

    for (int i = 0; i < numPoints; ++i) {
        // Rango de slip: -0.5 a 1.0
        double s = -0.5 + 1.5 * i / (numPoints - 1);
        TorqueSlipPoint pt;
        pt.slip = s;
        pt.torque = calculateTorque(s);
        pt.current = calculateStatorCurrent(s);
        pt.powerFactor = calculatePowerFactor(s);
        pt.speed = getActualSpeed(s);
        curve.push_back(pt);
    }
    return curve;
}

std::vector<std::pair<double, double>>
InductionGeneratorModel::getCurrentSlipCurve(int numPoints) {
    std::vector<std::pair<double, double>> curve;
    curve.reserve(numPoints);
    for (int i = 0; i < numPoints; ++i) {
        double s = -0.5 + 1.5 * i / (numPoints - 1);
        if (std::abs(s) < 1e-10) s = 1e-10;
        curve.emplace_back(s, calculateStatorCurrent(s));
    }
    return curve;
}

std::vector<std::pair<double, double>>
InductionGeneratorModel::getPowerFactorSlipCurve(int numPoints) {
    std::vector<std::pair<double, double>> curve;
    curve.reserve(numPoints);
    for (int i = 0; i < numPoints; ++i) {
        double s = -0.5 + 1.5 * i / (numPoints - 1);
        if (std::abs(s) < 1e-10) s = 1e-10;
        curve.emplace_back(s, calculatePowerFactor(s));
    }
    return curve;
}

// ============================================================================
// Resolver slip
// ============================================================================
double InductionGeneratorModel::solveSlipFromTorque(double torque) const {
    // Busqueda binaria en el rango de slip
    double s_lo = -0.5, s_hi = 1.0;
    for (int i = 0; i < 50; ++i) {
        double s_mid = (s_lo + s_hi) / 2.0;
        double T_mid = calculateTorque(s_mid);
        if (T_mid < torque) {
            // Para generador (s < 0), torque aumenta con |s|
            if (torque > 0) {
                s_hi = s_mid;
            } else {
                s_lo = s_mid;
            }
        } else {
            if (torque > 0) {
                s_lo = s_mid;
            } else {
                s_hi = s_mid;
            }
        }
        if (std::abs(s_hi - s_lo) < 1e-8) break;
    }
    double s = (s_lo + s_hi) / 2.0;
    return std::max(-0.5, std::min(1.0, s));
}

double InductionGeneratorModel::solveSlipFromPower(double Pmech) const {
    // P_mech = T * ws * (1-s)
    // Busqueda binaria
    double s_lo = -0.5, s_hi = 1.0;
    double P_pu = Pmech / params_.ratedPower;
    for (int i = 0; i < 50; ++i) {
        double s_mid = (s_lo + s_hi) / 2.0;
        if (std::abs(s_mid) < 1e-10) s_mid = 1e-10;
        double T_mid = calculateTorque(s_mid);
        double P_mid = T_mid * (1.0 - s_mid);
        if (P_mid < P_pu) {
            s_hi = s_mid;
        } else {
            s_lo = s_mid;
        }
        if (std::abs(s_hi - s_lo) < 1e-8) break;
    }
    double s = (s_lo + s_hi) / 2.0;
    return std::max(-0.5, std::min(1.0, s));
}

// ============================================================================
// Calculo completo
// ============================================================================
InductionGeneratorResult InductionGeneratorModel::calculate() {
    return calculate(slip_);
}

InductionGeneratorResult InductionGeneratorModel::calculate(double slip) {
    InductionGeneratorResult res = {};
    if (status_ == 0) {
        res.status = 0;
        lastResult_ = res;
        return res;
    }

    double s = slip;
    res.slip = s;
    res.torque = calculateTorque(s);
    res.P = calculateActivePower(s);
    res.Q = calculateReactivePower(s);
    res.I = calculateStatorCurrent(s);
    res.statorCurrent = calculateStatorCurrent(s);
    res.rotorCurrent = calculateRotorCurrent(s);
    res.magnetizingCurrent = calculateMagnetizingCurrent(s);
    res.efficiency = calculateEfficiency(s);
    res.powerFactor = calculatePowerFactor(s);
    res.speed = getActualSpeed(s);
    res.angularSpeed = 2.0 * IND_PI * res.speed / 60.0;
    res.Te = calculateTorqueNm(s);
    res.Tmech = res.Te;
    res.rotorLosses = calculateRotorLosses(s);
    res.statorLosses = calculateStatorLosses(s);
    res.electricalLosses = res.rotorLosses + res.statorLosses;
    res.mechanicalLosses = calculateMechanicalLosses(res.speed);
    res.outputPower = res.P;

    lastResult_ = res;
    return res;
}

InductionGeneratorResult
InductionGeneratorModel::calculateFromSpeed(double speedRpm) {
    setSpeed(speedRpm);
    return calculate(slip_);
}

InductionGeneratorResult
InductionGeneratorModel::calculateFromPower(double Pmech) {
    slip_ = solveSlipFromPower(Pmech);
    return calculate(slip_);
}

} // namespace powsys365
