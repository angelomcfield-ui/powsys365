// ============================================================================
// solar_pv.cpp
// Implementacion del modelo fotovoltaico CIRED
// ============================================================================

#include "solar_pv.h"
#include <stdexcept>
#include <numeric>
#include <limits>

namespace powsys365 {

// ============================================================================
// Constructores
// ============================================================================
SolarPVModel::SolarPVModel()
    : cell_(), array_(), inverter_(), irradiance_(1000.0),
      ambientTemp_(25.0), cellTemp_(25.0), VterminalPu_(1.0),
      pfSetpoint_(1.0), status_(1) {
    lastOutput_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1};
}

SolarPVModel::SolarPVModel(const CellParameters& cellParams,
                            const ArrayParameters& arrayParams,
                            const InverterModel& inverter)
    : cell_(cellParams), array_(arrayParams), inverter_(inverter),
      irradiance_(1000.0), ambientTemp_(25.0), cellTemp_(25.0),
      VterminalPu_(1.0), pfSetpoint_(1.0), status_(1) {
    lastOutput_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1};
}

// ============================================================================
// Setters
// ============================================================================
void SolarPVModel::setIrradiance(double irradiance) {
    irradiance_ = std::max(0.0, std::min(2000.0, irradiance));
}

void SolarPVModel::setTemperature(double ambientTemp) {
    ambientTemp_ = std::max(-40.0, std::min(80.0, ambientTemp));
}

void SolarPVModel::setCellTemperature(double cellTemp) {
    cellTemp_ = std::max(-40.0, std::min(100.0, cellTemp));
}

void SolarPVModel::setNOCT(double noct) {
    cell_.NOCT = std::max(30.0, std::min(60.0, noct));
}

void SolarPVModel::setCellParameters(const CellParameters& params) {
    cell_ = params;
}

void SolarPVModel::setArrayParameters(const ArrayParameters& params) {
    array_ = params;
}

void SolarPVModel::setInverterModel(const InverterModel& inv) {
    inverter_ = inv;
}

void SolarPVModel::setTerminalVoltage(double Vpu) {
    VterminalPu_ = Vpu;
}

void SolarPVModel::setPowerFactor(double pf) {
    pfSetpoint_ = std::max(inverter_.pfMin, std::min(1.0, std::abs(pf)));
}

// ============================================================================
// Temperatura de celda: modelo empirico basado en NOCT
// Tc = Tamb + (NOCT - 20) * G / 800
// ============================================================================
double SolarPVModel::calculateCellTemperature(double G, double Tamb) const {
    return Tamb + (cell_.NOCT - 20.0) * G / 800.0;
}

// ============================================================================
// Corriente fotogenerada: Iph = Iph_ref * G/G_ref * [1 + alpha * (T - T_ref)]
// ============================================================================
double SolarPVModel::calculatePhotoCurrent(double G, double T) const {
    double G_ratio = G / 1000.0;
    return cell_.Iph_ref * G_ratio
           * (1.0 + cell_.alphaIsc * (T - 25.0));
}

// ============================================================================
// Corriente de saturacion: Is = Is_ref * (T/T_ref)^3 * exp(q*Eg/k*(1/T_ref - 1/T))
// Simplificado: ajuste exponencial
// ============================================================================
double SolarPVModel::calculateSaturationCurrent(double T) const {
    double T_ref = 298.15; // 25 C en Kelvin
    double T_K = T + 273.15;
    double Eg = 1.12; // eV - bandgap del silicio
    double k_q = 8.617e-5; // k/q en eV/K
    double factor = std::pow(T_K / T_ref, 3.0)
                    * std::exp(Eg / k_q * (1.0 / T_ref - 1.0 / T_K));
    return cell_.Is_ref * factor;
}

// ============================================================================
// Voltaje termico: Vt = k*T/q
// ============================================================================
double SolarPVModel::calculateThermalVoltage(double T) const {
    double T_K = T + 273.15;
    double k = 1.380649e-23;   // J/K
    double q = 1.602176634e-19; // C
    return k * T_K / q;        // V
}

// ============================================================================
// Funcion Lambert W (rama W0) - metodo de Halley
// ============================================================================
double SolarPVModel::lambertW(double z, double tol, int maxIter) const {
    if (z < -1.0 / std::exp(1.0)) return std::numeric_limits<double>::quiet_NaN();
    if (z == 0.0) return 0.0;

    // Aproximacion inicial
    double w;
    if (z > 1.0) {
        w = std::log(z) - std::log(std::log(z));
    } else if (z > -1.0 / std::exp(1.0)) {
        w = z;
    } else {
        w = -1.0;
    }

    // Iteracion de Halley
    for (int i = 0; i < maxIter; ++i) {
        double expw = std::exp(w);
        double wexpw = w * expw;
        double dw = (wexpw - z) / (expw * (w + 1.0) - (w + 2.0) * (wexpw - z) / (2.0 * w + 2.0));
        w -= dw;
        if (std::abs(dw) < tol) break;
    }
    return w;
}

// ============================================================================
// Solucion de la ecuacion del modelo de 5 parametros
// Usando la funcion Lambert W para solucion exacta
// I = (Iph + Is - V/Rsh) / (1 + Rs/Rsh) -
//     (n*Vt/Rs) * W0( (Rs*Is/(n*Vt*(1+Rs/Rsh))) * exp( (V+Rs*(Iph+Is))/(n*Vt*(1+Rs/Rsh)) ) )
// ============================================================================
double SolarPVModel::solveCellCurrent(double V, double Iph, double Is,
                                       double nVt, double Rs, double Rsh,
                                       double tol, int maxIter) const {
    if (Rs < 1e-10) {
        // Caso sin resistencia serie: solucion directa
        double arg = (Iph + Is - V / Rsh) / Is;
        if (arg <= 0) return 0.0;
        return Iph - Is * (std::exp(V / nVt) - 1.0) - V / Rsh;
    }

    double denom = 1.0 + Rs / Rsh;
    double a = Rs * Is / (nVt * denom);
    double b = (V + Rs * (Iph + Is)) / (nVt * denom);

    if (a * std::exp(b) > 1e300) {
        // Overflow: corriente muy baja
        return 0.0;
    }

    double Warg = a * std::exp(b);
    double W = lambertW(Warg);
    if (std::isnan(W) || std::isinf(W)) {
        // Fallback: metodo iterativo
        double I = Iph;
        for (int i = 0; i < maxIter; ++i) {
            double exp_term = std::exp((V + Rs * I) / nVt);
            double f = Iph - Is * (exp_term - 1.0) - (V + Rs * I) / Rsh - I;
            double df = -Is * exp_term * Rs / nVt - Rs / Rsh - 1.0;
            if (std::abs(df) < 1e-15) break;
            double dI = f / df;
            I -= dI;
            if (std::abs(dI) < tol) break;
        }
        return std::max(0.0, I);
    }

    double I = (Iph + Is - V / Rsh) / denom - nVt * W / Rs;
    return std::max(0.0, I);
}

// ============================================================================
// Corriente de celda dado voltaje (publico)
// ============================================================================
double SolarPVModel::calculateCellCurrent(double V, double Iph, double Is,
                                            double Rs, double Rsh,
                                            double nVt) const {
    return solveCellCurrent(V, Iph, Is, nVt, Rs, Rsh);
}

// ============================================================================
// Voltaje de celda dado corriente (publico) - inverso del modelo
// ============================================================================
double SolarPVModel::calculateCellVoltage(double I, double Iph, double Is,
                                            double Rs, double Rsh,
                                            double nVt) const {
    // V = n*Vt * ln((Iph - I + Is*(1 - (V+Rs*I)/Rsh/Iph)) / Is) - Rs*I
    // Aproximacion iterativa
    double V_guess = cell_.Voc;
    for (int i = 0; i < 50; ++i) {
        double Vsh = (V_guess + Rs * I); // V+Rs*I
        double exp_term = (Iph + Is - I - Vsh / Rsh) / Is;
        if (exp_term <= 0) {
            V_guess = cell_.Vmp;
            break;
        }
        double V_new = nVt * std::log(exp_term) - Rs * I;
        if (std::abs(V_new - V_guess) < 1e-9) {
            V_guess = V_new;
            break;
        }
        V_guess = V_new;
    }
    return std::max(0.0, V_guess);
}

// ============================================================================
// Curva I-V completa del panel
// ============================================================================
std::vector<IVPoint> SolarPVModel::calculateIVCurve(int numPoints) {
    return calculateIVCurve(irradiance_, cellTemp_, numPoints);
}

std::vector<IVPoint> SolarPVModel::calculateIVCurve(double G, double T,
                                                     int numPoints) {
    std::vector<IVPoint> curve;
    curve.reserve(numPoints + 1);

    // Parametros a condiciones operativas
    double Iph = calculatePhotoCurrent(G, T);
    double Is = calculateSaturationCurrent(T);
    double nVt = cell_.n * calculateThermalVoltage(T);
    double Rs = cell_.Rs;
    double Rsh = cell_.Rsh;

    // Voltaje maximo: Voc aproximado
    double Voc_T = cell_.Voc * (1.0 + cell_.betaVoc * (T - 25.0));
    double dV = Voc_T / numPoints;

    for (int i = 0; i <= numPoints; ++i) {
        double Vcell = i * dV;
        double Icell = solveCellCurrent(Vcell, Iph, Is, nVt, Rs, Rsh);
        double Pcell = Vcell * Icell;

        IVPoint pt;
        pt.voltage = Vcell * array_.panelsInSeries;
        pt.current = Icell * array_.stringsInParallel;
        pt.power = Pcell * array_.panelsInSeries * array_.stringsInParallel
                   / 1e6; // MW
        curve.push_back(pt);
    }

    // Aplicar factores de perdida
    double degradation = calculateDegradationFactor();
    double shading = calculateShadingLoss();
    double mismatch = calculateMismatchLoss();
    double soiling = array_.soilingFactor;
    double wiring = array_.wiringLossFactor;
    double totalFactor = degradation * shading * mismatch * soiling * wiring;

    for (auto& pt : curve) {
        pt.power *= totalFactor;
        pt.current *= totalFactor;
    }

    return curve;
}

// ============================================================================
// Punto de maxima potencia (MPP)
// Usando busqueda en la curva I-V
// ============================================================================
MPPPoint SolarPVModel::calculateMPP() {
    return calculateMPP(irradiance_, cellTemp_);
}

MPPPoint SolarPVModel::calculateMPP(double G, double T) {
    MPPPoint mpp;
    mpp.Vmpp = 0.0;
    mpp.Impp = 0.0;
    mpp.Pmpp = 0.0;
    mpp.efficiency = 0.0;

    // Parametros a condiciones operativas
    double Iph = calculatePhotoCurrent(G, T);
    double Is = calculateSaturationCurrent(T);
    double nVt = cell_.n * calculateThermalVoltage(T);
    double Rs = cell_.Rs;
    double Rsh = cell_.Rsh;

    // Voltaje maximo aproximado
    double Voc_T = cell_.Voc * (1.0 + cell_.betaVoc * (T - 25.0));

    // Busqueda del MPP por muestreo fino
    double Vmpp_cell = 0.0;
    double Impp_cell = 0.0;
    double Pmax = 0.0;

    int nSamples = 1000;
    double dV = Voc_T / nSamples;

    for (int i = 0; i <= nSamples; ++i) {
        double V = i * dV;
        double I = solveCellCurrent(V, Iph, Is, nVt, Rs, Rsh);
        double P = V * I;
        if (P > Pmax) {
            Pmax = P;
            Vmpp_cell = V;
            Impp_cell = I;
        }
    }

    // Refinamiento con interpolacion parabolica
    if (Vmpp_cell > dV && Vmpp_cell < Voc_T - dV) {
        double V_left = Vmpp_cell - dV;
        double V_right = Vmpp_cell + dV;
        double I_left = solveCellCurrent(V_left, Iph, Is, nVt, Rs, Rsh);
        double I_right = solveCellCurrent(V_right, Iph, Is, nVt, Rs, Rsh);
        double P_left = V_left * I_left;
        double P_right = V_right * I_right;

        double a_num = P_left - 2.0 * Pmax + P_right;
        if (std::abs(a_num) > 1e-12) {
            double a = a_num / (dV * dV);
            double b = (P_right - P_left) / (2.0 * dV);
            double V_opt = Vmpp_cell - b / (2.0 * a);
            if (V_opt > 0 && V_opt < Voc_T) {
                double I_opt = solveCellCurrent(V_opt, Iph, Is, nVt, Rs, Rsh);
                double P_opt = V_opt * I_opt;
                if (P_opt > Pmax) {
                    Vmpp_cell = V_opt;
                    Impp_cell = I_opt;
                    Pmax = P_opt;
                }
            }
        }
    }

    // Escalar al array completo
    double totalFactor = calculateDegradationFactor()
                         * calculateShadingLoss()
                         * calculateMismatchLoss()
                         * array_.soilingFactor
                         * array_.wiringLossFactor;

    mpp.Vmpp = Vmpp_cell * array_.panelsInSeries;
    mpp.Impp = Impp_cell * array_.stringsInParallel;
    mpp.Pmpp = Pmax * array_.panelsInSeries * array_.stringsInParallel
               * totalFactor / 1e6; // MW

    // Eficiencia del panel
    double G_eff = G;
    double totalArea = cell_.area * getTotalPanels();
    if (totalArea > 0 && G_eff > 0) {
        mpp.efficiency = (Pmax * totalFactor / (G_eff * cell_.area)) * 100.0;
    }

    return mpp;
}

// ============================================================================
// Eficiencia del inversor como funcion de carga
// Modelo: eta = eta_nom - (1-eta_nom) * (1 - P/Pnom)^2
// ============================================================================
double SolarPVModel::calculateInverterEfficiency(double Pdc) const {
    double Pnom = inverter_.ratedPower; // kW
    if (Pnom < 1e-6) return inverter_.efficiencyNominal;
    double loading = Pdc / Pnom;
    if (loading <= 0.01) return inverter_.efficiencyMin;

    // Modelo de eficiencia tipico: curva con maximo en carga media-alta
    double eta = inverter_.efficiencyNominal;
    if (loading < 0.1) {
        eta = inverter_.efficiencyMin
              + (inverter_.efficiencyNominal - inverter_.efficiencyMin)
                * loading / 0.1;
    } else if (loading < 0.5) {
        eta = inverter_.efficiencyNominal
              - 0.01 * std::pow(1.0 - loading / 0.5, 2.0);
    }
    return std::max(inverter_.efficiencyMin, std::min(0.995, eta));
}

// ============================================================================
// THDi del inversor
// ============================================================================
double SolarPVModel::calculateTHDi(double Pac) const {
    double Pnom = inverter_.ratedPower;
    if (Pnom < 1e-6) return inverter_.thdiNominal;
    double loading = Pac / Pnom;
    if (loading < 0.05) return inverter_.thdiMinLoad;

    // THDi disminuye con la carga
    double thdi = inverter_.thdiNominal
                  + (inverter_.thdiMinLoad - inverter_.thdiNominal)
                    * (1.0 - loading);
    return std::min(inverter_.maxTHDi, std::max(inverter_.thdiNominal, thdi));
}

// ============================================================================
// Perdidas del inversor
// ============================================================================
double SolarPVModel::calculateInverterLosses(double Pdc) const {
    double eta = calculateInverterEfficiency(Pdc);
    return Pdc * (1.0 - eta); // kW
}

// ============================================================================
// Factores de perdida
// ============================================================================
double SolarPVModel::calculateDegradationFactor() const {
    return std::pow(1.0 - array_.degradationRate, array_.yearsInOperation);
}

double SolarPVModel::calculateShadingLoss() const {
    return array_.shadingFactor;
}

double SolarPVModel::calculateMismatchLoss() const {
    return array_.mismatchFactor;
}

// ============================================================================
// Potencia nominal DC y AC
// ============================================================================
double SolarPVModel::getRatedPowerDC() const {
    return cell_.Pmp * getTotalPanels() / 1e6; // MW
}

double SolarPVModel::getRatedPowerAC() const {
    return inverter_.ratedPower * array_.stringsInParallel / 1e3; // MW
}

// ============================================================================
// Calculo completo de salida AC
// ============================================================================
PVOutput SolarPVModel::calculateOutput() {
    return calculateOutput(irradiance_, cellTemp_, VterminalPu_);
}

PVOutput SolarPVModel::calculateOutput(double G, double T,
                                        double VterminalPu) {
    if (status_ == 0 || G < 1.0) {
        lastOutput_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0};
        return lastOutput_;
    }

    // Actualizar temperatura de celda si no se fijo manualmente
    cellTemp_ = calculateCellTemperature(G, ambientTemp_);

    // Calcular MPP
    MPPPoint mpp = calculateMPP(G, T);

    // Voltaje y corriente DC
    double Vdc = mpp.Vmpp;
    double Idc = mpp.Impp;
    double Pdc = mpp.Pmpp; // MW

    // Limitar Vdc al rango del inversor
    double Vdc_clamped = std::max(inverter_.minVoltageDC,
                                   std::min(inverter_.maxVoltageDC, Vdc));
    if (Vdc_clamped < inverter_.minVoltageDC) {
        // Inversor no arranca
        lastOutput_ = {0.0, 0.0, Vdc_clamped, 0.0, 0.0, 0.0, 0.0, 1.0, 1};
        return lastOutput_;
    }

    // Recalcular P si el voltaje fue limitado
    double Pdc_actual = Pdc;
    if (std::abs(Vdc_clamped - Vdc) > 1.0) {
        double ratio = Vdc_clamped / Vdc;
        Pdc_actual = Pdc * ratio; // Aproximacion
    }

    // Limitar a potencia nominal del inversor
    double Pac_max = getRatedPowerAC();
    Pdc_actual = std::min(Pdc_actual, Pac_max / inverter_.efficiencyNominal);

    // Eficiencia del inversor
    double eta_inv = calculateInverterEfficiency(Pdc_actual * 1e3); // kW input

    // Potencia AC
    double Pac = Pdc_actual * eta_inv;
    Pac = std::min(Pac, Pac_max);

    // THDi
    double thdi = calculateTHDi(Pac * 1e3);

    // Control de Q (Volt/VAr)
    double Qac = 0.0;
    if (VterminalPu < 0.95) {
        Qac = 0.44 * Pac; // Capacitivo - inyeccion de Q
    } else if (VterminalPu > 1.05) {
        Qac = -0.44 * Pac; // Inductivo - absorcion de Q
    } else if (pfSetpoint_ < 1.0) {
        Qac = Pac * std::sqrt(1.0 / (pfSetpoint_ * pfSetpoint_) - 1.0);
    }

    // LVRT: PF 0.9 durante falla
    if (VterminalPu < 0.88) {
        Qac = Pac * std::sqrt(1.0 / (0.9 * 0.9) - 1.0);
    }

    double S = std::sqrt(Pac * Pac + Qac * Qac);
    double pf = (S > 1e-6) ? Pac / S : 1.0;

    // Eficiencia total
    double G_total = G * getTotalPanels() * cell_.area;
    double eta_total = (G_total > 0) ? (Pac * 1e6 / G_total) * 100.0 : 0.0;

    lastOutput_ = {
        Pac,                           // Pac (MW)
        Qac,                           // Qac (MVAr)
        Vdc_clamped,                   // Vdc (V)
        Idc,                           // Idc (A)
        Pdc_actual,                    // Pdc (MW)
        eta_total,                     // eficiencia total (%)
        thdi,                          // THDi (%)
        pf,                            // factor de potencia
        status_
    };

    return lastOutput_;
}

} // namespace powsys365
