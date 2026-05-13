// ============================================================================
// wind_turbine.h
// Modelo de turbina eolica IEC 61400-27-1 y WECC
// Tipos 1-4: Squirrel Cage, Wound Rotor, DFIG, Full Converter
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <utility>

namespace powsys365 {

// Constantes matematicas
constexpr double WT_PI = 3.14159265358979323846;
constexpr double WT_RHO_AIR = 1.225;          // kg/m^3 a nivel del mar
constexpr double WT_G = 9.81;                 // m/s^2

// ============================================================================
// Punto de la curva de potencia
// ============================================================================
struct PowerCurvePoint {
    double windSpeed;   // m/s
    double power;       // MW
    double cp;          // Coeficiente de potencia
    double tsr;         // Tip Speed Ratio
    double pitchAngle;  // grados
};

// ============================================================================
// Resultado electrico de la turbina
// ============================================================================
struct ElectricalOutput {
    double P;           // Potencia activa (MW)
    double Q;           // Potencia reactiva (MVAr)
    double Vterminal;   // Voltaje terminal (pu)
    double Iterminal;   // Corriente terminal (pu)
    double slip;        // Deslizamiento (pu)
    double omegaRotor;  // Velocidad rotor (pu)
    double omegaGen;    // Velocidad generador (pu)
    double pitchAngle;  // Angulo de pitch (grados)
    int    status;      // Estado operativo
};

// ============================================================================
// Parametros de la turbina eolica
// ============================================================================
struct WindTurbineParameters {
    // Parametros mecanicos
    double ratedPower = 2.0;          // MW
    double cutInSpeed = 3.0;          // m/s
    double ratedSpeed = 12.0;         // m/s
    double cutOutSpeed = 25.0;        // m/s
    double rotorDiameter = 80.0;      // m
    double hubHeight = 80.0;          // m
    double airDensity = 1.225;        // kg/m^3
    double maxPitchAngle = 30.0;      // grados
    double minPitchAngle = 0.0;       // grados
    double pitchRateMax = 10.0;       // grados/s

    // Parametros de transmision
    double gearRatio = 1.0;           // Relacion de engranaje
    double driveTrainInertia = 3.0;   // s (conste H)
    double shaftStiffness = 0.3;      // pu
    double shaftDamping = 0.01;       // pu

    // Parametros electricos generales
    double statorResistance = 0.01;   // pu
    double statorReactance = 0.15;    // pu
    double magnetizingReactance = 4.0; // pu
    double baseVoltage = 0.69;        // kV
    double baseFrequency = 50.0;      // Hz

    // Parametros Type 1: Jaula de ardilla
    double rotorResistanceT1 = 0.018; // pu
    double rotorReactanceT1 = 0.18;   // pu
    double capacitorCompensation = 0.33; // pu compensacion del capacitor

    // Parametros Type 2: Rotor bobinado
    double rotorResistanceT2 = 0.018; // pu
    double rotorReactanceT2 = 0.18;   // pu
    double externalResistanceMax = 0.05; // pu resistencia externa maxima

    // Parametros Type 3: DFIG
    double rotorResistanceT3 = 0.009; // pu
    double rotorReactanceT3 = 0.09;   // pu
    double converterRatedPower = 0.35; // pu del rated power (30% aprox)
    double dcLinkVoltage = 1200.0;    // V
    double dcLinkCapacitance = 0.06;  // F
    double crowbarResistance = 0.5;   // pu

    // Parametros Type 4: Full Converter
    double rotorResistanceT4 = 0.009; // pu (solo para el generador sincrono
    double rotorReactanceT4 = 0.09;   // pu  o PMSM interno)
    double converterRatedPowerT4 = 1.1; // pu (110% oversizing)
    double dcLinkVoltageT4 = 1200.0;  // V
    double dcLinkCapacitanceT4 = 0.06; // F
    double filterResistance = 0.001;  // pu
    double filterReactance = 0.15;    // pu
    double switchingFrequency = 3000.0; // Hz

    // Parametros LVRT (Low Voltage Ride Through)
    double lvrtVthreshold = 0.9;      // pu
    double lvrtVmin = 0.15;           // pu
    double lvrtReactiveSupport = 2.0; // pu inyeccion reactiva durante LVRT
    double lvrtTripTime = 0.16;       // s
    double lvrtPfactor = 1.0;         // factor de P durante LVRT

    // Coeficientes aerodinamicos Cp (modelo tipico)
    double cpOpt = 0.48;              // Cp optimo
    double tsrOpt = 6.9;              // TSR optimo
    double cpOffset = 0.0068;         // offset lineal de Cp

    // Parametros de control
    double kpPitch = 1.5;             // ganancia proporcional pitch
    double kiPitch = 0.3;             // ganancia integral pitch
    double kpSpeed = 0.6;             // ganancia proporcional velocidad
    double kiSpeed = 0.5;             // ganancia integral velocidad
};

// ============================================================================
// Clase principal: WindTurbineModel
// Modelo completo IEC 61400-27-1 para tipos 1-4
// ============================================================================
class WindTurbineModel {
public:
    enum TurbineType {
        TYPE1_SQUIRREL_CAGE = 1,    // Induccion jaula de ardilla + capacitor
        TYPE2_WOUND_ROTOR = 2,       // Rotor bobinado + resistencia variable
        TYPE3_DFIG = 3,              // Doblemente alimentado
        TYPE4_FULL_CONVERTER = 4     // Convertidor completo
    };

    enum OperatingMode {
        MODE_NORMAL = 0,
        MODE_LVRT = 1,
        MODE_PARKING = 2,
        MODE_STARTING = 3
    };

    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    WindTurbineModel();
    WindTurbineModel(TurbineType type, const WindTurbineParameters& params);

    // ------------------------------------------------------------------------
    // Setters de configuracion
    // ------------------------------------------------------------------------
    void setTurbineType(TurbineType type);
    void setTurbineParameters(const WindTurbineParameters& params);
    void setWindSpeed(double windSpeed);
    void setAirDensity(double rho);
    void setPitchAngle(double betaDeg);
    void setRotorSpeed(double omega);
    void setTerminalVoltage(double Vpu);
    void setOperatingMode(OperatingMode mode);

    // ------------------------------------------------------------------------
    // Modelo aerodinamico: Cp(lambda, beta)
    // ------------------------------------------------------------------------
    double calculateCp(double lambda, double betaDeg) const;
    double calculateCpOptimal(double lambda) const;
    double calculateTSR(double windSpeed) const;
    double calculateTSROptimal() const;
    double getCpOpt() const { return params_.cpOpt; }
    double getTsrOpt() const { return params_.tsrOpt; }

    // ------------------------------------------------------------------------
    // Potencia mecanica del viento
    // ------------------------------------------------------------------------
    double calculateWindPower(double windSpeed) const;
    double calculateMechanicalPower(double windSpeed, double cp) const;
    double calculateMechanicalPower(double windSpeed, double lambda,
                                     double betaDeg) const;

    // ------------------------------------------------------------------------
    // Control de pitch y velocidad
    // ------------------------------------------------------------------------
    double pitchController(double P_error, double dt);
    double speedController(double omega_error, double dt);
    double optimalTorqueControl(double windSpeed);

    // ------------------------------------------------------------------------
    // Modelos electricos por tipo
    // ------------------------------------------------------------------------
    ElectricalOutput calculateElectricalOutput();

    // Type 1: Jaula de ardilla + capacitor
    ElectricalOutput calculateType1Output(double Pmech);

    // Type 2: Rotor bobinado + resistencia externa
    ElectricalOutput calculateType2Output(double Pmech);

    // Type 3: DFIG con back-to-back converter
    ElectricalOutput calculateType3Output(double Pmech);

    // Type 4: Full converter
    ElectricalOutput calculateType4Output(double Pmech);

    // ------------------------------------------------------------------------
    // Modelo del generador de induccion (Type 1-3)
    // ------------------------------------------------------------------------
    double calculateInductionGeneratorSlip(double Pmech) const;
    double calculateInductionGeneratorTorque(double slip) const;
    double calculateInductionGeneratorQ(double P, double slip) const;
    double calculateStatorCurrent(double P, double Q, double V) const;

    // ------------------------------------------------------------------------
    // LVRT (Low Voltage Ride Through)
    // ------------------------------------------------------------------------
    bool checkLVRT(double Vpu, double duration) const;
    bool isLVRTActive(double Vpu) const;
    double calculateLVRTReactiveSupport(double Vpu) const;
    double calculateLVRTActivePowerReduction(double Vpu) const;

    // ------------------------------------------------------------------------
    // Curva de potencia completa
    // ------------------------------------------------------------------------
    std::vector<PowerCurvePoint> getPowerCurve(
        double windSpeedMin = 0.0,
        double windSpeedMax = 30.0,
        double windSpeedStep = 0.5);

    std::vector<PowerCurvePoint> getPowerCurveOptimal(
        double windSpeedMin = 0.0,
        double windSpeedMax = 30.0,
        double windSpeedStep = 0.5);

    // ------------------------------------------------------------------------
    // Regiones de operacion
    // ------------------------------------------------------------------------
    int getOperatingRegion(double windSpeed) const;
    double getRatedPower() const { return params_.ratedPower; }
    double getCurrentPower() const { return lastOutput_.P; }
    double getCurrentReactivePower() const { return lastOutput_.Q; }

    // ------------------------------------------------------------------------
    // Area del rotor
    // ------------------------------------------------------------------------
    double getRotorArea() const {
        return WT_PI * params_.rotorDiameter * params_.rotorDiameter / 4.0;
    }

    // ------------------------------------------------------------------------
    // Simulacion paso a paso
    // ------------------------------------------------------------------------
    void step(double dt, double windSpeed, double Vterminal = 1.0);

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    TurbineType getType() const { return type_; }
    double getWindSpeed() const { return windSpeed_; }
    double getSOC() const { return 0.0; } // placeholder for interface
    double getPitchAngle() const { return pitchAngle_; }
    double getRotorSpeed() const { return omegaRotor_; }
    double getSlip() const { return lastOutput_.slip; }
    const WindTurbineParameters& getParameters() const { return params_; }
    const ElectricalOutput& getLastOutput() const { return lastOutput_; }

    // ------------------------------------------------------------------------
    // Estado
    // ------------------------------------------------------------------------
    int getStatus() const { return status_; }
    void setStatus(int s) { status_ = s; }

private:
    TurbineType type_;
    WindTurbineParameters params_;
    OperatingMode opMode_;

    // Estados
    double windSpeed_ = 10.0;         // m/s
    double pitchAngle_ = 0.0;         // grados
    double omegaRotor_ = 1.0;         // pu velocidad rotor
    double omegaGen_ = 1.0;           // pu velocidad generador
    double integralPitch_ = 0.0;      // termino integral pitch
    double integralSpeed_ = 0.0;      // termino integral speed
    double externalResistance_ = 0.0; // pu (Type 2)
    double Vterminal_ = 1.0;          // pu
    double crowbarActive_ = false;    // DFIG crowbar

    int status_ = 1;                  // 1=online, 0=offline
    ElectricalOutput lastOutput_;

    // Metodos internos
    double calculateAerodynamicTorque(double windSpeed, double omega, double beta) const;
    double solveSteadyStateOmega(double windSpeed, double beta) const;
};

} // namespace powsys365
