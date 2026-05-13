// ============================================================================
// solar_pv.h
// Modelo fotovoltaico CIRED / IEEE 1547
// Panel PV con modelo de 5 parametros + inversor
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <utility>

namespace powsys365 {

// ============================================================================
// Punto de la curva I-V
// ============================================================================
struct IVPoint {
    double voltage;    // V
    double current;    // A
    double power;      // W
};

// ============================================================================
// Punto de maxima potencia (MPP)
// ============================================================================
struct MPPPoint {
    double Vmpp;       // V
    double Impp;       // A
    double Pmpp;       // W
    double efficiency; // %
};

// ============================================================================
// Salida del sistema PV
// ============================================================================
struct PVOutput {
    double Pac;        // Potencia activa AC (MW)
    double Qac;        // Potencia reactiva AC (MVAr)
    double Vdc;        // Voltaje DC (V)
    double Idc;        // Corriente DC (A)
    double Pdc;        // Potencia DC (MW)
    double efficiency; // Eficiencia total (%)
    double thdi;       // THDi total (%)
    double pf;         // Factor de potencia
    int    status;     // Estado operativo
};

// ============================================================================
// Modelo del inversor
// ============================================================================
struct InverterModel {
    double ratedPower = 100.0;       // kW
    double minVoltageDC = 500.0;     // V
    double maxVoltageDC = 1000.0;    // V
    double nominalVoltageDC = 600.0; // V
    double efficiencyNominal = 0.98; // 98% a potencia nominal
    double efficiencyMin = 0.95;     // 95% a minima potencia
    double switchingFreq = 3000.0;   // Hz
    double filterInductance = 0.05;  // pu
    double filterCapacitance = 0.02; // pu
    double thdiNominal = 3.0;        // % THDi a potencia nominal
    double thdiMinLoad = 5.0;        // % THDi a carga minima
    double maxTHDi = 5.0;            // % THDi maximo segun estandar
    double vRegulationRange = 0.05;  // +/- 5% de regulacion
    double pfMin = 0.9;              // Factor de potencia minimo
    double responseTime = 0.1;       // s
};

// ============================================================================
// Parametros de celda PV (modelo de 5 parametros)
// ============================================================================
struct CellParameters {
    // Parametros estandar (STC: 1000 W/m2, 25 C, AM1.5)
    double Iph_ref = 8.5;        // A - fotocorriente
    double Is_ref = 2.5e-10;     // A - corriente de saturacion
    double Rs = 0.004;           // Ohm - resistencia serie
    double Rsh = 10.0;           // Ohm - resistencia shunt
    double n = 1.0;              // Factor de idealidad
    double Isc = 8.8;            // A - corriente de cortocircuito
    double Voc = 44.0;           // V - voltaje de circuito abierto
    double Vmp = 36.0;           // V - voltaje MPP
    double Imp = 8.2;            // A - corriente MPP
    double Pmp = 295.0;          // W - potencia MPP
    double alphaIsc = 0.0006;    // A/C - coef. temp Isc
    double betaVoc = -0.003;     // V/C - coef. temp Voc
    double gammaPmp = -0.004;    // 1/C - coef. temp Pmp
    double NOCT = 45.0;          // C - Nominal Operating Cell Temperature
    int    cellsInSeries = 72;   // Celdas en serie
    double area = 1.95;          // m^2 - area de la celda
};

// ============================================================================
// Parametros del string/array PV
// ============================================================================
struct ArrayParameters {
    int panelsInSeries = 20;     // Paneles en serie por string
    int stringsInParallel = 100; // Strings en paralelo
    int numStrings = 10;         // Numero de strings totales
    double tiltAngle = 30.0;     // grados
    double azimuthAngle = 180.0; // grados (sur = 180)
    double albedo = 0.2;         // Coeficiente de reflexion
    double soilingFactor = 0.98; // Factor de ensuciamiento
    double mismatchFactor = 0.98; // Factor de mismatch
    double wiringLossFactor = 0.98; // Perdidas cableado
    double degradationRate = 0.005; // 0.5% anual
    int    yearsInOperation = 0; // Anos de operacion
    double shadingFactor = 1.0;  // 1.0 = sin sombra, 0.0 = totalmente sombreado
};

// ============================================================================
// Clase principal: SolarPVModel
// ============================================================================
class SolarPVModel {
public:
    enum ShadingType {
        SHADING_NONE = 0,
        SHADING_UNIFORM = 1,
        SHADING_NONUNIFORM = 2,
        SHADING_BYPASS = 3
    };

    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    SolarPVModel();
    explicit SolarPVModel(const CellParameters& cellParams,
                          const ArrayParameters& arrayParams,
                          const InverterModel& inverter);

    // ------------------------------------------------------------------------
    // Setters de condiciones ambientales
    // ------------------------------------------------------------------------
    void setIrradiance(double irradiance);        // W/m^2
    void setTemperature(double ambientTemp);      // C
    void setCellTemperature(double cellTemp);     // C (directo)
    void setNOCT(double noct);                    // C

    // ------------------------------------------------------------------------
    // Setters de configuracion
    // ------------------------------------------------------------------------
    void setCellParameters(const CellParameters& params);
    void setArrayParameters(const ArrayParameters& params);
    void setInverterModel(const InverterModel& inv);
    void setTerminalVoltage(double Vpu);
    void setPowerFactor(double pf);

    // ------------------------------------------------------------------------
    // Modelo de celda: ecuacion I = Iph - Is*(exp((V+Rs*I)/(n*Vt))-1) - (V+Rs*I)/Rsh
    // ------------------------------------------------------------------------
    double calculateCellCurrent(double V, double Iph, double Is,
                                 double Rs, double Rsh, double nVt) const;
    double calculateCellVoltage(double I, double Iph, double Is,
                                 double Rs, double Rsh, double nVt) const;

    // ------------------------------------------------------------------------
    // Calculo de parametros a condiciones operativas
    // ------------------------------------------------------------------------
    double calculatePhotoCurrent(double G, double T) const;
    double calculateSaturationCurrent(double T) const;
    double calculateThermalVoltage(double T) const;
    double calculateCellTemperature(double G, double Tamb) const;

    // ------------------------------------------------------------------------
    // Curva I-V completa
    // ------------------------------------------------------------------------
    std::vector<IVPoint> calculateIVCurve(int numPoints = 200);
    std::vector<IVPoint> calculateIVCurve(double G, double T, int numPoints = 200);

    // ------------------------------------------------------------------------
    // Punto de maxima potencia (MPP)
    // ------------------------------------------------------------------------
    MPPPoint calculateMPP();
    MPPPoint calculateMPP(double G, double T);

    // ------------------------------------------------------------------------
    // Calculo de salida AC completa
    // ------------------------------------------------------------------------
    PVOutput calculateOutput();
    PVOutput calculateOutput(double G, double T, double VterminalPu = 1.0);

    // ------------------------------------------------------------------------
    // Modelo del inversor
    // ------------------------------------------------------------------------
    double calculateInverterEfficiency(double Pdc) const;
    double calculateTHDi(double Pac) const;
    double calculateInverterLosses(double Pdc) const;

    // ------------------------------------------------------------------------
    // Degradacion y sombreado
    // ------------------------------------------------------------------------
    double calculateDegradationFactor() const;
    double calculateShadingLoss() const;
    double calculateMismatchLoss() const;

    // ------------------------------------------------------------------------
    // Numero total de paneles y potencia nominal
    // ------------------------------------------------------------------------
    int getTotalPanels() const {
        return array_.panelsInSeries * array_.stringsInParallel;
    }
    double getRatedPowerDC() const;    // MW
    double getRatedPowerAC() const;    // MW
    double getCurrentPower() const { return lastOutput_.Pac; }

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    double getIrradiance() const { return irradiance_; }
    double getTemperature() const { return ambientTemp_; }
    double getCellTemperature() const { return cellTemp_; }
    const CellParameters& getCellParams() const { return cell_; }
    const ArrayParameters& getArrayParams() const { return array_; }
    const InverterModel& getInverter() const { return inverter_; }
    const PVOutput& getLastOutput() const { return lastOutput_; }

private:
    CellParameters cell_;
    ArrayParameters array_;
    InverterModel inverter_;

    double irradiance_ = 1000.0;   // W/m^2
    double ambientTemp_ = 25.0;    // C
    double cellTemp_ = 25.0;       // C
    double VterminalPu_ = 1.0;     // pu
    double pfSetpoint_ = 1.0;      // Factor de potencia
    int    status_ = 1;

    PVOutput lastOutput_;

    // Metodos internos
    double solveCellCurrent(double V, double Iph, double Is,
                            double nVt, double Rs, double Rsh,
                            double tol = 1e-9, int maxIter = 100) const;
    double lambertW(double z, double tol = 1e-10, int maxIter = 100) const;
};

} // namespace powsys365
