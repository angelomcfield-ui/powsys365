// ============================================================================
// induction_generator.h
// Generador de induccion - Equivalent circuit model
// Torque-slip curve, reactive power consumption, starting current analysis
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <utility>

namespace powsys365 {

// ============================================================================
// Parametros del circuito equivalente
// ============================================================================
struct InductionGeneratorParameters {
    double R1 = 0.01;   // pu - Resistencia estator
    double X1 = 0.15;   // pu - Reactancia estator
    double R2 = 0.018;  // pu - Resistencia rotor
    double X2 = 0.18;   // pu - Reactancia rotor
    double Xm = 4.0;    // pu - Reactancia magnetizante
    double baseMVA = 100.0; // MVA base
    double baseVoltage = 0.69; // kV
    double baseFrequency = 50.0; // Hz
    int    numPoles = 4;      // Numero de polos
    double ratedSpeed = 1500.0; // rpm
    double ratedPower = 2.0;  // MW
    double inertia = 3.0;     // s - constante H
    double frictionCoeff = 0.01; // Coeficiente de friccion
    double ironLossFactor = 0.01; // Perdidas hierro pu
    double strayLossFactor = 0.005; // Perdidas dispersas pu
};

// ============================================================================
// Resultados del generador
// ============================================================================
struct InductionGeneratorResult {
    double slip;            // pu
    double torque;          // pu
    double P;               // MW potencia activa
    double Q;               // MVAr potencia reactiva (consumo positivo)
    double I;               // pu corriente
    double efficiency;      // %
    double powerFactor;     // cos(phi)
    double statorCurrent;   // pu
    double rotorCurrent;    // pu
    double magnetizingCurrent; // pu
    double speed;           // rpm
    double angularSpeed;    // rad/s
    double Te;              // Nm torque electromagnetico
    double Tmech;           // Nm torque mecanico
    double rotorLosses;      // MW perdidas rotor
    double statorLosses;     // MW perdidas estator
    double electricalLosses; // MW perdidas electricas totales
    double mechanicalLosses; // MW perdidas mecanicas
    double outputPower;      // MW
    int    status;           // Estado
};

// ============================================================================
// Punto de la curva torque-slip
// ============================================================================
struct TorqueSlipPoint {
    double slip;
    double torque;
    double current;
    double powerFactor;
    double speed;
};

// ============================================================================
// Clase: InductionGeneratorModel
// ============================================================================
class InductionGeneratorModel {
public:
    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    InductionGeneratorModel();
    explicit InductionGeneratorModel(
        const InductionGeneratorParameters& params);

    // ------------------------------------------------------------------------
    // Setters
    // ------------------------------------------------------------------------
    void setParameters(const InductionGeneratorParameters& params);
    void setSlip(double slip);
    void setSpeed(double speedRpm); // rpm
    void setMechanicalPower(double Pmech); // MW
    void setTerminalVoltage(double Vpu);
    void setFrequency(double fHz);
    void setStatus(int status);

    // ------------------------------------------------------------------------
    // Circuito equivalente
    // ------------------------------------------------------------------------
    double calculateImpedance(double slip) const;          // Z_total pu
    double calculateStatorCurrent(double slip) const;       // I1 pu
    double calculateRotorCurrent(double slip) const;        // I2 pu
    double calculateMagnetizingCurrent(double slip) const;  // Im pu
    double calculateRotorImpedance(double slip) const;      // Z2 pu
    double calculateTheveninVoltage() const;                // Vth pu
    double calculateTheveninImpedance() const;              // Zth pu

    // ------------------------------------------------------------------------
    // Torque-slip
    // ------------------------------------------------------------------------
    double calculateTorque(double slip) const;              // pu
    double calculateTorqueNm(double slip) const;            // Nm
    double calculateMaxTorque() const;                      // pu
    double calculateSlipAtMaxTorque() const;                // pu
    double calculateStartingTorque() const;                 // pu (s=1)
    double calculatePullUpTorque() const;                   // pu

    // ------------------------------------------------------------------------
    // Potencia
    // ------------------------------------------------------------------------
    double calculateActivePower(double slip) const;         // MW
    double calculateReactivePower(double slip) const;       // MVAr
    double calculateAirGapPower(double slip) const;         // MW
    double calculateRotorLosses(double slip) const;         // MW
    double calculateStatorLosses(double slip) const;        // MW
    double calculateEfficiency(double slip) const;          // %
    double calculatePowerFactor(double slip) const;         // cos(phi)

    // ------------------------------------------------------------------------
    // Corriente de arranque (starting current)
    // ------------------------------------------------------------------------
    double calculateStartingCurrent() const;         // pu (s=1)
    double calculateStartingCurrentK() const;        // I_start/I_rated
    double calculateLockedRotorCurrent() const;      // pu
    double calculateInrushCurrent() const;           // pu (con saturacion)

    // ------------------------------------------------------------------------
    // Curvas
    // ------------------------------------------------------------------------
    std::vector<TorqueSlipPoint> getTorqueSlipCurve(int numPoints = 200);
    std::vector<std::pair<double, double>> getCurrentSlipCurve(
        int numPoints = 200);
    std::vector<std::pair<double, double>> getPowerFactorSlipCurve(
        int numPoints = 200);

    // ------------------------------------------------------------------------
    // Resolver slip dado torque o potencia mecanica
    // ------------------------------------------------------------------------
    double solveSlipFromTorque(double torque) const;   // Metodo iterativo
    double solveSlipFromPower(double Pmech) const;     // Metodo iterativo

    // ------------------------------------------------------------------------
    // Calculo completo
    // ------------------------------------------------------------------------
    InductionGeneratorResult calculate();
    InductionGeneratorResult calculate(double slip);
    InductionGeneratorResult calculateFromSpeed(double speedRpm);
    InductionGeneratorResult calculateFromPower(double Pmech);

    // ------------------------------------------------------------------------
    // Velocidad sincrona
    // ------------------------------------------------------------------------
    double getSynchronousSpeed() const;       // rpm
    double getSynchronousSpeedRad() const;    // rad/s
    double getRatedSpeed() const { return params_.ratedSpeed; }
    double getActualSpeed(double slip) const;  // rpm

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    double getSlip() const { return slip_; }
    double getTorque() const { return lastResult_.torque; }
    double getP() const { return lastResult_.P; }
    double getQ() const { return lastResult_.Q; }
    double getCurrent() const { return lastResult_.I; }
    double getEfficiency() const { return lastResult_.efficiency; }
    double getPowerFactor() const { return lastResult_.powerFactor; }
    const InductionGeneratorParameters& getParameters() const {
        return params_;
    }
    const InductionGeneratorResult& getLastResult() const {
        return lastResult_;
    }
    int getStatus() const { return status_; }

private:
    InductionGeneratorParameters params_;
    double slip_;
    double terminalVoltage_;
    double frequency_;
    int    status_;
    InductionGeneratorResult lastResult_;

    // Metodos internos
    double calculateMechanicalLosses(double speedRpm) const;
    double calculateIronLosses(double slip) const;
};

} // namespace powsys365
