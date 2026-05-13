// ============================================================================
// facts.h
// Modelos FACTS: SVC, STATCOM, TCSC, UPFC, MMC
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// Modos de operacion FACTS
// ============================================================================
enum FACTSOperatingMode {
    FACTS_MODE_STANDBY = 0,
    FACTS_MODE_VOLTAGE_REGULATION = 1,
    FACTS_MODE_VAR_COMPENSATION = 2,
    FACTS_MODE_POWER_FLOW_CONTROL = 3,
    FACTS_MODE_DAMPING = 4,
    FACTS_MODE_IMPEDANCE_CONTROL = 5,
    FACTS_MODE_UNIFIED_CONTROL = 6
};

// ============================================================================
// Punto de operacion
// ============================================================================
struct FACTSOperatingPoint {
    double P;          // MW potencia activa
    double Q;          // MVAr potencia reactiva
    double V;          // pu voltaje
    double I;          // pu corriente
    double Bsvc;       // pu susceptancia SVC
    double Xtcsc;      // Ohm reactancia TCSC
    double Vser;       // pu voltaje serie UPFC
    double thetaSer;   // rad angulo serie UPFC
    double Vsh;        // pu voltaje shunt UPFC
    double thetaSh;    // rad angulo shunt UPFC
    double modulation; // indice de modulacion
    int    mode;       // modo operativo
    int    status;     // estado
};

// ============================================================================
// Clase: SVCModel - Static Var Compensator
// ============================================================================
class SVCModel {
public:
    // TCR + TSC branches
    SVCModel();
    SVCModel(int id, int busId, double Qmax, double Qmin = 0.0);

    void setOperatingMode(FACTSOperatingMode mode);
    void setVoltageReference(double Vref); // pu
    void setVoltageMeasured(double Vmeas); // pu
    void setGain(double Kv);               // ganancia del regulador
    void setTimeConstant(double Tc);       // s
    void setTCRLimits(double Bmax, double Bmin); // pu
    void setTSCCapacitorSteps(const std::vector<double>& steps); // pu
    void setStatus(int status);

    // Calculo principal
    double calculateInjection(); // MVAr
    double calculateBsvc();      // pu susceptancia
    double calculateBref();      // pu susceptancia de referencia

    // Seleccion de pasos TSC
    std::vector<bool> selectTSCSteps(double Bneeded);

    // Punto de operacion
    FACTSOperatingPoint getOperatingPoint() const;

    // Getters
    double getQout() const { return Qout_; }
    double getBsvc() const { return Bsvc_; }
    double getVref() const { return Vref_; }
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    busId_;
    double Qmax_;        // MVAr
    double Qmin_;        // MVAr
    double Vref_;        // pu
    double Vmeasured_;   // pu
    double Kv_;          // ganancia
    double Tc_;          // s
    double Bsvc_;        // pu susceptancia total
    double BsvcMax_;     // pu max
    double BsvcMin_;     // pu min
    double Btcr_;        // pu TCR (negativo = inductivo)
    double Btsc_;        // pu TSC (positivo = capacitivo)
    double Qout_;        // MVAr inyectado
    int    status_;
    FACTSOperatingMode mode_;

    std::vector<double> tscSteps_; // Pu de cada paso TSC
    std::vector<bool>   tscState_; // Estado de cada paso

    double BtcrMin_; // pu
    double BtcrMax_; // pu
};

// ============================================================================
// Clase: STATCOMModel - Static Synchronous Compensator
// ============================================================================
class STATCOMModel {
public:
    STATCOMModel();
    STATCOMModel(int id, int busId, double rating);

    void setOperatingMode(FACTSOperatingMode mode);
    void setVoltageReference(double Vref); // pu
    void setVoltageMeasured(double Vmeas); // pu
    void setReactiveReference(double Qref); // MVAr
    void setSourceImpedance(double R, double X); // pu
    void setPWMParams(double carrierFreq, double modIndex);
    void setCurrentLimits(double Imax); // pu
    void setStatus(int status);

    // Control VSI
    double calculateInjection();       // MVAr
    double calculateVoltageSource();   // pu voltaje interno
    double calculateCurrentInjection(); // pu corriente
    double calculateReactivePower();   // MVAr

    // Modelo de fuente de voltaje con impedancia interna
    double calculateInternalVoltage(double Q) const;
    double calculateInternalAngle(double P, double Q) const;

    FACTSOperatingPoint getOperatingPoint() const;

    // Getters
    double getQout() const { return Qout_; }
    double getVsource() const { return Vsource_; }
    double getIout() const { return Iout_; }
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    busId_;
    double rating_;       // MVA
    double Vref_;         // pu
    double Vmeasured_;    // pu
    double Qref_;         // MVAr
    double Rsource_;      // pu
    double Xsource_;      // pu
    double carrierFreq_;  // Hz
    double modIndex_;     // 0-1.15
    double Imax_;         // pu
    double Qout_;         // MVAr
    double Vsource_;      // pu voltaje interno
    double thetaSource_;  // rad angulo interno
    double Iout_;         // pu
    int    status_;
    FACTSOperatingMode mode_;

    double integralError_; // Para PI control
};

// ============================================================================
// Clase: TCSCModel - Thyristor Controlled Series Capacitor
// ============================================================================
class TCSCModel {
public:
    TCSCModel();
    TCSCModel(int id, int fromBus, int toBus, double Xnominal,
              double Xmin, double Xmax);

    void setOperatingMode(FACTSOperatingMode mode);
    void setCompensationLevel(double comp); // 0-1
    void setReactance(double Xtcsc); // Ohm
    void setPowerFlowReference(double Pref); // MW
    void setPowerFlowMeasured(double Pmeas); // MW
    void setFiringAngle(double alphaDeg); // grados
    void setStatus(int status);

    // Control de reactancia variable
    double calculateReactance();       // Ohm Xtcsc efectivo
    double calculateEffectiveX();      // Ohm con compensacion
    double calculateCompensationLevel() const; // 0-1
    double calculatePowerFlowControl(); // MW controlado

    // Modelo TCSC: Xtcsc(alpha)
    double calculateXfromFiringAngle(double alphaDeg) const;
    double calculateFiringAngleFromX(double Xdesired) const;

    FACTSOperatingPoint getOperatingPoint() const;

    // Getters
    double getXtcsc() const { return Xtcsc_; }
    double getXnominal() const { return Xnominal_; }
    double getEffectiveReactance() const { return Xeff_; }
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    fromBus_;
    int    toBus_;
    double Xnominal_;    // Ohm
    double Xmin_;        // Ohm
    double Xmax_;        // Ohm
    double Xtcsc_;       // Ohm actual
    double Xeff_;        // Ohm efectivo
    double Xc_;          // Ohm capacitivo base
    double compLevel_;   // 0-1
    double Pref_;        // MW
    double Pmeas_;       // MW
    double alphaDeg_;    // grados firing angle
    double alphaMin_;    // 145 grados
    double alphaMax_;    // 180 grados
    int    status_;
    FACTSOperatingMode mode_;
};

// ============================================================================
// Clase: UPFCModel - Unified Power Flow Controller
// ============================================================================
class UPFCModel {
public:
    UPFCModel();
    UPFCModel(int id, int fromBus, int toBus,
              double seriesRating, double shuntRating);

    void setOperatingMode(FACTSOperatingMode mode);
    void setSeriesVoltage(double Vser, double thetaSer); // pu, rad
    void setShuntVoltage(double Vsh, double thetaSh);    // pu, rad
    void setVoltageReference(double Vref); // pu
    void setAngleReference(double thetaref); // rad
    void setImpedanceReference(double Xref); // Ohm
    void setPowerReferences(double Pref, double Qref); // MW, MVAr
    void setSeriesRating(double rating); // MVA
    void setShuntRating(double rating); // MVA
    void setStatus(int status);

    // Control combinado
    double calculateSeriesInjection();    // pu voltaje serie inyectado
    double calculateShuntInjection();     // pu voltaje shunt inyectado
    double calculateSeriesReactive();     // MVAr serie
    double calculateShuntReactive();      // MVAr shunt
    double calculateActivePowerTransfer(); // MW transferido
    double calculateReactivePowerControl(); // MVAr controlado

    // Modelo de inyeccion de potencia
    double calculateRealPowerInjection(); // MW
    double calculateReactivePowerInjection(); // MVAr

    // Control independiente: voltaje, angulo, impedancia
    void setVoltageControl(double Vref, double Kv);
    void setAngleControl(double thetaref, double Ktheta);
    void setImpedanceControl(double Xref, double Kx);

    FACTSOperatingPoint getOperatingPoint() const;

    // Getters
    double getSeriesVoltage() const { return Vser_; }
    double getSeriesAngle() const { return thetaSer_; }
    double getShuntVoltage() const { return Vsh_; }
    double getShuntAngle() const { return thetaSh_; }
    double getQseries() const { return Qser_; }
    double getQshunt() const { return Qsh_; }
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    fromBus_;
    int    toBus_;
    double seriesRating_;  // MVA
    double shuntRating_;   // MVA
    double Vser_;          // pu voltaje serie
    double thetaSer_;      // rad angulo serie
    double Vsh_;           // pu voltaje shunt
    double thetaSh_;       // rad angulo shunt
    double Vref_;          // pu referencia
    double thetaRef_;      // rad referencia
    double Xref_;          // Ohm referencia
    double Pref_;          // MW
    double Qref_;          // MVAr
    double Kv_;            // ganancia voltaje
    double Ktheta_;        // ganancia angulo
    double Kx_;            // ganancia impedancia
    double Qser_;          // MVAr serie
    double Qsh_;           // MVAr shunt
    double Ptransferred_;  // MW
    int    status_;
    FACTSOperatingMode mode_;
};

// ============================================================================
// Clase: MMCModel - Modular Multilevel Converter
// ============================================================================
class MMCModel {
public:
    struct Submodule {
        double capacitorVoltage;  // V
        double capacitance;       // F
        int    state;             // 0=off, 1=inserted, 2=bypass
    };

    MMCModel();
    MMCModel(int id, int numArms, int submodulesPerArm,
             double submoduleCapacitance, double submoduleVoltage,
             double ratedPower);

    void setOperatingMode(FACTSOperatingMode mode);
    void setDCVoltage(double Vdc);         // kV
    void setACVoltage(double Vac);         // kV
    void setPowerReference(double P);      // MW
    void setReactiveReference(double Q);   // MVAr
    void setArmInductance(double Larm);    // H
    void setArmResistance(double Rarm);    // Ohm
    void setCirculatingCurrent(double Icirc); // A
    void setStatus(int status);

    // Balanceo de capacitores
    void balanceCapacitors();
    double getAverageCapacitorVoltage() const;
    double getCapacitorVoltageRipple() const;
    double getMaxCapacitorDeviation() const;

    // Control de brazos
    double calculateUpperArmVoltage() const;   // kV
    double calculateLowerArmVoltage() const;   // kV
    double calculateArmCurrent() const;        // A
    double calculateCirculatingCurrent() const; // A
    double calculateArmInductorVoltage() const; // kV

    // Control de inyeccion
    double calculateInjection();              // MVAr
    double calculateActivePowerInjection();   // MW
    double calculateReactivePowerInjection(); // MVAr
    double calculateModulationIndex();

    // Sorting y balanceo
    void sortAndBalance();
    int  selectSubmodule(bool insert, double armCurrent);

    FACTSOperatingPoint getOperatingPoint() const;

    // Getters
    double getVdc() const { return Vdc_; }
    double getVac() const { return Vac_; }
    double getPout() const { return Pout_; }
    double getQout() const { return Qout_; }
    int    getNumSubmodules() const { return numSubmodulesPerArm_; }
    double getSubmoduleVoltage() const;
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    numArms_;
    int    numSubmodulesPerArm_;
    double submoduleCapacitance_;  // F
    double submoduleVoltageRated_; // V
    double ratedPower_;            // MW
    double Vdc_;                   // kV
    double Vac_;                   // kV
    double Pref_;                  // MW
    double Qref_;                  // MVAr
    double Larm_;                  // H inductancia brazo
    double Rarm_;                  // Ohm resistencia brazo
    double Icirc_;                 // A corriente circulante
    double Pout_;                  // MW
    double Qout_;                  // MVAr
    int    status_;
    FACTSOperatingMode mode_;

    std::vector<std::vector<Submodule>> arms_; // [arm][submodule]

    double modulationIndex_;
    double switchingFreq_;

    // Metodos internos
    double calculateInsertionIndex(int arm) const;
    void updateCapacitorVoltages(double dt);
};

} // namespace powsys365
