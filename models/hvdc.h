// ============================================================================
// hvdc.h
// Modelos HVDC: LCC, VSC, MTDC
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

namespace powsys365 {

// Constantes
constexpr double HVDC_PI = 3.14159265358979323846;
constexpr double HVDC_SQRT2 = 1.4142135623730951;
constexpr double HVDC_SQRT3 = 1.7320508075688772;
constexpr double HVDC_SQRT6 = 2.449489742783178;

// ============================================================================
// Modo de control VSC
// ============================================================================
enum VSCControlMode {
    VSC_PQ = 0,       // Control P-Q
    VSC_VDC_Q = 1,    // Control Vdc-Q
    VSC_VDC_VAC = 2,  // Control Vdc-Vac
    VSC_DROOP = 3     // Control droop
};

// ============================================================================
// Modo de control LCC
// ============================================================================
enum LCCControlMode {
    LCC_CC = 0,       // Constant current
    LCC_CEA = 1,      // Constant extinction angle
    LCC_CP = 2,       // Constant power
    LCC_CIDC = 3      // Constant Id (current)
};

// ============================================================================
// Resultado de flujo de potencia
// ============================================================================
struct HVDCPowerFlow {
    double Pdc;        // MW potencia DC
    double Vdc;        // kV voltaje DC
    double Idc;        // A corriente DC
    double Pac_from;   // MW potencia AC lado emisor
    double Pac_to;     // MW potencia AC lado receptor
    double Qac_from;   // MVAr reactivo AC lado emisor
    double Qac_to;     // MVAr reactivo AC lado receptor
    double Vac_from;   // kV voltaje AC lado emisor
    double Vac_to;     // kV voltaje AC lado receptor
    double Iac_from;   // A corriente AC lado emisor
    double Iac_to;     // A corriente AC lado receptor
    double losses;     // MW perdidas totales
    double alpha;      // Angulo de disparo (LCC)
    double gamma;      // Angulo de extincion (LCC)
    double mu;         // Angulo de overlap (LCC)
    double phi_from;   // Angulo de fase AC lado emisor
    double phi_to;     // Angulo de fase AC lado receptor
    double pf_from;    // Factor de potencia lado emisor
    double pf_to;      // Factor de potencia lado receptor
    int    status;     // Estado
};

// ============================================================================
// Terminal MTDC
// ============================================================================
struct MTDCTerminal {
    int    id;               // Identificador
    int    busId;            // Barra AC conectada
    std::string name;        // Nombre
    double ratedPower;       // MW
    double VdcRef;           // kV referencia
    double Pset;             // MW setpoint
    double droopCoeff;       // Coeficiente droop
    VSCControlMode controlMode; // Modo de control
    double Pac;              // MW medido
    double Qac;              // MVAr medido
    double Vdc;              // kV medido
    double Idc;              // A medido
    double Vac;              // kV AC
    double VacRef;           // kV referencia AC
    double pf;               // Factor de potencia
    int    status;           // 1=online, 0=offline
    bool   blackStartCapable; // Capacidad de black start

    MTDCTerminal(int id_ = 0, int busId_ = 0, const std::string& name_ = "",
                 double rated = 100.0, double VdcRef_ = 320.0)
        : id(id_), busId(busId_), name(name_), ratedPower(rated),
          VdcRef(VdcRef_), Pset(0.0), droopCoeff(0.05),
          controlMode(VSC_PQ), Pac(0.0), Qac(0.0), Vdc(VdcRef_),
          Idc(0.0), Vac(230.0), VacRef(1.0), pf(1.0), status(1),
          blackStartCapable(false) {}
};

// ============================================================================
// Submodulo MMC
// ============================================================================
struct MMCSubmodule {
    double capacitorVoltage;  // V
    double capacitorValue;    // F
    double voltageTolerance;  // %
    int    state;             // 0=off, 1=on, 2=bypass

    MMCSubmodule()
        : capacitorVoltage(2000.0), capacitorValue(10e-3),
          voltageTolerance(5.0), state(0) {}
};

// ============================================================================
// Clase: HVDCLCCModel - Line Commutated Converter
// ============================================================================
class HVDCLCCModel {
public:
    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    HVDCLCCModel();
    HVDCLCCModel(int id, int fromBus, int toBus, double ratedPower,
                 double Vdc, double Vac, int pulses = 12);

    // ------------------------------------------------------------------------
    // Setters
    // ------------------------------------------------------------------------
    void setFiringAngle(double alphaDeg);
    void setExtinctionAngle(double gammaDeg);
    void setCurrentOrder(double IdcRef);
    void setPowerOrder(double PdcRef);
    void setControlMode(LCCControlMode mode);
    void setTransformerRatio(double ratio);
    void setCommutationReactance(double Xc); // pu
    void setDCResistance(double Rdc); // Ohm
    void setStatus(int status);

    // ------------------------------------------------------------------------
    // Calculo principal
    // ------------------------------------------------------------------------
    HVDCPowerFlow getPowerFlow() const;

    // ------------------------------------------------------------------------
    // Voltajes DC
    // ------------------------------------------------------------------------
    double calculateRectifierVoltage(double alpha, double mu) const;
    double calculateInverterVoltage(double gamma, double mu) const;
    double calculateNoLoadVoltage() const;

    // ------------------------------------------------------------------------
    // Angulo de overlap mu
    // ------------------------------------------------------------------------
    double calculateOverlapAngle(double alpha, bool isInverter) const;

    // ------------------------------------------------------------------------
    // Perdidas
    // ------------------------------------------------------------------------
    double getLosses() const;
    double calculateACPowerLoss() const;
    double calculateDCPowerLoss() const;
    double getACCurrents() const; // A RMS AC
    double getDCCurrents() const; // A DC

    // ------------------------------------------------------------------------
    // Inversion de potencia
    // ------------------------------------------------------------------------
    void reversePowerFlow();
    bool canReversePower() const { return true; }

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    double getRatedPower() const { return ratedPower_; }
    double getVdc() const { return Vdc_; }
    double getIdc() const;
    double getAlpha() const { return alpha_; }
    double getGamma() const { return gamma_; }
    int    getPulseNumber() const { return pulseNumber_; }
    int    getStatus() const { return status_; }
    LCCControlMode getControlMode() const { return controlMode_; }

private:
    int    id_;
    int    fromBus_;
    int    toBus_;
    double ratedPower_;    // MW
    double Vdc_;           // kV
    double Vac_;           // kV AC
    int    pulseNumber_;   // 6 o 12 pulsos

    // Angulos
    double alpha_;         // grados - firing angle
    double gamma_;         // grados - extinction angle
    double gammaMin_;      // grados - min extinction angle
    double mu_;            // grados - overlap angle

    // Referencias
    double IdcRef_;        // A
    double PdcRef_;        // MW
    double VdcRef_;        // kV

    // Parametros
    double transformerRatio_; // Relacion transformador
    double commutationReactance_; // pu
    double dcResistance_;    // Ohm
    double transformerResistance_; // pu
    double transformerReactance_;  // pu
    double efficiency_;      // 0-1

    LCCControlMode controlMode_;
    int    status_;
    bool   powerReversed_;

    // Constantes del convertidor
    double getConverterConstant() const {
        return (pulseNumber_ == 12) ? 3.0 * HVDC_SQRT2 / HVDC_PI
                                    : 3.0 * HVDC_SQRT2 / (2.0 * HVDC_PI);
    }
};

// ============================================================================
// Clase: HVDCVSCModel - Voltage Source Converter
// ============================================================================
class HVDCVSCModel {
public:
    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    HVDCVSCModel();
    HVDCVSCModel(int id, int fromBus, int toBus, double ratedPower,
                 double Vdc, double Vac);

    // ------------------------------------------------------------------------
    // Setters
    // ------------------------------------------------------------------------
    void setControlMode(VSCControlMode mode);
    void setPowerReference(double P);    // MW
    void setReactiveReference(double Q); // MVAr
    void setDCVoltageReference(double VdcRef); // kV
    void setACVoltageReference(double VacRef); // pu
    void setDroopCoefficient(double droop);    // pu
    void setConverterLossFactor(double lossFactor);
    void setFilterParameters(double Rf, double Xf); // pu
    void setDCResistance(double Rdc); // Ohm
    void setStatus(int status);

    // ------------------------------------------------------------------------
    // Calculo principal
    // ------------------------------------------------------------------------
    HVDCPowerFlow getPowerFlow() const;

    // ------------------------------------------------------------------------
    // Controladores
    // ------------------------------------------------------------------------
    double calculatePQControl() const;
    double calculateVdcQControl() const;
    double calculateVdcVacControl() const;
    double calculateDroopControl() const;

    // ------------------------------------------------------------------------
    // MMC
    // ------------------------------------------------------------------------
    double calculateMMCVoltage() const;
    int    getNumberOfSubmodules() const;
    double getSubmoduleVoltage() const;
    double getArmCurrent() const;

    // ------------------------------------------------------------------------
    // Perdidas
    // ------------------------------------------------------------------------
    double getLosses() const;
    double getACCurrents() const; // A RMS
    double getDCCurrents() const; // A DC

    // ------------------------------------------------------------------------
    // Black start
    // ------------------------------------------------------------------------
    bool canBlackStart() const { return blackStartCapable_; }
    void setBlackStartCapable(bool capable) { blackStartCapable_ = capable; }
    double getBlackStartPower() const;

    // ------------------------------------------------------------------------
    // Inversion de potencia
    // ------------------------------------------------------------------------
    void reversePowerFlow();
    bool canReversePower() const { return true; }

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    double getRatedPower() const { return ratedPower_; }
    double getVdc() const { return Vdc_; }
    double getIdc() const;
    double getPacFrom() const { return Pac_from_; }
    double getPacTo() const { return Pac_to_; }
    VSCControlMode getControlMode() const { return controlMode_; }
    int    getStatus() const { return status_; }

private:
    int    id_;
    int    fromBus_;
    int    toBus_;
    double ratedPower_;   // MW
    double Vdc_;          // kV DC
    double Vac_;          // kV AC
    double Idc_;          // A DC

    // Control
    VSCControlMode controlMode_;
    double Pref_;         // MW
    double Qref_;         // MVAr
    double VdcRef_;       // kV
    double VacRef_;       // pu
    double droopCoeff_;   // pu

    // Parametros
    double converterLossFactor_; // 0.01 = 1%
    double filterR_;        // pu
    double filterX_;        // pu
    double dcResistance_;   // Ohm
    double transformerRatio_;
    double transformerX_;   // pu
    double modulationIndex_; // m = 0-1.15 (con third harmonic)
    double switchingFreq_;   // Hz

    double Pac_from_;
    double Pac_to_;
    double Qac_from_;
    double Qac_to_;

    int    status_;
    bool   powerReversed_;
    bool   blackStartCapable_;

    // MMC
    int    numSubmodules_;  // Por brazo
    double submoduleCapacitance_; // F
    double submoduleVoltageRated_; // V

    double calculateConverterLosses(double P) const;
    double calculateACVoltage(double Vdc) const;
};

// ============================================================================
// Clase: HVDCMTDCModel - Multi-Terminal DC
// ============================================================================
class HVDCMTDCModel {
public:
    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    HVDCMTDCModel();
    explicit HVDCMTDCModel(int numTerminals);

    // ------------------------------------------------------------------------
    // Gestion de terminales
    // ------------------------------------------------------------------------
    void addTerminal(const MTDCTerminal& terminal);
    void removeTerminal(int terminalId);
    MTDCTerminal* getTerminal(int terminalId);
    const MTDCTerminal* getTerminal(int terminalId) const;
    size_t getNumTerminals() const { return terminals_.size(); }

    // ------------------------------------------------------------------------
    // Configuracion
    // ------------------------------------------------------------------------
    void setDCCableResistance(int fromTerm, int toTerm, double R); // Ohm
    void setTerminalPowerReference(int terminalId, double P);
    void setTerminalControlMode(int terminalId, VSCControlMode mode);
    void setTerminalDroopCoeff(int terminalId, double droop);
    void setTerminalVdcRef(int terminalId, double Vdc);
    void setTerminalVacRef(int terminalId, double Vac);
    void setStatus(int status);

    // ------------------------------------------------------------------------
    // Calculo principal: power flow MTDC
    // ------------------------------------------------------------------------
    std::vector<HVDCPowerFlow> getPowerFlow();

    // ------------------------------------------------------------------------
    // Control de voltaje DC
    // ------------------------------------------------------------------------
    double calculateDCVoltage() const;
    void balanceDCVoltage();

    // ------------------------------------------------------------------------
    // Asignacion de potencia
    // ------------------------------------------------------------------------
    void allocatePower(double totalPower);
    double calculatePowerAllocation(int terminalId) const;

    // ------------------------------------------------------------------------
    // Perdidas
    // ------------------------------------------------------------------------
    double getTotalLosses() const;
    double getCablesLosses() const;

    // ------------------------------------------------------------------------
    // Corrientes AC/DC por terminal
    // ------------------------------------------------------------------------
    std::vector<double> getACCurrents() const;
    std::vector<double> getDCCurrents() const;

    // ------------------------------------------------------------------------
    // Black start MTDC
    // ------------------------------------------------------------------------
    bool canBlackStart() const;
    int  getBlackStartTerminal() const;

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    const std::vector<MTDCTerminal>& getTerminals() const { return terminals_; }
    int  getStatus() const { return status_; }

private:
    std::vector<MTDCTerminal> terminals_;
    std::vector<std::vector<double>> cableResistance_; // Matriz R[i][j]
    double VdcNominal_;   // kV
    double totalLosses_;
    int    status_;

    // Resolver power flow DC
    bool solveDCPowerFlow();
    void calculateCableLosses();
};

} // namespace powsys365
