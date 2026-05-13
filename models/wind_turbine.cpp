// ============================================================================
// wind_turbine.cpp
// Implementacion del modelo de turbina eolica IEC 61400-27-1 y WECC
// ============================================================================

#include "wind_turbine.h"
#include <stdexcept>
#include <numeric>

namespace powsys365 {

// ============================================================================
// Constructores
// ============================================================================
WindTurbineModel::WindTurbineModel()
    : type_(TYPE3_DFIG), params_(), opMode_(MODE_NORMAL),
      windSpeed_(10.0), pitchAngle_(0.0), omegaRotor_(1.0),
      omegaGen_(1.0), integralPitch_(0.0), integralSpeed_(0.0),
      externalResistance_(0.0), Vterminal_(1.0), crowbarActive_(false),
      status_(1) {
    lastOutput_ = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1};
}

WindTurbineModel::WindTurbineModel(TurbineType type,
                                    const WindTurbineParameters& params)
    : type_(type), params_(params), opMode_(MODE_NORMAL),
      windSpeed_(10.0), pitchAngle_(0.0), omegaRotor_(1.0),
      omegaGen_(1.0), integralPitch_(0.0), integralSpeed_(0.0),
      externalResistance_(0.0), Vterminal_(1.0), crowbarActive_(false),
      status_(1) {
    lastOutput_ = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1};
}

// ============================================================================
// Setters
// ============================================================================
void WindTurbineModel::setTurbineType(TurbineType type) { type_ = type; }
void WindTurbineModel::setTurbineParameters(const WindTurbineParameters& p) {
    params_ = p;
}
void WindTurbineModel::setWindSpeed(double windSpeed) {
    windSpeed_ = std::max(0.0, windSpeed);
}
void WindTurbineModel::setAirDensity(double rho) {
    params_.airDensity = std::max(0.5, std::min(2.0, rho));
}
void WindTurbineModel::setPitchAngle(double betaDeg) {
    pitchAngle_ = std::max(params_.minPitchAngle,
                            std::min(params_.maxPitchAngle, betaDeg));
}
void WindTurbineModel::setRotorSpeed(double omega) {
    omegaRotor_ = std::max(0.1, std::min(1.3, omega));
}
void WindTurbineModel::setTerminalVoltage(double Vpu) {
    Vterminal_ = Vpu;
}
void WindTurbineModel::setOperatingMode(OperatingMode mode) {
    opMode_ = mode;
}

// ============================================================================
// Modelo aerodinamico Cp(lambda, beta) - Modelo tipico IEC 61400-27-1
// Basado en la funcion de potencia de He (2009)
// ============================================================================
double WindTurbineModel::calculateCp(double lambda, double betaDeg) const {
    double beta = betaDeg; // en grados
    // Modelo estandarizado IEC 61400-27-1
    double lambda_i = 1.0 / (1.0 / (lambda + 0.08 * beta) -
                              0.035 / (beta * beta * beta + 1.0));
    double Cp = 0.5176 * (116.0 / lambda_i - 0.4 * beta - 5.0)
                * std::exp(-21.0 / lambda_i) + params_.cpOffset * lambda;
    return std::max(0.0, std::min(0.59, Cp));
}

double WindTurbineModel::calculateCpOptimal(double lambda) const {
    return calculateCp(lambda, 0.0);
}

// ============================================================================
// Tip Speed Ratio (TSR)
// ============================================================================
double WindTurbineModel::calculateTSR(double windSpeed) const {
    if (windSpeed < 0.1) return 0.0;
    double omega_rad_s = omegaRotor_ * params_.ratedSpeed
                         / params_.rotorDiameter * 2.0;
    double tipSpeed = omega_rad_s * (params_.rotorDiameter / 2.0);
    return tipSpeed / windSpeed;
}

double WindTurbineModel::calculateTSROptimal() const {
    return params_.tsrOpt;
}

// ============================================================================
// Potencia del viento incidente
// ============================================================================
double WindTurbineModel::calculateWindPower(double windSpeed) const {
    double A = getRotorArea();
    return 0.5 * params_.airDensity * A * windSpeed * windSpeed * windSpeed
           / 1e6; // MW
}

double WindTurbineModel::calculateMechanicalPower(double windSpeed,
                                                   double cp) const {
    double A = getRotorArea();
    return 0.5 * params_.airDensity * A * windSpeed * windSpeed * windSpeed
           * cp / 1e6; // MW
}

double WindTurbineModel::calculateMechanicalPower(double windSpeed,
                                                   double lambda,
                                                   double betaDeg) const {
    double cp = calculateCp(lambda, betaDeg);
    return calculateMechanicalPower(windSpeed, cp);
}

// ============================================================================
// Torque aerodinamico
// ============================================================================
double WindTurbineModel::calculateAerodynamicTorque(double windSpeed,
                                                      double omega,
                                                      double beta) const {
    double lambda = calculateTSR(windSpeed);
    double cp = calculateCp(lambda, beta);
    double Pw = 0.5 * params_.airDensity * getRotorArea()
                * std::pow(windSpeed, 3.0) * cp;
    if (omega < 0.01) return 0.0;
    double omegaRated = 2.0 * WT_PI * params_.baseFrequency / params_.gearRatio;
    double omegaActual = omega * omegaRated;
    return Pw / omegaActual; // Nm
}

// ============================================================================
// Resolver omega en estado estacionario
// ============================================================================
double WindTurbineModel::solveSteadyStateOmega(double windSpeed,
                                                 double beta) const {
    if (windSpeed < params_.cutInSpeed) return 0.0;
    // Para region 2 (maximo Cp): omega_opt = lambda_opt * V / R
    double omegaOpt = params_.tsrOpt * windSpeed / (params_.rotorDiameter / 2.0);
    double omegaRated = 2.0 * WT_PI * params_.baseFrequency / params_.gearRatio;
    double omegaPu = omegaOpt / omegaRated;
    // Limitar a omega nominal
    return std::min(1.0, omegaPu);
}

// ============================================================================
// Controladores
// ============================================================================
double WindTurbineModel::pitchController(double P_error, double dt) {
    integralPitch_ += P_error * dt;
    integralPitch_ = std::max(-10.0, std::min(10.0, integralPitch_));
    double pitchCmd = params_.kpPitch * P_error
                      + params_.kiPitch * integralPitch_;
    // Limitar rate
    double deltaPitch = pitchCmd - pitchAngle_;
    deltaPitch = std::max(-params_.pitchRateMax * dt,
                           std::min(params_.pitchRateMax * dt, deltaPitch));
    pitchAngle_ = std::max(params_.minPitchAngle,
                            std::min(params_.maxPitchAngle,
                                     pitchAngle_ + deltaPitch));
    return pitchAngle_;
}

double WindTurbineModel::speedController(double omega_error, double dt) {
    integralSpeed_ += omega_error * dt;
    integralSpeed_ = std::max(-5.0, std::min(5.0, integralSpeed_));
    return params_.kpSpeed * omega_error + params_.kiSpeed * integralSpeed_;
}

double WindTurbineModel::optimalTorqueControl(double windSpeed) {
    // K_opt = 0.5 * rho * A * R^3 * Cp_opt / lambda_opt^3
    double R = params_.rotorDiameter / 2.0;
    double K_opt = 0.5 * params_.airDensity * getRotorArea()
                   * R * R * R * params_.cpOpt
                   / std::pow(params_.tsrOpt, 3.0);
    double omegaRad = omegaRotor_ * 2.0 * WT_PI * params_.baseFrequency
                      / params_.gearRatio;
    return K_opt * omegaRad * omegaRad; // Torque optimo en Nm
}

// ============================================================================
// Generador de induccion: slip
// ============================================================================
double WindTurbineModel::calculateInductionGeneratorSlip(double Pmech) const {
    double R2 = 0.0;
    switch (type_) {
        case TYPE1_SQUIRREL_CAGE: R2 = params_.rotorResistanceT1; break;
        case TYPE2_WOUND_ROTOR:   R2 = params_.rotorResistanceT2
                                           + externalResistance_; break;
        case TYPE3_DFIG:          R2 = params_.rotorResistanceT3; break;
        default:                  R2 = params_.rotorResistanceT1; break;
    }
    double X2 = 0.0;
    switch (type_) {
        case TYPE1_SQUIRREL_CAGE: X2 = params_.rotorReactanceT1; break;
        case TYPE2_WOUND_ROTOR:   X2 = params_.rotorReactanceT2; break;
        case TYPE3_DFIG:          X2 = params_.rotorReactanceT3; break;
        default:                  X2 = params_.rotorReactanceT1; break;
    }
    double R1 = params_.statorResistance;
    double X1 = params_.statorReactance;
    double Xm = params_.magnetizingReactance;

    // Aproximacion: s = R2 * P / (omega_s * (V^2 / (R1 + R2/s)^2 + (X1+X2)^2))
    // Metodo iterativo simplificado
    double V = Vterminal_;
    double s_guess = -R2 * Pmech / params_.ratedPower;
    s_guess = std::max(-0.3, std::min(0.1, s_guess));

    // Iteracion de Newton-Raphson (3 iteraciones)
    for (int i = 0; i < 3; ++i) {
        double denom = std::pow(R1 + R2 / s_guess, 2.0)
                       + std::pow(X1 + X2, 2.0);
        double f = Pmech / params_.ratedPower
                   - V * V * R2 / s_guess / denom;
        double dfd_s = -V * V * R2
                       * (std::pow(R1 + R2 / s_guess, 2.0)
                          + std::pow(X1 + X2, 2.0)
                          - 2.0 * R2 / s_guess / s_guess
                            * (R1 + R2 / s_guess))
                       / (s_guess * s_guess
                          * denom * denom);
        if (std::abs(dfd_s) > 1e-10) {
            s_guess -= f / dfd_s;
        }
    }
    return std::max(-0.5, std::min(0.5, s_guess));
}

double WindTurbineModel::calculateInductionGeneratorTorque(double slip) const {
    double R2 = 0.0, X2 = 0.0;
    switch (type_) {
        case TYPE1_SQUIRREL_CAGE: R2 = params_.rotorResistanceT1;
                                   X2 = params_.rotorReactanceT1; break;
        case TYPE2_WOUND_ROTOR:   R2 = params_.rotorResistanceT2
                                           + externalResistance_;
                                   X2 = params_.rotorReactanceT2; break;
        case TYPE3_DFIG:          R2 = params_.rotorResistanceT3;
                                   X2 = params_.rotorReactanceT3; break;
        default:                  R2 = params_.rotorResistanceT1;
                                   X2 = params_.rotorReactanceT1; break;
    }
    double R1 = params_.statorResistance;
    double X1 = params_.statorReactance;
    double V = Vterminal_;
    double s = slip;
    if (std::abs(s) < 1e-6) s = (s >= 0) ? 1e-6 : -1e-6;

    double denom = std::pow(R1 + R2 / s, 2.0) + std::pow(X1 + X2, 2.0);
    double T = V * V * R2 / s / denom; // pu torque
    return T;
}

double WindTurbineModel::calculateInductionGeneratorQ(double P,
                                                       double slip) const {
    double R2 = 0.0, X2 = 0.0;
    switch (type_) {
        case TYPE1_SQUIRREL_CAGE: R2 = params_.rotorResistanceT1;
                                   X2 = params_.rotorReactanceT1; break;
        case TYPE2_WOUND_ROTOR:   R2 = params_.rotorResistanceT2
                                           + externalResistance_;
                                   X2 = params_.rotorReactanceT2; break;
        case TYPE3_DFIG:          R2 = params_.rotorResistanceT3;
                                   X2 = params_.rotorReactanceT3; break;
        default:                  R2 = params_.rotorResistanceT1;
                                   X2 = params_.rotorReactanceT1; break;
    }
    double R1 = params_.statorResistance;
    double X1 = params_.statorReactance;
    double Xm = params_.magnetizingReactance;
    double V = Vterminal_;
    double s = slip;
    if (std::abs(s) < 1e-6) s = (s >= 0) ? 1e-6 : -1e-6;

    double denom = std::pow(R1 + R2 / s, 2.0) + std::pow(X1 + X2, 2.0);
    double Q = V * V * (X1 + X2 + (R1 + R2 / s) * (R1 + R2 / s) / Xm)
               / denom;
    return Q; // MVAr (positivo = consumo de reactiva)
}

double WindTurbineModel::calculateStatorCurrent(double P, double Q,
                                                 double V) const {
    if (V < 1e-6) return 0.0;
    double S = std::sqrt(P * P + Q * Q);
    return S / V; // pu
}

// ============================================================================
// LVRT - Low Voltage Ride Through
// ============================================================================
bool WindTurbineModel::checkLVRT(double Vpu, double duration) const {
    if (Vpu >= 0.9) return true;                        // Operacion normal
    if (Vpu >= 0.5 && duration <= 10.0) return true;    // NERC/WECC
    if (Vpu >= 0.3 && duration <= 0.625) return true;   // Dip corto
    if (Vpu >= params_.lvrtVmin && duration <= params_.lvrtTripTime)
        return true;                                    // Dip profundo
    return false;                                       // Disparo
}

bool WindTurbineModel::isLVRTActive(double Vpu) const {
    return Vpu < params_.lvrtVthreshold;
}

double WindTurbineModel::calculateLVRTReactiveSupport(double Vpu) const {
    if (Vpu >= params_.lvrtVthreshold) return 0.0;
    double dV = params_.lvrtVthreshold - Vpu;
    return params_.lvrtReactiveSupport * dV; // pu de potencia reactiva
}

double WindTurbineModel::calculateLVRTActivePowerReduction(double Vpu) const {
    if (Vpu >= params_.lvrtVthreshold) return 1.0;
    return params_.lvrtPfactor; // factor de reduccion de P
}

// ============================================================================
// Modelo electrico Type 1: Jaula de ardilla + capacitor
// ============================================================================
ElectricalOutput WindTurbineModel::calculateType1Output(double Pmech) {
    ElectricalOutput out;
    double Ploss = 0.05 * Pmech; // 5% perdidas
    out.P = std::max(0.0, std::min(params_.ratedPower, Pmech - Ploss));

    double slip = calculateInductionGeneratorSlip(out.P);
    out.slip = slip;

    // Potencia reactiva: consumo por el generador de induccion
    double Qgen = calculateInductionGeneratorQ(out.P, slip);

    // Compensacion por capacitor
    double Qcap = params_.capacitorCompensation * params_.ratedPower;

    out.Q = Qgen - Qcap; // Positivo = consumo neto, negativo = inyeccion
    out.Vterminal = Vterminal_;
    out.Iterminal = calculateStatorCurrent(out.P, std::abs(out.Q), Vterminal_);
    out.omegaRotor = omegaRotor_;
    out.omegaGen = omegaGen_;
    out.pitchAngle = pitchAngle_;
    out.status = status_;

    return out;
}

// ============================================================================
// Modelo electrico Type 2: Rotor bobinado + resistencia variable
// ============================================================================
ElectricalOutput WindTurbineModel::calculateType2Output(double Pmech) {
    ElectricalOutput out;
    double Ploss = 0.04 * Pmech; // 4% perdidas
    out.P = std::max(0.0, std::min(params_.ratedPower, Pmech - Ploss));

    // Ajuste de resistencia externa para control de velocidad
    double omegaOpt = solveSteadyStateOmega(windSpeed_, pitchAngle_);
    double omegaError = omegaOpt - omegaRotor_;
    if (windSpeed_ < params_.ratedSpeed) {
        externalResistance_ = std::max(0.0,
            std::min(params_.externalResistanceMax,
                     -omegaError * params_.externalResistanceMax));
    } else {
        externalResistance_ = 0.0;
    }

    double slip = calculateInductionGeneratorSlip(out.P);
    out.slip = slip;

    double Qgen = calculateInductionGeneratorQ(out.P, slip);
    double Qcap = params_.capacitorCompensation * 0.5 * params_.ratedPower;
    out.Q = Qgen - Qcap;
    out.Vterminal = Vterminal_;
    out.Iterminal = calculateStatorCurrent(out.P, std::abs(out.Q), Vterminal_);
    out.omegaRotor = omegaRotor_;
    out.omegaGen = omegaGen_;
    out.pitchAngle = pitchAngle_;
    out.status = status_;

    return out;
}

// ============================================================================
// Modelo electrico Type 3: DFIG
// ============================================================================
ElectricalOutput WindTurbineModel::calculateType3Output(double Pmech) {
    ElectricalOutput out;
    double Ploss = 0.03 * Pmech; // 3% perdidas (converter + generador)
    out.P = std::max(0.0, std::min(params_.ratedPower, Pmech - Ploss));

    // DFIG: slip range tipico [-0.3, 0.3]
    double slip = calculateInductionGeneratorSlip(out.P);
    out.slip = slip;

    // El DFIG puede controlar Q independientemente via RSC
    // RSC controla P y Q del rotor
    // Potencia del rotor: Pr = -s * Ps (aproximadamente)
    double P_rotor = -slip * out.P;

    // GSC (Grid Side Converter) controla Vdc y Q
    // Q disponible del DFIG: controlado por el RSC
    double Q_dfig = -out.P * 0.3; // Puede inyectar reactivo (tipico)
    out.Q = Q_dfig;

    // LVRT: crowbar activation
    if (Vterminal_ < 0.5) {
        crowbarActive_ = true;
        out.P *= 0.5;
        out.Q = calculateLVRTReactiveSupport(Vterminal_);
    } else {
        crowbarActive_ = false;
    }

    out.Vterminal = Vterminal_;
    out.Iterminal = calculateStatorCurrent(out.P, std::abs(out.Q), Vterminal_);
    out.omegaRotor = omegaRotor_;
    out.omegaGen = omegaGen_;
    out.pitchAngle = pitchAngle_;
    out.status = status_;

    return out;
}

// ============================================================================
// Modelo electrico Type 4: Full Converter
// ============================================================================
ElectricalOutput WindTurbineModel::calculateType4Output(double Pmech) {
    ElectricalOutput out;
    double Ploss = 0.02 * Pmech; // 2% perdidas (generador + converter)
    out.P = std::max(0.0, std::min(params_.ratedPower, Pmech - Ploss));

    // Full converter: P y Q son controlados independientemente
    // El generador puede ser sincrono (PMSG) o asincrono
    out.slip = 0.0; // Desacoplado electricamente

    // Control de Q: se puede ajustar segun el voltaje terminal
    if (Vterminal_ < 0.95) {
        out.Q = 0.44 * out.P; // Soporte capacitivo (inyeccion de Q)
    } else if (Vterminal_ > 1.05) {
        out.Q = -0.44 * out.P; // Soporte inductivo (absorcion de Q)
    } else {
        out.Q = 0.0; // Unity power factor
    }

    // LVRT: inyectar Q maximo
    if (isLVRTActive(Vterminal_)) {
        out.P *= calculateLVRTActivePowerReduction(Vterminal_);
        out.Q = calculateLVRTReactiveSupport(Vterminal_);
    }

    out.Vterminal = Vterminal_;
    out.Iterminal = calculateStatorCurrent(out.P, std::abs(out.Q), Vterminal_);
    out.omegaRotor = omegaRotor_;
    out.omegaGen = omegaGen_;
    out.pitchAngle = pitchAngle_;
    out.status = status_;

    return out;
}

// ============================================================================
// Calculo general de salida electrica
// ============================================================================
ElectricalOutput WindTurbineModel::calculateElectricalOutput() {
    if (status_ == 0 || windSpeed_ < params_.cutInSpeed
        || windSpeed_ > params_.cutOutSpeed) {
        lastOutput_ = {0.0, 0.0, Vterminal_, 0.0, 0.0, omegaRotor_, omegaGen_,
                       pitchAngle_, 0};
        return lastOutput_;
    }

    // Calcular lambda y Cp
    double lambda = calculateTSR(windSpeed_);
    double cp = calculateCp(lambda, pitchAngle_);

    // Potencia mecanica
    double Pmech = calculateMechanicalPower(windSpeed_, cp);
    Pmech = std::min(Pmech, params_.ratedPower * 1.1); // 10% margen

    // Seleccionar modelo segun tipo
    switch (type_) {
        case TYPE1_SQUIRREL_CAGE:
            lastOutput_ = calculateType1Output(Pmech);
            break;
        case TYPE2_WOUND_ROTOR:
            lastOutput_ = calculateType2Output(Pmech);
            break;
        case TYPE3_DFIG:
            lastOutput_ = calculateType3Output(Pmech);
            break;
        case TYPE4_FULL_CONVERTER:
            lastOutput_ = calculateType4Output(Pmech);
            break;
    }

    // LVRT
    if (isLVRTActive(Vterminal_)) {
        opMode_ = MODE_LVRT;
    } else {
        opMode_ = MODE_NORMAL;
    }

    return lastOutput_;
}

// ============================================================================
// Regiones de operacion
// ============================================================================
int WindTurbineModel::getOperatingRegion(double windSpeed) const {
    if (windSpeed < params_.cutInSpeed) return 1;     // Parada
    if (windSpeed < params_.ratedSpeed * 0.7) return 2; // Region suboptima
    if (windSpeed < params_.ratedSpeed) return 3;     // Region Cp maximo
    if (windSpeed < params_.ratedSpeed * 1.1) return 4; // Region transicion
    if (windSpeed < params_.cutOutSpeed) return 5;    // Region de pitch
    return 1;                                          // Cut-out
}

// ============================================================================
// Curva de potencia completa
// ============================================================================
std::vector<PowerCurvePoint> WindTurbineModel::getPowerCurve(
    double windSpeedMin, double windSpeedMax, double windSpeedStep) {
    std::vector<PowerCurvePoint> curve;
    double savedWindSpeed = windSpeed_;
    double savedPitch = pitchAngle_;
    double savedOmega = omegaRotor_;

    for (double v = windSpeedMin; v <= windSpeedMax; v += windSpeedStep) {
        windSpeed_ = v;
        omegaRotor_ = solveSteadyStateOmega(v, pitchAngle_);
        double lambda = calculateTSR(v);
        double cp = calculateCp(lambda, pitchAngle_);
        double Pmech = calculateMechanicalPower(v, cp);

        PowerCurvePoint pt;
        pt.windSpeed = v;
        pt.power = std::min(Pmech, params_.ratedPower);
        pt.cp = cp;
        pt.tsr = lambda;
        pt.pitchAngle = pitchAngle_;
        curve.push_back(pt);
    }

    windSpeed_ = savedWindSpeed;
    pitchAngle_ = savedPitch;
    omegaRotor_ = savedOmega;
    return curve;
}

std::vector<PowerCurvePoint> WindTurbineModel::getPowerCurveOptimal(
    double windSpeedMin, double windSpeedMax, double windSpeedStep) {
    std::vector<PowerCurvePoint> curve;
    double savedWindSpeed = windSpeed_;
    double savedPitch = pitchAngle_;
    double savedOmega = omegaRotor_;

    pitchAngle_ = 0.0; // Optimo sin pitch

    for (double v = windSpeedMin; v <= windSpeedMax; v += windSpeedStep) {
        windSpeed_ = v;
        omegaRotor_ = solveSteadyStateOmega(v, 0.0);
        double lambda = calculateTSR(v);
        double cp = calculateCp(lambda, 0.0);
        double Pmech = calculateMechanicalPower(v, cp);

        PowerCurvePoint pt;
        pt.windSpeed = v;
        pt.power = std::min(Pmech, params_.ratedPower);
        pt.cp = cp;
        pt.tsr = lambda;
        pt.pitchAngle = 0.0;
        curve.push_back(pt);
    }

    windSpeed_ = savedWindSpeed;
    pitchAngle_ = savedPitch;
    omegaRotor_ = savedOmega;
    return curve;
}

// ============================================================================
// Simulacion paso a paso
// ============================================================================
void WindTurbineModel::step(double dt, double windSpeed, double Vterminal) {
    windSpeed_ = windSpeed;
    Vterminal_ = Vterminal;

    if (windSpeed_ < params_.cutInSpeed || windSpeed_ > params_.cutOutSpeed) {
        status_ = 0;
        lastOutput_ = {0.0, 0.0, Vterminal, 0.0, 0.0, omegaRotor_, omegaGen_,
                       pitchAngle_, 0};
        return;
    }

    status_ = 1;

    // LVRT check
    bool lvrtActive = isLVRTActive(Vterminal);
    if (lvrtActive) {
        opMode_ = MODE_LVRT;
    } else {
        opMode_ = MODE_NORMAL;
    }

    // Calcular potencia mecanica
    double lambda = calculateTSR(windSpeed_);
    double cp = calculateCp(lambda, pitchAngle_);
    double Pm = calculateMechanicalPower(windSpeed_, cp);

    // Control de pitch para region de potencia nominal
    if (Pm > params_.ratedPower && (type_ == TYPE3_DFIG
                                    || type_ == TYPE4_FULL_CONVERTER)) {
        double P_error = params_.ratedPower - Pm;
        pitchAngle_ = pitchController(P_error, dt);
    } else if (windSpeed_ < params_.ratedSpeed) {
        pitchAngle_ = 0.0; // Optimo Cp
    }

    // Actualizar velocidad del rotor (ecuacion del movimiento)
    double Ta = calculateAerodynamicTorque(windSpeed_, omegaRotor_, pitchAngle_);
    double omegaRated = 2.0 * WT_PI * params_.baseFrequency;
    double Te = Pm * 1e6 / (omegaRotor_ * omegaRated); // Torque electrico
    double dOmega = (Ta - Te) * dt / (2.0 * params_.driveTrainInertia);
    omegaRotor_ += dOmega;
    omegaRotor_ = std::max(0.3, std::min(1.2, omegaRotor_));

    // Calcular salida electrica segun tipo
    lastOutput_ = calculateElectricalOutput();

    // Aplicar LVRT si activo
    if (lvrtActive) {
        lastOutput_.P *= calculateLVRTActivePowerReduction(Vterminal);
        lastOutput_.Q = calculateLVRTReactiveSupport(Vterminal);
    }
}

// Variable interna para step (declarada en .h como Pout_/Qout_)
// Usamos lastOutput_.P/Q en su lugar

} // namespace powsys365
