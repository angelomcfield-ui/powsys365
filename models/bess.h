// ============================================================================
// bess.h
// Modelo de Battery Energy Storage System (BESS)
// Soporte: Li-ion LFP/NMC, Vanadium Flow
// Modos: Peak shaving, frequency regulation, voltage support, black start
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// Quimica de bateria
// ============================================================================
enum BatteryChemistry {
    CHEM_LFP = 0,      // LiFePO4 - LFP
    CHEM_NMC = 1,      // LiNiMnCoO2 - NMC
    CHEM_NCA = 2,      // LiNiCoAlO2 - NCA
    CHEM_VANADIUM_FLOW = 3, // Vanadium Redox Flow Battery (VRFB)
    CHEM_LEAD_ACID = 4,     // Plomo-acido
    CHEM_SODIUM_ION = 5     // Sodio-ion
};

// ============================================================================
// Modo de operacion del BESS
// ============================================================================
enum BESSOperatingMode {
    BESS_MODE_STANDBY = 0,
    BESS_MODE_PEAK_SHAVING = 1,
    BESS_MODE_FREQUENCY_REGULATION = 2,
    BESS_MODE_VOLTAGE_SUPPORT = 3,
    BESS_MODE_BLACK_START = 4,
    BESS_MODE_ENERGY_ARBITRAGE = 5,
    BESS_MODE_SPINNING_RESERVE = 6,
    BESS_MODE_RAMP_RATE_CONTROL = 7
};

// ============================================================================
// Parametros de ciclo de vida (rainflow counting)
// ============================================================================
struct CycleData {
    double dod;        // Depth of Discharge (%)
    double socMin;     // SOC minimo del ciclo
    double socMax;     // SOC maximo del ciclo
    double cycles;     // Numero de ciclos equivalentes
    double throughput; // MWh de throughput
};

// ============================================================================
// Capacidad disponible
// ============================================================================
struct AvailablePower {
    double PchargeMax;    // MW maximo de carga (negativo)
    double PdischargeMax; // MW maximo de descarga (positivo)
    double Eavailable;    // MWh disponible
    double timeToEmpty;   // Horas hasta vacio
    double timeToFull;    // Horas hasta lleno
};

// ============================================================================
// Parametros del BESS
// ============================================================================
struct BESSParameters {
    // Capacidad
    double ratedPower = 10.0;       // MW
    double energyCapacity = 40.0;   // MWh
    double socInitial = 0.5;        // SOC inicial (0-1)
    double socMin = 0.1;            // SOC minimo operativo
    double socMax = 0.9;            // SOC maximo operativo

    // Eficiencia
    double roundTripEfficiency = 0.92; // Eficiencia round-trip
    double chargeEfficiency = 0.96;    // Eficiencia de carga
    double dischargeEfficiency = 0.96; // Eficiencia de descarga
    double selfDischargeRate = 0.001;  // %/hora (0.1%/h tipico Li-ion)
    double standbyLoss = 0.005;        // %/hora de perdidas en standby

    // Limites de C-rate
    double chargeCrateMax = 1.0;     // C maximo de carga
    double dischargeCrateMax = 1.0;  // C maximo de descarga
    double responseTime = 0.05;      // s
    double rampRateMax = 10.0;       // MW/s

    // Droop control
    double droopCoefficient = 0.05;  // pu (5% tipico)
    double virtualInertiaH = 3.0;    // s - constante de inercia virtual
    double frequencyDeadband = 0.05; // Hz (0.036 Hz = 0.06% tipico)

    // Degradacion
    double calendarDegradationRate = 0.015; // 1.5% anual
    double cycleDegradationFactor = 0.0002; // factor de degradacion ciclica
    double temperatureDerating = 1.0;       // factor a 25C

    // Black start
    bool blackStartCapable = false;
    double blackStartPower = 2.0;    // MW para black start
    double blackStartDuration = 60.0; // s

    // Parametros quimicos
    BatteryChemistry chemistry = CHEM_LFP;
    double nominalCellVoltage = 3.2;   // V (LFP)
    double minCellVoltage = 2.5;       // V
    double maxCellVoltage = 3.65;      // V
    double operatingTempMin = -10.0;   // C
    double operatingTempMax = 50.0;    // C
    int cellsInSeries = 1024;          // celdas en serie
    int stringsInParallel = 10;        // strings paralelo
    double dcVoltageNominal = 3276.8;  // V
    double dcVoltageMax = 3737.6;      // V
    double dcVoltageMin = 2560.0;      // V
};

// ============================================================================
// Clase principal: BESSModel
// ============================================================================
class BESSModel {
public:
    // ------------------------------------------------------------------------
    // Constructores
    // ------------------------------------------------------------------------
    BESSModel();
    explicit BESSModel(const BESSParameters& params);

    // ------------------------------------------------------------------------
    // Setters de configuracion
    // ------------------------------------------------------------------------
    void setParameters(const BESSParameters& params);
    void setSOC(double soc);          // 0-1
    void setSOCPercent(double socPct); // 0-100%
    void setOperatingMode(BESSOperatingMode mode);
    void setPowerReference(double P); // MW (positivo=descarga, negativo=carga)
    void setReactiveReference(double Q); // MVAr
    void setDroopCoefficient(double droop);
    void setVirtualInertiaH(double H);
    void setTerminalVoltage(double Vpu);
    void setFrequency(double fHz);
    void setTemperature(double tempC);

    // ------------------------------------------------------------------------
    // SOC
    // ------------------------------------------------------------------------
    double getSOC() const { return soc_; }
    double getSOCPercent() const { return soc_ * 100.0; }
    double getEnergyStored() const;    // MWh actual
    double getEnergyCapacity() const { return params_.energyCapacity; }

    // ------------------------------------------------------------------------
    // Carga/Descarga
    // ------------------------------------------------------------------------
    double charge(double powerMW);     // Carga (power > 0 es potencia de carga)
    double discharge(double powerMW);  // Descarga (power > 0 es potencia solicitada)

    // ------------------------------------------------------------------------
    // Potencia disponible
    // ------------------------------------------------------------------------
    AvailablePower calculateAvailablePower() const;
    double getMaxChargePower() const;    // MW (negativo)
    double getMaxDischargePower() const; // MW (positivo)

    // ------------------------------------------------------------------------
    // Degradacion ciclica (rainflow counting simplificado)
    // ------------------------------------------------------------------------
    void recordCycle(double socMin, double socMax);
    double calculateCycleDegradation() const;
    double calculateCalendarDegradation(double years) const;
    double calculateTotalDegradation(double years) const;
    double getRemainingCapacity() const;
    double getStateOfHealth() const { return stateOfHealth_; }
    const std::vector<CycleData>& getCycleHistory() const { return cycleHistory_; }

    // ------------------------------------------------------------------------
    // Respuesta de frecuencia (droop + virtual inertia)
    // ------------------------------------------------------------------------
    double calculateDroopResponse(double deltaF) const; // deltaF en pu
    double calculateVirtualInertiaResponse(double df_dt) const; // df/dt en pu/s
    double calculateFrequencyRegulationPower(double fHz, double df_dt) const;

    // ------------------------------------------------------------------------
    // FFR - Fast Frequency Response
    // ------------------------------------------------------------------------
    double calculateFFR(double df_dt, double delta_f) const;

    // ------------------------------------------------------------------------
    // Peak shaving
    // ------------------------------------------------------------------------
    double calculatePeakShavingPower(double loadMW, double thresholdMW) const;

    // ------------------------------------------------------------------------
    // Voltage support (Volt/VAr)
    // ------------------------------------------------------------------------
    double calculateVoltageSupportQ(double Vpu) const;

    // ------------------------------------------------------------------------
    // Black start
    // ------------------------------------------------------------------------
    bool canBlackStart() const;
    double getBlackStartPower() const;

    // ------------------------------------------------------------------------
    // Simulacion paso a paso
    // ------------------------------------------------------------------------
    void step(double dt, double fHz = 60.0, double Vpu = 1.0);

    // ------------------------------------------------------------------------
    // Getters de salida
    // ------------------------------------------------------------------------
    double getPout() const { return Pout_; }
    double getQout() const { return Qout_; }
    double getPdc() const { return Pdc_; }
    double getVdc() const { return Vdc_; }
    double getEfficiency() const;
    double getRoundTripEfficiency() const { return params_.roundTripEfficiency; }
    BESSOperatingMode getOperatingMode() const { return mode_; }
    BatteryChemistry getChemistry() const { return params_.chemistry; }
    const BESSParameters& getParameters() const { return params_; }
    int getStatus() const { return status_; }
    void setStatus(int s) { status_ = s; }

private:
    BESSParameters params_;
    BESSOperatingMode mode_;

    double soc_;              // 0-1
    double stateOfHealth_;    // 0-1 (capacidad remanente)
    double Pout_;             // MW salida AC (positivo=descarga)
    double Qout_;             // MVAr salida AC
    double Pdc_;              // MW salida DC
    double Vdc_;              // V DC
    double Pref_;             // Setpoint de potencia
    double Qref_;             // Setpoint de reactiva
    double Vterminal_;        // pu
    double frequency_;        // Hz
    double temperature_;      // C
    int    status_;           // 1=online, 0=offline

    std::vector<CycleData> cycleHistory_;
    double socPrevious_;      // SOC previo para deteccion de ciclos
    bool cycleRecording_;     // flag de grabacion

    // Limites de SOC para operacion
    void updateSOCLimits();
    double socMinCurrent_;    // SOC min actual (considerando degradacion)
    double socMaxCurrent_;    // SOC max actual

    // Perdidas de conversion DC-AC
    double calculateConverterLosses(double P) const;
    double calculateSelfDischarge(double dt) const;
};

} // namespace powsys365
