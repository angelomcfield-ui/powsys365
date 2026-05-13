// ============================================================================
// hvdc.cpp
// Implementacion de modelos HVDC: LCC, VSC, MTDC
// ============================================================================

#include "hvdc.h"
#include <numeric>
#include <stdexcept>

namespace powsys365 {

// ============================================================================
// HVDCLCCModel - Line Commutated Converter
// ============================================================================

HVDCLCCModel::HVDCLCCModel()
    : id_(0), fromBus_(0), toBus_(1), ratedPower_(500.0),
      Vdc_(500.0), Vac_(230.0), pulseNumber_(12),
      alpha_(15.0), gamma_(18.0), gammaMin_(15.0), mu_(0.0),
      IdcRef_(1000.0), PdcRef_(500.0), VdcRef_(500.0),
      transformerRatio_(1.0), commutationReactance_(0.15),
      dcResistance_(5.0), transformerResistance_(0.01),
      transformerReactance_(0.15), efficiency_(0.97),
      controlMode_(LCC_CC), status_(1), powerReversed_(false) {}

HVDCLCCModel::HVDCLCCModel(int id, int fromBus, int toBus,
                            double ratedPower, double Vdc, double Vac,
                            int pulses)
    : id_(id), fromBus_(fromBus), toBus_(toBus), ratedPower_(ratedPower),
      Vdc_(Vdc), Vac_(Vac), pulseNumber_(pulses),
      alpha_(15.0), gamma_(18.0), gammaMin_(15.0), mu_(0.0),
      IdcRef_(ratedPower / Vdc * 1e6 / 1000.0), PdcRef_(ratedPower),
      VdcRef_(Vdc), transformerRatio_(Vdc / Vac),
      commutationReactance_(0.15), dcResistance_(5.0),
      transformerResistance_(0.01), transformerReactance_(0.15),
      efficiency_(0.97), controlMode_(LCC_CC), status_(1),
      powerReversed_(false) {}

// Setters
void HVDCLCCModel::setFiringAngle(double alphaDeg) {
    alpha_ = std::max(0.0, std::min(90.0, alphaDeg));
}
void HVDCLCCModel::setExtinctionAngle(double gammaDeg) {
    gamma_ = std::max(gammaMin_, std::min(60.0, gammaDeg));
}
void HVDCLCCModel::setCurrentOrder(double IdcRef) {
    IdcRef_ = IdcRef;
}
void HVDCLCCModel::setPowerOrder(double PdcRef) {
    PdcRef_ = PdcRef;
}
void HVDCLCCModel::setControlMode(LCCControlMode mode) {
    controlMode_ = mode;
}
void HVDCLCCModel::setTransformerRatio(double ratio) {
    transformerRatio_ = ratio;
}
void HVDCLCCModel::setCommutationReactance(double Xc) {
    commutationReactance_ = Xc;
}
void HVDCLCCModel::setDCResistance(double Rdc) {
    dcResistance_ = Rdc;
}
void HVDCLCCModel::setStatus(int status) {
    status_ = status;
}

// Angulo de overlap
// cos(mu) = 1 - 2*Xc*Idc/(sqrt(2)*Vac*sin(pi/m))
double HVDCLCCModel::calculateOverlapAngle(double alpha, bool isInverter) const {
    double m = (pulseNumber_ == 12) ? 6.0 : 3.0;
    double Vac_phase = Vac_ / HVDC_SQRT3; // Fase a neutro
    double Xc_ohm = commutationReactance_ * (Vac_ * Vac_) / ratedPower_;
    double denom = HVDC_SQRT2 * Vac_phase * std::sin(HVDC_PI / m);
    if (denom < 1e-10) return 0.0;
    double cos_mu = 1.0 - 2.0 * Xc_ohm * IdcRef_ / denom;
    cos_mu = std::max(-1.0, std::min(1.0, cos_mu));
    double mu = std::acos(cos_mu);
    return mu * 180.0 / HVDC_PI; // grados
}

// Voltaje DC rectificador
// Vd = (3*sqrt(2)/pi) * m * Vac * cos(alpha) - (3/pi) * Xc * Idc
double HVDCLCCModel::calculateRectifierVoltage(double alpha, double mu) const {
    double alphaRad = alpha * HVDC_PI / 180.0;
    double m = (pulseNumber_ == 12) ? 2.0 : 1.0;
    double Xc_ohm = commutationReactance_ * (Vac_ * Vac_) / ratedPower_;
    double B = getConverterConstant();
    double Vd = m * B * Vac_ * std::cos(alphaRad)
                - (3.0 * Xc_ohm * IdcRef_ / HVDC_PI) * m;
    return Vd;
}

// Voltaje DC inversor
// Vi = (3*sqrt(2)/pi) * m * Vac * cos(gamma) - (3/pi) * Xc * Idc
double HVDCLCCModel::calculateInverterVoltage(double gamma, double mu) const {
    double gammaRad = gamma * HVDC_PI / 180.0;
    double m = (pulseNumber_ == 12) ? 2.0 : 1.0;
    double Xc_ohm = commutationReactance_ * (Vac_ * Vac_) / ratedPower_;
    double B = getConverterConstant();
    double Vi = m * B * Vac_ * std::cos(gammaRad)
                - (3.0 * Xc_ohm * IdcRef_ / HVDC_PI) * m;
    return Vi;
}

double HVDCLCCModel::calculateNoLoadVoltage() const {
    double m = (pulseNumber_ == 12) ? 2.0 : 1.0;
    double B = getConverterConstant();
    return m * B * Vac_;
}

double HVDCLCCModel::getIdc() const {
    if (status_ == 0) return 0.0;
    double Vdr = calculateRectifierVoltage(alpha_, mu_);
    double Vdi = calculateInverterVoltage(gamma_, mu_);
    double dV = Vdr - Vdi;
    if (dcResistance_ < 1e-6) return 0.0;
    return dV * 1000.0 / dcResistance_; // A
}

double HVDCLCCModel::getLosses() const {
    return calculateACPowerLoss() + calculateDCPowerLoss();
}

double HVDCLCCModel::calculateACPowerLoss() const {
    double Idc = getIdc();
    double Pac = Idc * Vac_ / 1000.0; // MVA aprox
    // Perdidas en transformador y conmutacion: ~1-2%
    return Pac * 0.01 * 2.0; // MW
}

double HVDCLCCModel::calculateDCPowerLoss() const {
    double Idc = getIdc();
    return Idc * Idc * dcResistance_ / 1e6; // MW
}

double HVDCLCCModel::getACCurrents() const {
    double Idc = getIdc();
    return Idc * HVDC_SQRT2 / HVDC_SQRT3; // A RMS AC
}

double HVDCLCCModel::getDCCurrents() const {
    return getIdc();
}

void HVDCLCCModel::reversePowerFlow() {
    powerReversed_ = !powerReversed_;
    std::swap(fromBus_, toBus_);
}

HVDCPowerFlow HVDCLCCModel::getPowerFlow() const {
    HVDCPowerFlow pf = {};
    if (status_ == 0) {
        pf.status = 0;
        return pf;
    }

    double mu_local = calculateOverlapAngle(alpha_, false);
    double Idc = getIdc();
    double Vdr = calculateRectifierVoltage(alpha_, mu_local);
    double Vdi = calculateInverterVoltage(gamma_, mu_local);

    double Pdc = Idc * Vdr / 1e6; // Rectifier power MW
    double Pdc_inv = Idc * Vdi / 1e6; // Inverter power MW
    double Ploss_dc = Idc * Idc * dcResistance_ / 1e6;

    double Pac_r = Pdc + calculateACPowerLoss();
    double Pac_i = Pdc_inv;

    // Potencia reactiva: Q = P * tan(phi)
    double phi_r = alpha_ + mu_local / 2.0;
    double phi_i = gamma_ + mu_local / 2.0;
    double Qac_r = Pac_r * std::tan(phi_r * HVDC_PI / 180.0);
    double Qac_i = Pac_i * std::tan(phi_i * HVDC_PI / 180.0);

    pf.Pdc = Pdc;
    pf.Vdc = Vdr;
    pf.Idc = Idc;
    pf.Pac_from = Pac_r;
    pf.Pac_to = Pac_i;
    pf.Qac_from = Qac_r;
    pf.Qac_to = Qac_i;
    pf.Vac_from = Vac_;
    pf.Vac_to = Vac_;
    pf.Iac_from = getACCurrents();
    pf.Iac_to = getACCurrents();
    pf.losses = getLosses();
    pf.alpha = alpha_;
    pf.gamma = gamma_;
    pf.mu = mu_;
    pf.phi_from = phi_r;
    pf.phi_to = phi_i;
    pf.pf_from = std::cos(phi_r * HVDC_PI / 180.0);
    pf.pf_to = std::cos(phi_i * HVDC_PI / 180.0);
    pf.status = status_;

    return pf;
}

// ============================================================================
// HVDCVSCModel - Voltage Source Converter
// ============================================================================

HVDCVSCModel::HVDCVSCModel()
    : id_(0), fromBus_(0), toBus_(1), ratedPower_(500.0),
      Vdc_(500.0), Vac_(230.0), Idc_(0.0),
      controlMode_(VSC_PQ), Pref_(0.0), Qref_(0.0),
      VdcRef_(500.0), VacRef_(1.0), droopCoeff_(0.05),
      converterLossFactor_(0.02), filterR_(0.01), filterX_(0.15),
      dcResistance_(5.0), transformerRatio_(1.0),
      transformerX_(0.15), modulationIndex_(0.9),
      switchingFreq_(2000.0), Pac_from_(0.0), Pac_to_(0.0),
      Qac_from_(0.0), Qac_to_(0.0), status_(1),
      powerReversed_(false), blackStartCapable_(false),
      numSubmodules_(200), submoduleCapacitance_(10e-3),
      submoduleVoltageRated_(2000.0) {}

HVDCVSCModel::HVDCVSCModel(int id, int fromBus, int toBus,
                            double ratedPower, double Vdc, double Vac)
    : id_(id), fromBus_(fromBus), toBus_(toBus), ratedPower_(ratedPower),
      Vdc_(Vdc), Vac_(Vac), Idc_(0.0),
      controlMode_(VSC_PQ), Pref_(0.0), Qref_(0.0),
      VdcRef_(Vdc), VacRef_(1.0), droopCoeff_(0.05),
      converterLossFactor_(0.02), filterR_(0.01), filterX_(0.15),
      dcResistance_(5.0), transformerRatio_(Vdc / Vac),
      transformerX_(0.15), modulationIndex_(0.9),
      switchingFreq_(2000.0), Pac_from_(0.0), Pac_to_(0.0),
      Qac_from_(0.0), Qac_to_(0.0), status_(1),
      powerReversed_(false), blackStartCapable_(false),
      numSubmodules_(200), submoduleCapacitance_(10e-3),
      submoduleVoltageRated_(2000.0) {}

// Setters
void HVDCVSCModel::setControlMode(VSCControlMode mode) {
    controlMode_ = mode;
}
void HVDCVSCModel::setPowerReference(double P) {
    Pref_ = std::max(-ratedPower_, std::min(ratedPower_, P));
}
void HVDCVSCModel::setReactiveReference(double Q) {
    Qref_ = std::max(-ratedPower_ * 0.5, std::min(ratedPower_ * 0.5, Q));
}
void HVDCVSCModel::setDCVoltageReference(double VdcRef) {
    VdcRef_ = VdcRef;
}
void HVDCVSCModel::setACVoltageReference(double VacRef) {
    VacRef_ = VacRef;
}
void HVDCVSCModel::setDroopCoefficient(double droop) {
    droopCoeff_ = droop;
}
void HVDCVSCModel::setConverterLossFactor(double lossFactor) {
    converterLossFactor_ = lossFactor;
}
void HVDCVSCModel::setFilterParameters(double Rf, double Xf) {
    filterR_ = Rf;
    filterX_ = Xf;
}
void HVDCVSCModel::setDCResistance(double Rdc) {
    dcResistance_ = Rdc;
}
void HVDCVSCModel::setStatus(int status) {
    status_ = status;
}

// Control P-Q
double HVDCVSCModel::calculatePQControl() const {
    return Pref_;
}

// Control Vdc-Q: P se ajusta para mantener Vdc
double HVDCVSCModel::calculateVdcQControl() const {
    double Vdc_error = VdcRef_ - Vdc_;
    double Kp = ratedPower_ / (0.1 * VdcRef_);
    double P = Kp * Vdc_error;
    return std::max(-ratedPower_, std::min(ratedPower_, P));
}

// Control Vdc-Vac
double HVDCVSCModel::calculateVdcVacControl() const {
    return calculateVdcQControl();
}

// Control droop: P = Pref - (1/droop) * (Vdc - Vdc_ref)
double HVDCVSCModel::calculateDroopControl() const {
    double Vdc_error = Vdc_ - VdcRef_;
    double P = Pref_ - Vdc_error / droopCoeff_ * ratedPower_ / VdcRef_;
    return std::max(-ratedPower_, std::min(ratedPower_, P));
}

double HVDCVSCModel::calculateConverterLosses(double P) const {
    double absP = std::abs(P);
    // Perdidas: conduction + switching
    double P_base_loss = converterLossFactor_ * ratedPower_;
    double P_var_loss = 0.01 * absP; // 1% variable
    return P_base_loss + P_var_loss;
}

double HVDCVSCModel::calculateACVoltage(double Vdc) const {
    return Vdc * modulationIndex_ / HVDC_SQRT2;
}

double HVDCVSCModel::getLosses() const {
    return calculateConverterLosses(Pref_);
}

double HVDCVSCModel::getACCurrents() const {
    double S = std::sqrt(Pref_ * Pref_ + Qref_ * Qref_);
    if (Vac_ < 1e-6) return 0.0;
    return S * 1e6 / (Vac_ * 1000.0 * HVDC_SQRT3); // A
}

double HVDCVSCModel::getDCCurrents() const {
    return getIdc();
}

double HVDCVSCModel::getIdc() const {
    double Pac = std::abs(Pref_);
    double Ploss = calculateConverterLosses(Pref_);
    double Pdc = Pac + Ploss;
    if (Vdc_ < 1e-6) return 0.0;
    return Pdc * 1e6 / Vdc_; // A
}

// MMC
double HVDCVSCModel::calculateMMCVoltage() const {
    return numSubmodules_ * submoduleVoltageRated_ / 1000.0; // kV
}

int HVDCVSCModel::getNumberOfSubmodules() const {
    return numSubmodules_;
}

double HVDCVSCModel::getSubmoduleVoltage() const {
    double Varm = Vdc_ * 1000.0 / 2.0; // Voltaje por brazo
    return Varm / numSubmodules_;
}

double HVDCVSCModel::getArmCurrent() const {
    double Idc = getIdc();
    return Idc / 3.0 + getACCurrents() / 2.0; // Aproximacion
}

double HVDCVSCModel::getBlackStartPower() const {
    if (!blackStartCapable_) return 0.0;
    return ratedPower_ * 0.1; // 10% para black start
}

void HVDCVSCModel::reversePowerFlow() {
    powerReversed_ = !powerReversed_;
    std::swap(fromBus_, toBus_);
    Pref_ = -Pref_;
}

HVDCPowerFlow HVDCVSCModel::getPowerFlow() const {
    HVDCPowerFlow pf = {};
    if (status_ == 0) {
        pf.status = 0;
        return pf;
    }

    double P = 0.0;
    switch (controlMode_) {
        case VSC_PQ:       P = calculatePQControl(); break;
        case VSC_VDC_Q:    P = calculateVdcQControl(); break;
        case VSC_VDC_VAC:  P = calculateVdcVacControl(); break;
        case VSC_DROOP:    P = calculateDroopControl(); break;
    }

    double Ploss = calculateConverterLosses(P);
    double Pdc = std::abs(P) + Ploss;
    double Idc_val = (Vdc_ > 1e-6) ? Pdc * 1e6 / Vdc_ : 0.0;

    double Pac_from_val = P;
    double Pac_to_val = P; // P negativo = flujo opuesto
    double Q = Qref_;

    pf.Pdc = Pdc;
    pf.Vdc = Vdc_;
    pf.Idc = Idc_val;
    pf.Pac_from = Pac_from_val;
    pf.Pac_to = Pac_to_val;
    pf.Qac_from = Q;
    pf.Qac_to = Q;
    pf.Vac_from = Vac_;
    pf.Vac_to = Vac_;
    pf.Iac_from = getACCurrents();
    pf.Iac_to = getACCurrents();
    pf.losses = Ploss + Idc_val * Idc_val * dcResistance_ / 1e6;
    pf.pf_from = (Pac_from_val != 0.0)
                 ? std::abs(Pac_from_val)
                   / std::sqrt(Pac_from_val * Pac_from_val + Q * Q) : 1.0;
    pf.pf_to = pf.pf_from;
    pf.status = status_;

    return pf;
}

// ============================================================================
// HVDCMTDCModel - Multi-Terminal DC
// ============================================================================

HVDCMTDCModel::HVDCMTDCModel()
    : VdcNominal_(320.0), totalLosses_(0.0), status_(1) {}

HVDCMTDCModel::HVDCMTDCModel(int numTerminals)
    : VdcNominal_(320.0), totalLosses_(0.0), status_(1) {
    cableResistance_.resize(numTerminals,
                            std::vector<double>(numTerminals, 0.0));
}

void HVDCMTDCModel::addTerminal(const MTDCTerminal& terminal) {
    terminals_.push_back(terminal);
    size_t n = terminals_.size();
    cableResistance_.resize(n, std::vector<double>(n, 0.0));
}

void HVDCMTDCModel::removeTerminal(int terminalId) {
    terminals_.erase(
        std::remove_if(terminals_.begin(), terminals_.end(),
            [terminalId](const MTDCTerminal& t) {
                return t.id == terminalId;
            }), terminals_.end());
}

MTDCTerminal* HVDCMTDCModel::getTerminal(int terminalId) {
    for (auto& t : terminals_) {
        if (t.id == terminalId) return &t;
    }
    return nullptr;
}

const MTDCTerminal* HVDCMTDCModel::getTerminal(int terminalId) const {
    for (const auto& t : terminals_) {
        if (t.id == terminalId) return &t;
    }
    return nullptr;
}

void HVDCMTDCModel::setDCCableResistance(int fromTerm, int toTerm,
                                          double R) {
    if (fromTerm >= 0 && fromTerm < (int)cableResistance_.size()
        && toTerm >= 0 && toTerm < (int)cableResistance_[0].size()) {
        cableResistance_[fromTerm][toTerm] = R;
        cableResistance_[toTerm][fromTerm] = R;
    }
}

void HVDCMTDCModel::setTerminalPowerReference(int terminalId, double P) {
    auto* t = getTerminal(terminalId);
    if (t) t->Pset = P;
}

void HVDCMTDCModel::setTerminalControlMode(int terminalId,
                                            VSCControlMode mode) {
    auto* t = getTerminal(terminalId);
    if (t) t->controlMode = mode;
}

void HVDCMTDCModel::setTerminalDroopCoeff(int terminalId, double droop) {
    auto* t = getTerminal(terminalId);
    if (t) t->droopCoeff = droop;
}

void HVDCMTDCModel::setTerminalVdcRef(int terminalId, double Vdc) {
    auto* t = getTerminal(terminalId);
    if (t) t->VdcRef = Vdc;
}

void HVDCMTDCModel::setTerminalVacRef(int terminalId, double Vac) {
    auto* t = getTerminal(terminalId);
    if (t) t->VacRef = Vac;
}

void HVDCMTDCModel::setStatus(int status) {
    status_ = status;
}

// Calcular voltaje DC del sistema
double HVDCMTDCModel::calculateDCVoltage() const {
    double Vdc_sum = 0.0;
    int count = 0;
    for (const auto& t : terminals_) {
        if (t.status == 1 && t.controlMode == VSC_VDC_Q) {
            Vdc_sum += t.VdcRef;
            count++;
        }
    }
    return (count > 0) ? Vdc_sum / count : VdcNominal_;
}

// Balance de voltaje DC (droop control)
void HVDCMTDCModel::balanceDCVoltage() {
    double Vdc = calculateDCVoltage();
    for (auto& t : terminals_) {
        if (t.status != 1) continue;
        if (t.controlMode == VSC_DROOP) {
            double error = Vdc - t.VdcRef;
            t.Pac = t.Pset - error / t.droopCoeff * t.ratedPower
                    / t.VdcRef;
            t.Pac = std::max(-t.ratedPower,
                              std::min(t.ratedPower, t.Pac));
        }
    }
}

// Asignacion de potencia
double HVDCMTDCModel::calculatePowerAllocation(int terminalId) const {
    const auto* t = getTerminal(terminalId);
    if (!t || t->status != 1) return 0.0;

    double P = 0.0;
    switch (t->controlMode) {
        case VSC_PQ:
            P = t->Pset;
            break;
        case VSC_DROOP: {
            double Vdc = calculateDCVoltage();
            double error = Vdc - t->VdcRef;
            P = t->Pset - error / t->droopCoeff * t->ratedPower
                / t->VdcRef;
            break;
        }
        case VSC_VDC_Q:
            // Terminal slack: balancea potencia
            P = 0.0;
            for (const auto& other : terminals_) {
                if (other.id != terminalId && other.status == 1) {
                    P -= other.Pac;
                }
            }
            break;
        default:
            P = t->Pset;
            break;
    }
    return std::max(-t->ratedPower, std::min(t->ratedPower, P));
}

void HVDCMTDCModel::allocatePower(double totalPower) {
    if (terminals_.empty()) return;
    double P_per_terminal = totalPower / (double)terminals_.size();
    for (auto& t : terminals_) {
        if (t.status == 1) {
            t.Pac = std::max(-t.ratedPower,
                              std::min(t.ratedPower, P_per_terminal));
        }
    }
}

bool HVDCMTDCModel::solveDCPowerFlow() {
    // Asignar potencia a cada terminal segun su modo
    for (auto& t : terminals_) {
        if (t.status != 1) continue;
        t.Pac = calculatePowerAllocation(t.id);
        if (std::abs(t.VdcRef) > 1e-6) {
            t.Idc = t.Pac * 1e6 / (t.VdcRef * 1000.0); // A
        }
    }
    return true;
}

void HVDCMTDCModel::calculateCableLosses() {
    totalLosses_ = 0.0;
    size_t n = terminals_.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            double R = cableResistance_[i][j];
            if (R > 0 && terminals_[i].status == 1
                && terminals_[j].status == 1) {
                double Iavg = (terminals_[i].Idc + terminals_[j].Idc)
                               / 2.0;
                double Ploss = Iavg * Iavg * R / 1e6; // MW
                totalLosses_ += Ploss;
            }
        }
    }
}

double HVDCMTDCModel::getTotalLosses() const {
    double convLosses = 0.0;
    for (const auto& t : terminals_) {
        if (t.status == 1) {
            double S = std::sqrt(t.Pac * t.Pac + t.Qac * t.Qac);
            convLosses += 0.02 * S; // 2% perdidas convertidor
        }
    }
    return totalLosses_ + convLosses;
}

double HVDCMTDCModel::getCablesLosses() const {
    return totalLosses_;
}

std::vector<HVDCPowerFlow> HVDCMTDCModel::getPowerFlow() {
    std::vector<HVDCPowerFlow> results;
    if (status_ == 0 || terminals_.empty()) return results;

    solveDCPowerFlow();
    calculateCableLosses();
    balanceDCVoltage();

    for (const auto& t : terminals_) {
        HVDCPowerFlow pf = {};
        if (t.status != 1) {
            pf.status = 0;
            results.push_back(pf);
            continue;
        }

        double Ploss = 0.02 * std::sqrt(t.Pac * t.Pac + t.Qac * t.Qac);
        double Vdc = (std::abs(t.Vdc) > 1e-6) ? t.Vdc : VdcNominal_;
        double Idc_val = (Vdc > 1e-6) ? (std::abs(t.Pac) + Ploss)
                                        * 1e6 / (Vdc * 1000.0) : 0.0;

        pf.Pdc = t.Pac;
        pf.Vdc = Vdc;
        pf.Idc = Idc_val;
        pf.Pac_from = t.Pac;
        pf.Qac_from = t.Qac;
        pf.Vac_from = t.Vac;
        pf.Iac_from = (t.Vac > 1e-6)
            ? std::sqrt(t.Pac * t.Pac + t.Qac * t.Qac) * 1e6
              / (t.Vac * 1000.0 * HVDC_SQRT3) : 0.0;
        pf.losses = Ploss;
        pf.pf_from = (t.Pac != 0.0)
            ? std::abs(t.Pac) / std::sqrt(t.Pac * t.Pac
                                            + t.Qac * t.Qac) : 1.0;
        pf.status = t.status;
        results.push_back(pf);
    }
    return results;
}

bool HVDCMTDCModel::canBlackStart() const {
    for (const auto& t : terminals_) {
        if (t.blackStartCapable && t.status == 1) return true;
    }
    return false;
}

int HVDCMTDCModel::getBlackStartTerminal() const {
    for (const auto& t : terminals_) {
        if (t.blackStartCapable && t.status == 1) return t.id;
    }
    return -1;
}

std::vector<double> HVDCMTDCModel::getACCurrents() const {
    std::vector<double> currents;
    for (const auto& t : terminals_) {
        if (t.status == 1 && t.Vac > 1e-6) {
            double S = std::sqrt(t.Pac * t.Pac + t.Qac * t.Qac);
            currents.push_back(S * 1e6 / (t.Vac * 1000.0 * HVDC_SQRT3));
        } else {
            currents.push_back(0.0);
        }
    }
    return currents;
}

std::vector<double> HVDCMTDCModel::getDCCurrents() const {
    std::vector<double> currents;
    for (const auto& t : terminals_) {
        double Vdc = (std::abs(t.Vdc) > 1e-6) ? t.Vdc : VdcNominal_;
        if (t.status == 1 && Vdc > 1e-6) {
            currents.push_back(std::abs(t.Pac) * 1e6 / (Vdc * 1000.0));
        } else {
            currents.push_back(0.0);
        }
    }
    return currents;
}

} // namespace powsys365
