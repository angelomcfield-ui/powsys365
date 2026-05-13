// ============================================================================
// facts.cpp
// Implementacion de modelos FACTS: SVC, STATCOM, TCSC, UPFC, MMC
// ============================================================================

#include "facts.h"
#include <numeric>
#include <algorithm>

// Constantes matematicas locales para este modulo
namespace {
    constexpr double FACTS_PI = 3.14159265358979323846;
    constexpr double FACTS_SQRT2 = 1.4142135623730951;
    constexpr double FACTS_SQRT3 = 1.7320508075688772;
    constexpr double FACTS_SQRT6 = 2.449489742783178;
}

namespace powsys365 {

// ============================================================================
// SVCModel - Static Var Compensator
// ============================================================================

SVCModel::SVCModel()
    : id_(0), busId_(0), Qmax_(100.0), Qmin_(-10.0), Vref_(1.0),
      Vmeasured_(1.0), Kv_(100.0), Tc_(0.05), Bsvc_(0.0),
      BsvcMax_(1.0), BsvcMin_(-1.0), Btcr_(0.0), Btsc_(0.0),
      Qout_(0.0), status_(1), mode_(FACTS_MODE_VOLTAGE_REGULATION),
      BtcrMin_(-1.0), BtcrMax_(0.0) {
    tscSteps_ = {0.1, 0.1, 0.1, 0.1, 0.1};
    tscState_.resize(tscSteps_.size(), false);
}

SVCModel::SVCModel(int id, int busId, double Qmax, double Qmin)
    : id_(id), busId_(busId), Qmax_(Qmax), Qmin_(Qmin), Vref_(1.0),
      Vmeasured_(1.0), Kv_(100.0), Tc_(0.05), Bsvc_(0.0),
      BsvcMax_(Qmax / 100.0), BsvcMin_(Qmin / 100.0), Btcr_(0.0),
      Btsc_(0.0), Qout_(0.0), status_(1),
      mode_(FACTS_MODE_VOLTAGE_REGULATION), BtcrMin_(-1.0), BtcrMax_(0.0) {
    tscSteps_ = {0.1, 0.1, 0.1, 0.1, 0.1};
    tscState_.resize(tscSteps_.size(), false);
}

void SVCModel::setOperatingMode(FACTSOperatingMode mode) { mode_ = mode; }
void SVCModel::setVoltageReference(double Vref) { Vref_ = Vref; }
void SVCModel::setVoltageMeasured(double Vmeas) { Vmeasured_ = Vmeas; }
void SVCModel::setGain(double Kv) { Kv_ = Kv; }
void SVCModel::setTimeConstant(double Tc) { Tc_ = Tc; }
void SVCModel::setTCRLimits(double Bmax, double Bmin) {
    BtcrMax_ = Bmax;
    BtcrMin_ = Bmin;
}
void SVCModel::setTSCCapacitorSteps(const std::vector<double>& steps) {
    tscSteps_ = steps;
    tscState_.resize(steps.size(), false);
}
void SVCModel::setStatus(int status) { status_ = status; }

double SVCModel::calculateBref() {
    if (status_ == 0) return 0.0;
    double error = Vref_ - Vmeasured_;
    double Bref = Kv_ * error; // pu
    return std::max(BsvcMin_, std::min(BsvcMax_, Bref));
}

std::vector<bool> SVCModel::selectTSCSteps(double Bneeded) {
    std::fill(tscState_.begin(), tscState_.end(), false);
    double B_accum = 0.0;
    // Greedily select largest steps first
    std::vector<size_t> indices(tscSteps_.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(),
        [this](size_t a, size_t b) {
            return tscSteps_[a] > tscSteps_[b];
        });
    for (size_t idx : indices) {
        if (B_accum + tscSteps_[idx] <= Bneeded + 1e-6) {
            tscState_[idx] = true;
            B_accum += tscSteps_[idx];
        }
    }
    return tscState_;
}

double SVCModel::calculateBsvc() {
    double Bref = calculateBref();
    // Componente TSC (capacitivo, positivo)
    std::vector<bool> steps = selectTSCSteps(std::max(0.0, Bref));
    Btsc_ = 0.0;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i]) Btsc_ += tscSteps_[i];
    }
    // Componente TCR (inductivo, negativo)
    double B_residual = Bref - Btsc_;
    if (B_residual < 0) {
        Btcr_ = std::max(BtcrMin_, B_residual);
    } else {
        Btcr_ = 0.0; // TCR off when no inductive needed
    }
    Bsvc_ = Btsc_ + Btcr_;
    return Bsvc_;
}

double SVCModel::calculateInjection() {
    if (status_ == 0) {
        Qout_ = 0.0;
        return 0.0;
    }
    calculateBsvc();
    // Q = B * V^2 (en pu base de 100 MVA)
    double Q_pu = Bsvc_ * Vmeasured_ * Vmeasured_;
    Qout_ = Q_pu * 100.0; // Escalar a MVAr
    Qout_ = std::max(Qmin_, std::min(Qmax_, Qout_));
    return Qout_;
}

FACTSOperatingPoint SVCModel::getOperatingPoint() const {
    FACTSOperatingPoint op = {};
    op.Q = Qout_;
    op.V = Vmeasured_;
    op.Bsvc = Bsvc_;
    op.mode = mode_;
    op.status = status_;
    return op;
}

// ============================================================================
// STATCOMModel - Static Synchronous Compensator
// ============================================================================

STATCOMModel::STATCOMModel()
    : id_(0), busId_(0), rating_(100.0), Vref_(1.0), Vmeasured_(1.0),
      Qref_(0.0), Rsource_(0.01), Xsource_(0.15), carrierFreq_(2000.0),
      modIndex_(0.9), Imax_(1.2), Qout_(0.0), Vsource_(1.0),
      thetaSource_(0.0), Iout_(0.0), status_(1),
      mode_(FACTS_MODE_VOLTAGE_REGULATION), integralError_(0.0) {}

STATCOMModel::STATCOMModel(int id, int busId, double rating)
    : id_(id), busId_(busId), rating_(rating), Vref_(1.0),
      Vmeasured_(1.0), Qref_(0.0), Rsource_(0.01), Xsource_(0.15),
      carrierFreq_(2000.0), modIndex_(0.9), Imax_(1.2), Qout_(0.0),
      Vsource_(1.0), thetaSource_(0.0), Iout_(0.0), status_(1),
      mode_(FACTS_MODE_VOLTAGE_REGULATION), integralError_(0.0) {}

void STATCOMModel::setOperatingMode(FACTSOperatingMode mode) { mode_ = mode; }
void STATCOMModel::setVoltageReference(double Vref) { Vref_ = Vref; }
void STATCOMModel::setVoltageMeasured(double Vmeas) { Vmeasured_ = Vmeas; }
void STATCOMModel::setReactiveReference(double Qref) { Qref_ = Qref; }
void STATCOMModel::setSourceImpedance(double R, double X) {
    Rsource_ = R; Xsource_ = X;
}
void STATCOMModel::setPWMParams(double carrierFreq, double modIndex) {
    carrierFreq_ = carrierFreq;
    modIndex_ = modIndex;
}
void STATCOMModel::setCurrentLimits(double Imax) { Imax_ = Imax; }
void STATCOMModel::setStatus(int status) { status_ = status; }

double STATCOMModel::calculateInternalVoltage(double Q) const {
    // Vsource = sqrt((Vbus + Q*X/Vbus)^2 + (P*X/Vbus)^2)
    double Vbus = Vmeasured_;
    if (Vbus < 1e-6) return Vref_;
    double P = 0.0; // STATCOM no consume/inyecta P activo (solo perdidas)
    double Ploss = Rsource_ * Iout_ * Iout_; // Perdidas
    double Vsource_sq = (Vbus + Q * Xsource_ / Vbus) * (Vbus + Q * Xsource_ / Vbus)
                        + (Ploss * Xsource_ / Vbus) * (Ploss * Xsource_ / Vbus);
    return std::sqrt(Vsource_sq);
}

double STATCOMModel::calculateInternalAngle(double P, double Q) const {
    double Vbus = Vmeasured_;
    if (Vbus < 1e-6) return 0.0;
    double delta = std::atan(P * Xsource_ / (Vbus * Vbus + Q * Xsource_));
    return delta;
}

double STATCOMModel::calculateInjection() {
    if (status_ == 0) {
        Qout_ = 0.0;
        Vsource_ = Vmeasured_;
        Iout_ = 0.0;
        return 0.0;
    }

    double Verror = Vref_ - Vmeasured_;
    integralError_ += Verror * 0.01; // dt integrativo
    integralError_ = std::max(-1.0, std::min(1.0, integralError_));

    // Control PI de voltaje
    double Kp = 2.0;
    double Ki = 50.0;
    double deltaV = Kp * Verror + Ki * integralError_;

    Vsource_ = Vmeasured_ + deltaV;
    Vsource_ = std::max(0.5, std::min(1.5, Vsource_));

    thetaSource_ = calculateInternalAngle(0.0, Qref_);

    // Corriente de inyeccion
    double dV = Vsource_ - Vmeasured_;
    double Z = std::sqrt(Rsource_ * Rsource_ + Xsource_ * Xsource_);
    if (Z < 1e-6) Z = Xsource_;
    Iout_ = dV / Z;
    Iout_ = std::max(-Imax_, std::min(Imax_, Iout_));

    Qout_ = calculateReactivePower();
    return Qout_;
}

double STATCOMModel::calculateReactivePower() {
    // Q = Vbus * (Vbus - Vsource*cos(theta)) / X + Vbus * Vsource*sin(theta) * R / (X^2+R^2)
    double Vbus = Vmeasured_;
    double Z2 = Rsource_ * Rsource_ + Xsource_ * Xsource_;
    if (Z2 < 1e-12) {
        Qout_ = 0.0;
        return Qout_;
    }
    // Q aproximado: Q = Vbus * (Vbus - Vsource) / X para theta pequeno
    double Q_pu = Vbus * (Vbus - Vsource_ * std::cos(thetaSource_)) / Xsource_;
    Qout_ = Q_pu * rating_;
    Qout_ = std::max(-rating_, std::min(rating_, Qout_));
    return Qout_;
}

double STATCOMModel::calculateVoltageSource() {
    return Vsource_;
}

double STATCOMModel::calculateCurrentInjection() {
    return Iout_;
}

FACTSOperatingPoint STATCOMModel::getOperatingPoint() const {
    FACTSOperatingPoint op = {};
    op.Q = Qout_;
    op.V = Vmeasured_;
    op.Vsh = Vsource_;
    op.thetaSh = thetaSource_;
    op.I = Iout_;
    op.modulation = modIndex_;
    op.mode = mode_;
    op.status = status_;
    return op;
}

// ============================================================================
// TCSCModel - Thyristor Controlled Series Capacitor
// ============================================================================

TCSCModel::TCSCModel()
    : id_(0), fromBus_(0), toBus_(1), Xnominal_(5.0), Xmin_(1.0),
      Xmax_(15.0), Xtcsc_(5.0), Xeff_(5.0), Xc_(5.0), compLevel_(0.7),
      Pref_(100.0), Pmeas_(100.0), alphaDeg_(165.0), alphaMin_(145.0),
      alphaMax_(180.0), status_(1), mode_(FACTS_MODE_IMPEDANCE_CONTROL) {}

TCSCModel::TCSCModel(int id, int fromBus, int toBus, double Xnominal,
                      double Xmin, double Xmax)
    : id_(id), fromBus_(fromBus), toBus_(toBus), Xnominal_(Xnominal),
      Xmin_(Xmin), Xmax_(Xmax), Xtcsc_(Xnominal), Xeff_(Xnominal),
      Xc_(Xnominal), compLevel_(0.7), Pref_(100.0), Pmeas_(100.0),
      alphaDeg_(165.0), alphaMin_(145.0), alphaMax_(180.0),
      status_(1), mode_(FACTS_MODE_IMPEDANCE_CONTROL) {}

void TCSCModel::setOperatingMode(FACTSOperatingMode mode) { mode_ = mode; }
void TCSCModel::setCompensationLevel(double comp) {
    compLevel_ = std::max(0.0, std::min(1.0, comp));
    Xeff_ = Xc_ * (1.0 - compLevel_);
    Xeff_ = std::max(Xmin_, std::min(Xmax_, Xeff_));
}
void TCSCModel::setReactance(double Xtcsc) {
    Xtcsc_ = std::max(Xmin_, std::min(Xmax_, Xtcsc));
    Xeff_ = Xtcsc_;
}
void TCSCModel::setPowerFlowReference(double Pref) { Pref_ = Pref; }
void TCSCModel::setPowerFlowMeasured(double Pmeas) { Pmeas_ = Pmeas; }
void TCSCModel::setFiringAngle(double alphaDeg) {
    alphaDeg_ = std::max(alphaMin_, std::min(alphaMax_, alphaDeg));
}
void TCSCModel::setStatus(int status) { status_ = status; }

// Modelo TCSC: Xtcsc como funcion del firing angle
// En region capacitiva (145-155): X aumenta con alpha
// En region inductiva (155-180): X cambia signo
// X(alpha) = Xc * (pi / (pi - 2*alpha - sin(2*alpha)))
double TCSCModel::calculateXfromFiringAngle(double alphaDeg) const {
    double alpha = alphaDeg * FACTS_PI / 180.0;
    double sigma = FACTS_PI - 2.0 * alpha;
    if (std::abs(sigma) < 1e-6) return Xc_;
    double sin2a = std::sin(2.0 * alpha);
    double denom = FACTS_PI - 2.0 * alpha - sin2a;
    if (std::abs(denom) < 1e-9) return Xc_;
    double k = FACTS_PI / denom;
    double X = Xc_ * (1.0 - k * (1.0 + 2.0 / (FACTS_PI - 2.0 * alpha)));
    // Simplificado: model tipico de X_TCSC vs alpha
    // Region capacitiva
    if (alphaDeg >= 145.0 && alphaDeg <= 155.0) {
        double t = (alphaDeg - 145.0) / 10.0; // 0 a 1
        return Xc_ * (1.0 + 2.0 * t); // Xc a 3*Xc
    } else if (alphaDeg > 155.0 && alphaDeg <= 180.0) {
        double t = (alphaDeg - 155.0) / 25.0; // 0 a 1
        return Xc_ * (3.0 - 4.0 * t); // 3*Xc a -Xc (inductivo)
    }
    return Xc_;
}

double TCSCModel::calculateFiringAngleFromX(double Xdesired) const {
    // Busqueda binaria simple
    double alpha_lo = alphaMin_, alpha_hi = alphaMax_;
    double Xlo = calculateXfromFiringAngle(alpha_lo);
    double Xhi = calculateXfromFiringAngle(alpha_hi);
    for (int i = 0; i < 20; ++i) {
        double alpha_mid = (alpha_lo + alpha_hi) / 2.0;
        double Xmid = calculateXfromFiringAngle(alpha_mid);
        if ((Xlo - Xdesired) * (Xmid - Xdesired) < 0) {
            alpha_hi = alpha_mid;
            Xhi = Xmid;
        } else {
            alpha_lo = alpha_mid;
            Xlo = Xmid;
        }
    }
    return (alpha_lo + alpha_hi) / 2.0;
}

double TCSCModel::calculateReactance() {
    if (status_ == 0) {
        Xeff_ = 0.0;
        return 0.0;
    }
    switch (mode_) {
        case FACTS_MODE_IMPEDANCE_CONTROL:
            Xeff_ = Xtcsc_;
            break;
        case FACTS_MODE_POWER_FLOW_CONTROL: {
            // Ajustar X para alcanzar Pref
            double dP = Pref_ - Pmeas_;
            double K = 0.1; // Ganancia
            Xeff_ += K * dP;
            Xeff_ = std::max(Xmin_, std::min(Xmax_, Xeff_));
            alphaDeg_ = calculateFiringAngleFromX(Xeff_);
            break;
        }
        default:
            Xeff_ = Xtcsc_;
            break;
    }
    return Xeff_;
}

double TCSCModel::calculateEffectiveX() {
    return calculateReactance();
}

double TCSCModel::calculateCompensationLevel() const {
    if (std::abs(Xc_) < 1e-6) return 0.0;
    return 1.0 - Xeff_ / Xc_;
}

double TCSCModel::calculatePowerFlowControl() {
    calculateReactance();
    return Pref_;
}

FACTSOperatingPoint TCSCModel::getOperatingPoint() const {
    FACTSOperatingPoint op = {};
    op.Xtcsc = Xeff_;
    op.mode = mode_;
    op.status = status_;
    return op;
}

// ============================================================================
// UPFCModel - Unified Power Flow Controller
// ============================================================================

UPFCModel::UPFCModel()
    : id_(0), fromBus_(0), toBus_(1), seriesRating_(100.0),
      shuntRating_(100.0), Vser_(0.0), thetaSer_(0.0), Vsh_(1.0),
      thetaSh_(0.0), Vref_(1.0), thetaRef_(0.0), Xref_(5.0),
      Pref_(100.0), Qref_(50.0), Kv_(10.0), Ktheta_(5.0), Kx_(2.0),
      Qser_(0.0), Qsh_(0.0), Ptransferred_(0.0), status_(1),
      mode_(FACTS_MODE_UNIFIED_CONTROL) {}

UPFCModel::UPFCModel(int id, int fromBus, int toBus,
                      double seriesRating, double shuntRating)
    : id_(id), fromBus_(fromBus), toBus_(toBus),
      seriesRating_(seriesRating), shuntRating_(shuntRating),
      Vser_(0.0), thetaSer_(0.0), Vsh_(1.0), thetaSh_(0.0),
      Vref_(1.0), thetaRef_(0.0), Xref_(5.0), Pref_(100.0),
      Qref_(50.0), Kv_(10.0), Ktheta_(5.0), Kx_(2.0),
      Qser_(0.0), Qsh_(0.0), Ptransferred_(0.0), status_(1),
      mode_(FACTS_MODE_UNIFIED_CONTROL) {}

void UPFCModel::setOperatingMode(FACTSOperatingMode mode) { mode_ = mode; }
void UPFCModel::setSeriesVoltage(double Vser, double thetaSer) {
    Vser_ = std::max(-seriesRating_ / 100.0, std::min(seriesRating_ / 100.0, Vser));
    thetaSer_ = thetaSer;
}
void UPFCModel::setShuntVoltage(double Vsh, double thetaSh) {
    Vsh_ = Vsh;
    thetaSh_ = thetaSh;
}
void UPFCModel::setVoltageReference(double Vref) { Vref_ = Vref; }
void UPFCModel::setAngleReference(double thetaref) { thetaRef_ = thetaref; }
void UPFCModel::setImpedanceReference(double Xref) { Xref_ = Xref; }
void UPFCModel::setPowerReferences(double Pref, double Qref) {
    Pref_ = Pref; Qref_ = Qref;
}
void UPFCModel::setSeriesRating(double rating) { seriesRating_ = rating; }
void UPFCModel::setShuntRating(double rating) { shuntRating_ = rating; }
void UPFCModel::setStatus(int status) { status_ = status; }

void UPFCModel::setVoltageControl(double Vref, double Kv) {
    Vref_ = Vref; Kv_ = Kv;
}
void UPFCModel::setAngleControl(double thetaref, double Ktheta) {
    thetaRef_ = thetaref; Ktheta_ = Ktheta;
}
void UPFCModel::setImpedanceControl(double Xref, double Kx) {
    Xref_ = Xref; Kx_ = Kx;
}

double UPFCModel::calculateSeriesInjection() {
    if (status_ == 0) return 0.0;
    // Vser para control de angulo y potencia
    double thetaError = thetaRef_ - thetaSer_;
    Vser_ = Ktheta_ * thetaError;
    Vser_ = std::max(-seriesRating_ / 100.0,
                      std::min(seriesRating_ / 100.0, Vser_));
    return Vser_;
}

double UPFCModel::calculateShuntInjection() {
    if (status_ == 0) return 0.0;
    double Verror = Vref_ - Vsh_;
    Vsh_ = 1.0 + Kv_ * Verror;
    Vsh_ = std::max(0.8, std::min(1.2, Vsh_));
    return Vsh_;
}

double UPFCModel::calculateSeriesReactive() {
    // Qser = Vser * Iline * sin(thetaSer - phi_line)
    Qser_ = Vser_ * Pref_ * 0.5; // Aproximacion
    Qser_ = std::max(-seriesRating_, std::min(seriesRating_, Qser_));
    return Qser_;
}

double UPFCModel::calculateShuntReactive() {
    // Qsh = Vsh * (Vsh - Vbus) / Xsh
    Qsh_ = (Vsh_ - Vref_) * shuntRating_;
    Qsh_ = std::max(-shuntRating_, std::min(shuntRating_, Qsh_));
    return Qsh_;
}

double UPFCModel::calculateActivePowerTransfer() {
    // P = (V1 * V2 / X) * sin(theta2 - theta1) + Vser * V2 / X * sin(thetaSer)
    Ptransferred_ = Pref_ + Vser_ * std::sin(thetaSer_) * seriesRating_;
    Ptransferred_ = std::max(-seriesRating_, std::min(seriesRating_, Ptransferred_));
    return Ptransferred_;
}

double UPFCModel::calculateReactivePowerControl() {
    calculateShuntReactive();
    return Qsh_;
}

double UPFCModel::calculateRealPowerInjection() {
    return calculateActivePowerTransfer();
}

double UPFCModel::calculateReactivePowerInjection() {
    return calculateSeriesReactive() + calculateShuntReactive();
}

FACTSOperatingPoint UPFCModel::getOperatingPoint() const {
    FACTSOperatingPoint op = {};
    op.P = Ptransferred_;
    op.Q = Qser_ + Qsh_;
    op.V = Vsh_;
    op.Vser = Vser_;
    op.thetaSer = thetaSer_;
    op.Vsh = Vsh_;
    op.thetaSh = thetaSh_;
    op.mode = mode_;
    op.status = status_;
    return op;
}

// ============================================================================
// MMCModel - Modular Multilevel Converter
// ============================================================================

MMCModel::MMCModel()
    : id_(0), numArms_(6), numSubmodulesPerArm_(200),
      submoduleCapacitance_(10e-3), submoduleVoltageRated_(2000.0),
      ratedPower_(500.0), Vdc_(500.0), Vac_(230.0), Pref_(0.0),
      Qref_(0.0), Larm_(0.05), Rarm_(0.1), Icirc_(0.0),
      Pout_(0.0), Qout_(0.0), status_(1),
      mode_(FACTS_MODE_VOLTAGE_REGULATION),
      modulationIndex_(0.9), switchingFreq_(2000.0) {
    arms_.resize(numArms_, std::vector<Submodule>(numSubmodulesPerArm_));
    for (auto& arm : arms_) {
        for (auto& sm : arm) {
            sm.capacitorVoltage = submoduleVoltageRated_;
            sm.capacitance = submoduleCapacitance_;
            sm.state = 0;
        }
    }
}

MMCModel::MMCModel(int id, int numArms, int submodulesPerArm,
                    double submoduleCapacitance, double submoduleVoltage,
                    double ratedPower)
    : id_(id), numArms_(numArms), numSubmodulesPerArm_(submodulesPerArm),
      submoduleCapacitance_(submoduleCapacitance),
      submoduleVoltageRated_(submoduleVoltage), ratedPower_(ratedPower),
      Vdc_(500.0), Vac_(230.0), Pref_(0.0), Qref_(0.0),
      Larm_(0.05), Rarm_(0.1), Icirc_(0.0), Pout_(0.0), Qout_(0.0),
      status_(1), mode_(FACTS_MODE_VOLTAGE_REGULATION),
      modulationIndex_(0.9), switchingFreq_(2000.0) {
    arms_.resize(numArms_, std::vector<Submodule>(numSubmodulesPerArm_));
    for (auto& arm : arms_) {
        for (auto& sm : arm) {
            sm.capacitorVoltage = submoduleVoltageRated_;
            sm.capacitance = submoduleCapacitance_;
            sm.state = 0;
        }
    }
}

void MMCModel::setOperatingMode(FACTSOperatingMode mode) { mode_ = mode; }
void MMCModel::setDCVoltage(double Vdc) { Vdc_ = Vdc; }
void MMCModel::setACVoltage(double Vac) { Vac_ = Vac; }
void MMCModel::setPowerReference(double P) {
    Pref_ = std::max(-ratedPower_, std::min(ratedPower_, P));
}
void MMCModel::setReactiveReference(double Q) {
    Qref_ = std::max(-ratedPower_, std::min(ratedPower_, Q));
}
void MMCModel::setArmInductance(double Larm) { Larm_ = Larm; }
void MMCModel::setArmResistance(double Rarm) { Rarm_ = Rarm; }
void MMCModel::setCirculatingCurrent(double Icirc) { Icirc_ = Icirc; }
void MMCModel::setStatus(int status) { status_ = status; }

double MMCModel::getAverageCapacitorVoltage() const {
    double sum = 0.0;
    int count = 0;
    for (const auto& arm : arms_) {
        for (const auto& sm : arm) {
            sum += sm.capacitorVoltage;
            count++;
        }
    }
    return (count > 0) ? sum / count : 0.0;
}

double MMCModel::getCapacitorVoltageRipple() const {
    double Vavg = getAverageCapacitorVoltage();
    if (Vavg < 1e-6) return 0.0;
    double Vmax = 0.0, Vmin = 1e9;
    for (const auto& arm : arms_) {
        for (const auto& sm : arm) {
            Vmax = std::max(Vmax, sm.capacitorVoltage);
            Vmin = std::min(Vmin, sm.capacitorVoltage);
        }
    }
    return ((Vmax - Vmin) / Vavg) * 100.0; // %
}

double MMCModel::getMaxCapacitorDeviation() const {
    double Vavg = getAverageCapacitorVoltage();
    if (Vavg < 1e-6) return 0.0;
    double maxDev = 0.0;
    for (const auto& arm : arms_) {
        for (const auto& sm : arm) {
            double dev = std::abs(sm.capacitorVoltage - Vavg) / Vavg;
            maxDev = std::max(maxDev, dev);
        }
    }
    return maxDev * 100.0; // %
}

void MMCModel::balanceCapacitors() {
    double Vavg = getAverageCapacitorVoltage();
    double tol = submoduleVoltageRated_ * 0.05; // 5%
    for (auto& arm : arms_) {
        for (auto& sm : arm) {
            if (sm.capacitorVoltage > Vavg + tol) {
                // Discharge
                sm.capacitorVoltage -= 0.1;
            } else if (sm.capacitorVoltage < Vavg - tol) {
                // Charge
                sm.capacitorVoltage += 0.1;
            }
            sm.capacitorVoltage = std::max(submoduleVoltageRated_ * 0.8,
                std::min(submoduleVoltageRated_ * 1.2, sm.capacitorVoltage));
        }
    }
}

double MMCModel::calculateUpperArmVoltage() const {
    return Vdc_ * 0.5 + Vac_ * modulationIndex_ / FACTS_SQRT2;
}

double MMCModel::calculateLowerArmVoltage() const {
    return Vdc_ * 0.5 - Vac_ * modulationIndex_ / FACTS_SQRT2;
}

double MMCModel::calculateArmCurrent() const {
    double S = std::sqrt(Pref_ * Pref_ + Qref_ * Qref_);
    if (Vac_ < 1e-6) return 0.0;
    return S * 1e6 / (Vac_ * 1000.0 * FACTS_SQRT3); // A
}

double MMCModel::calculateCirculatingCurrent() const {
    return Icirc_;
}

double MMCModel::calculateArmInductorVoltage() const {
    double Iarm = calculateArmCurrent();
    return Larm_ * Iarm * switchingFreq_ * 2.0 * FACTS_PI / 1000.0; // kV
}

double MMCModel::getSubmoduleVoltage() const {
    double Varm = Vdc_ * 1000.0 / 2.0;
    return Varm / numSubmodulesPerArm_;
}

double MMCModel::calculateInsertionIndex(int arm) const {
    // Ninserted = Ntotal * (Varm / Vcap_total)
    double VcapTotal = numSubmodulesPerArm_ * getSubmoduleVoltage();
    if (VcapTotal < 1e-6) return 0.0;
    double Varm = (arm % 2 == 0)
        ? calculateUpperArmVoltage()
        : calculateLowerArmVoltage();
    return Varm * 1000.0 / VcapTotal;
}

double MMCModel::calculateModulationIndex() {
    modulationIndex_ = Vac_ * FACTS_SQRT2 / Vdc_;
    return std::min(1.15, modulationIndex_);
}

double MMCModel::calculateInjection() {
    if (status_ == 0) return 0.0;
    Pout_ = Pref_;
    Qout_ = Qref_;
    balanceCapacitors();
    calculateModulationIndex();
    return Qout_;
}

double MMCModel::calculateActivePowerInjection() {
    Pout_ = Pref_;
    return Pout_;
}

double MMCModel::calculateReactivePowerInjection() {
    Qout_ = Qref_;
    return Qout_;
}

void MMCModel::sortAndBalance() {
    for (auto& arm : arms_) {
        std::sort(arm.begin(), arm.end(),
            [](const Submodule& a, const Submodule& b) {
                return a.capacitorVoltage < b.capacitorVoltage;
            });
    }
    balanceCapacitors();
}

int MMCModel::selectSubmodule(bool insert, double armCurrent) {
    // Seleccion: insertar SM con menor V si cargando, mayor V si descargando
    static int lastArm = 0;
    int selected = -1;
    auto& arm = arms_[lastArm % numArms_];
    if (insert) {
        if (armCurrent > 0) {
            // Cargando: insertar SM con menor V
            auto it = std::min_element(arm.begin(), arm.end(),
                [](const Submodule& a, const Submodule& b) {
                    return a.capacitorVoltage < b.capacitorVoltage;
                });
            if (it != arm.end()) selected = (int)(it - arm.begin());
        } else {
            // Descargando: insertar SM con mayor V
            auto it = std::max_element(arm.begin(), arm.end(),
                [](const Submodule& a, const Submodule& b) {
                    return a.capacitorVoltage < b.capacitorVoltage;
                });
            if (it != arm.end()) selected = (int)(it - arm.begin());
        }
    } else {
        // Bypass: seleccionar SM con menor desviacion
        double Vavg = getAverageCapacitorVoltage();
        auto it = std::min_element(arm.begin(), arm.end(),
            [Vavg](const Submodule& a, const Submodule& b) {
                return std::abs(a.capacitorVoltage - Vavg)
                       < std::abs(b.capacitorVoltage - Vavg);
            });
        if (it != arm.end()) selected = (int)(it - arm.begin());
    }
    lastArm++;
    return selected;
}

void MMCModel::updateCapacitorVoltages(double dt) {
    double Iarm = calculateArmCurrent();
    for (auto& arm : arms_) {
        for (auto& sm : arm) {
            if (sm.state == 1) { // Inserted
                double dV = Iarm * dt / sm.capacitance;
                sm.capacitorVoltage += dV;
            }
            sm.capacitorVoltage = std::max(submoduleVoltageRated_ * 0.7,
                std::min(submoduleVoltageRated_ * 1.3, sm.capacitorVoltage));
        }
    }
}

FACTSOperatingPoint MMCModel::getOperatingPoint() const {
    FACTSOperatingPoint op = {};
    op.P = Pout_;
    op.Q = Qout_;
    op.V = Vac_ / (Vdc_ > 0 ? Vdc_ : 1.0);
    op.modulation = modulationIndex_;
    op.mode = mode_;
    op.status = status_;
    return op;
}

} // namespace powsys365
