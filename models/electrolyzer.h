// ============================================================================
// electrolyzer.h
// Modelo de electrolizador para produccion de hidrogeno
// Tipos: Alcalino, PEM (Proton Exchange Membrane), SOEC (Solid Oxide)
// ============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// Tipos de electrolizador
// ============================================================================
enum ElectrolyzerType {
    ELEC_ALKALINE = 0,  // Alcalino (KOH)
    ELEC_PEM = 1,       // Proton Exchange Membrane
    ELEC_SOEC = 2       // Solid Oxide Electrolysis Cell
};

// ============================================================================
// Modo de operacion
// ============================================================================
enum ElectrolyzerMode {
    ELEC_MODE_CONSTANT_POWER = 0,  // Potencia constante
    ELEC_MODE_CONSTANT_CURRENT = 1, // Corriente constante
    ELEC_MODE_VARIABLE = 2,        // Variable segun disponibilidad
    ELEC_MODE_STANDBY = 3          // Standby
};

// ============================================================================
// Resultado de produccion
// ============================================================================
struct H2ProductionResult {
    double h2FlowRate;      // Nm3/h
    double h2MassFlow;      // kg/h
    double powerConsumption; // MW
    double efficiencyLHV;   // % (Lower Heating Value)
    double efficiencyHHV;   // % (Higher Heating Value)
    double cellVoltage;     // V
    double cellCurrent;     // A
    double currentDensity;  // A/cm2
    double electrolyteTemp; // C
    double ohmicOverpotential;   // V
    double activationOverpotential; // V
    double concentrationOverpotential; // V
    double faradayEfficiency;    // %
    double waterConsumption;     // L/h
    double o2Production;         // Nm3/h
    double heatGenerated;        // kW
    double status;               // 1=operativo, 0=parada
};

// ============================================================================
// Parametros del electrolizador
// ============================================================================
struct ElectrolyzerParameters {
    // Generales
    double ratedPower = 10.0;        // MW
    double ratedH2Production = 2000.0; // Nm3/h
    int    numCells = 100;            // Numero de celdas en serie
    double cellArea = 1000.0;         // cm2 por celda
    double minLoad = 0.15;            // 15% carga minima
    double maxLoad = 1.0;             // 100% carga maxima
    double rampUpRate = 0.5;          // %/s
    double rampDownRate = 0.5;        // %/s

    // Voltaje de celda (parametros de la curva U-I)
    double vRev = 1.23;              // V - voltaje reversible
    double vNominal = 1.8;           // V - voltaje nominal
    double vMax = 2.2;               // V - voltaje maximo
    double currentNominal = 10000.0; // A - corriente nominal
    double currentMax = 15000.0;     // A - corriente maxima
    double tempNominal = 80.0;       // C - temperatura nominal
    double tempMin = 40.0;           // C - temperatura minima
    double tempMax = 90.0;           // C - temperatura maxima
    double pressureNominal = 30.0;   // bar

    // Parametros especificos por tipo
    // Alcalino
    double concentrationKOH = 30.0;  // % peso KOH
    double diaphragmThickness = 2.0; // mm
    double gasCrossover = 0.01;      // % de crossover

    // PEM
    double membraneThickness = 0.2;  // mm
    double catalystLoadingAnode = 0.3; // mg Pt/cm2
    double catalystLoadingCathode = 0.4; // mg Pt/cm2
    double protonConductivity = 0.1; // S/cm

    // SOEC
    double electrolyteThickness = 0.02; // mm
    double operatingTempSOEC = 800.0;   // C
    double ionicConductivity = 0.05;    // S/cm

    // Parametros de Faraday
    double faradayEfficiencyNominal = 0.98; // 98%
    double faradayCurrentFactor = 0.01;     // factor de degradacion

    // Parametros termicos
    double heatCapacity = 1000.0;    // kJ/K
    double heatLossCoefficient = 10.0; // kW/K
    double coolingPower = 500.0;     // kW
    double heatingPower = 200.0;     // kW (SOEC)

    // Eficiencias
    double efficiencyLHV = 0.65;     // 65% LHV tipico
    double efficiencyHHV = 0.55;     // 55% HHV tipico
    double auxPowerFraction = 0.05;  // 5% potencia auxiliar
};

// ============================================================================
// Modelo de electrolizador
// ============================================================================
class ElectrolyzerModel {
public:
    ElectrolyzerModel();
    explicit ElectrolyzerModel(ElectrolyzerType type,
                                const ElectrolyzerParameters& params);

    // Setters
    void setType(ElectrolyzerType type);
    void setParameters(const ElectrolyzerParameters& params);
    void setPowerInput(double powerMW);       // MW
    void setCurrent(double currentA);         // A
    void setOperatingMode(ElectrolyzerMode mode);
    void setTemperature(double tempC);        // C
    void setPressure(double pressureBar);     // bar
    void setStatus(int status);
    void setLoadFactor(double load);          // 0-1

    // Modelo Faraday: eficiencia vs densidad de corriente
    double calculateFaradayEfficiency(double currentDensity) const;

    // Curva I-U (caracteristica voltametrica)
    double calculateCellVoltage(double current) const;
    double calculateCellVoltage(double current, double tempC) const;
    double calculateCurrent(double cellVoltage) const;

    // Sobrepotenciales
    double calculateReversibleVoltage(double tempC) const;  // V
    double calculateOhmicOverpotential(double current) const;       // V
    double calculateActivationOverpotential(double current) const;  // V
    double calculateConcentrationOverpotential(double current) const; // V

    // Produccion de H2
    H2ProductionResult calculateH2Production();
    H2ProductionResult calculateH2Production(double powerMW);

    // Tasa de produccion
    double calculateH2FlowRate(double powerMW) const;    // Nm3/h
    double calculateH2MassFlow(double flowRateNm3h) const; // kg/h

    // Consumo de agua y produccion de O2
    double calculateWaterConsumption(double h2FlowNm3h) const; // L/h
    double calculateO2Production(double h2FlowNm3h) const;   // Nm3/h

    // Modelo termico
    double calculateHeatGenerated(double powerMW) const;       // kW
    double calculateThermalEfficiency() const;                 // %
    double calculateTemperatureRise(double powerMW, double dt) const; // C

    // Curva I-U completa
    std::vector<std::pair<double, double>> getIVCurve(int numPoints = 100);

    // Potencia vs produccion
    std::vector<std::pair<double, double>> getPowerProductionCurve(
        int numPoints = 50);

    // Modelo de eficiencia a carga parcial
    double calculateEfficiencyAtLoad(double load) const; // 0-1 load, retorna %

    // Degradacion
    double calculateDegradationFactor(double operatingHours) const;

    // Getters
    double getPowerInput() const { return powerInput_; }
    double getH2Production() const { return lastResult_.h2FlowRate; }
    double getEfficiencyLHV() const { return lastResult_.efficiencyLHV; }
    double getEfficiencyHHV() const { return lastResult_.efficiencyHHV; }
    double getCellVoltage() const { return lastResult_.cellVoltage; }
    double getTemperature() const { return temperature_; }
    double getLoadFactor() const { return loadFactor_; }
    const H2ProductionResult& getLastResult() const { return lastResult_; }
    ElectrolyzerType getType() const { return type_; }
    int getStatus() const { return status_; }

private:
    ElectrolyzerType type_;
    ElectrolyzerParameters params_;
    ElectrolyzerMode mode_;

    double powerInput_;   // MW
    double current_;      // A
    double temperature_;  // C
    double pressure_;     // bar
    double loadFactor_;   // 0-1
    int    status_;

    double operatingHours_;
    H2ProductionResult lastResult_;

    // Constantes fisicas
    static constexpr double F = 96485.33212;   // C/mol - Constante de Faraday
    static constexpr double R_gas = 8.314462618; // J/(mol*K)
    static constexpr double M_H2 = 2.016;       // g/mol masa molar H2
    static constexpr double LHV_H2 = 120.0;     // MJ/kg Lower Heating Value
    static constexpr double HHV_H2 = 142.0;     // MJ/kg Higher Heating Value
    static constexpr double V_molar_STP = 22.414; // L/mol a STP
};

} // namespace powsys365
