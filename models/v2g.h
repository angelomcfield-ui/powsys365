// ============================================================================
// v2g.h
// Modelo Vehicle-to-Grid (V2G)
// EV battery + aggregator fleet + grid services
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <random>

namespace powsys365 {

// ============================================================================
// Tipos de EV
// ============================================================================
enum EVType {
    EV_BEV = 0,        // Battery Electric Vehicle
    EV_PHEV = 1,       // Plug-in Hybrid
    EV_FCEV = 2        // Fuel Cell (no V2G)
};

// ============================================================================
// Modo de carga V2G
// ============================================================================
enum V2GMode {
    V2G_UNIDIRECTIONAL = 0,   // Solo carga (G2V)
    V2G_BIDIRECTIONAL = 1,    // Carga y descarga (V2G)
    V2G_SMART_CHARGE = 2,     // Carga inteligente
    V2G_GRID_SUPPORT = 3      // Soporte activo a red
};

// ============================================================================
// Estado de conexion del vehiculo
// ============================================================================
enum EVConnectionStatus {
    EV_DISCONNECTED = 0,
    EV_CONNECTED_HOME = 1,
    EV_CONNECTED_WORK = 2,
    EV_CONNECTED_PUBLIC = 3,
    EV_DRIVING = 4
};

// ============================================================================
// Parametros de bateria del EV
// ============================================================================
struct EVBatteryParameters {
    double capacity = 60.0;          // kWh
    double socMin = 0.1;             // 10%
    double socMax = 0.95;            // 95%
    double socInitial = 0.5;         // 50%
    double chargePowerMax = 11.0;    // kW AC (Level 2)
    double dischargePowerMax = 11.0; // kW (V2G)
    double chargeEfficiency = 0.95;  // 95%
    double dischargeEfficiency = 0.95; // 95%
    double selfDischargeRate = 0.001; // %/hora
    double voltageNominal = 400.0;   // V
    double temperatureOptimal = 25.0; // C
    double cyclesLife = 3000.0;      // ciclos de vida
    double degradationRate = 0.0001;  // %/ciclo
    EVType type = EV_BEV;
};

// ============================================================================
// Perfil de llegada/salida
// ============================================================================
struct EVArrivalPattern {
    double arrivalTime;    // Hora del dia (0-24)
    double departureTime;  // Hora del dia (0-24)
    double arrivalSOC;     // SOC a la llegada (0-1)
    double desiredSOC;     // SOC deseado a la salida (0-1)
    double parkingDuration; // horas
    double location;       // 0=home, 1=work, 2=public
};

// ============================================================================
// Servicios de red
// ============================================================================
struct V2GGridService {
    bool frequencyRegulation; // Regulacion primaria/secundaria
    bool peakShaving;         // Recorte de picos
    bool voltageSupport;      // Soporte de voltaje (VAr)
    bool spinningReserve;     // Reserva giratoria
    bool energyArbitrage;     // Arbitraje de energia
    double servicePriority;   // 0-1 prioridad del servicio
};

// ============================================================================
// Modelo de un solo vehiculo EV
// ============================================================================
class EVModel {
public:
    EVModel();
    explicit EVModel(const EVBatteryParameters& params);

    void setBatteryParameters(const EVBatteryParameters& params);
    void setSOC(double soc);
    void setChargePower(double powerKW);   // kW positivo = carga
    void setDischargePower(double powerKW); // kW positivo = descarga
    void setConnectionStatus(EVConnectionStatus status);
    void setArrivalPattern(const EVArrivalPattern& pattern);

    // Carga/Descarga
    double charge(double powerKW, double dt);    // Retorna energia cargada kWh
    double discharge(double powerKW, double dt); // Retorna energia descargada kWh

    // Estado
    double getSOC() const { return soc_; }
    double getSOCPercent() const { return soc_ * 100.0; }
    double getEnergyStored() const; // kWh
    double getAvailableEnergy() const; // kWh disponible para descarga
    double getRangeKm() const; // km de autonomia restante
    EVConnectionStatus getConnectionStatus() const { return connStatus_; }

    // Limites
    double getMaxChargePower() const;   // kW
    double getMaxDischargePower() const; // kW
    double getChargeTimeToFull() const;  // horas
    double getDegradationFactor() const;

private:
    EVBatteryParameters params_;
    double soc_;
    double stateOfHealth_;
    EVConnectionStatus connStatus_;
    EVArrivalPattern arrival_;
    double chargeCycles_;
};

// ============================================================================
// Modelo de perfil de carga
// ============================================================================
struct ChargingProfile {
    double timeHour;     // Hora del dia
    double chargePower;  // kW (positivo = carga, negativo = V2G)
    double soc;          // SOC del vehiculo
    double gridPower;    // kW hacia/desde red
    double cost;         // $/kWh
    double revenue;      // $ de ingreso por servicios
};

// ============================================================================
// Modelo Agregador (Fleet)
// ============================================================================
class EVAggregatorModel {
public:
    EVAggregatorModel();
    explicit EVAggregatorModel(int numVehicles);

    void addVehicle(const EVModel& ev);
    void removeVehicle(int index);
    void setFleetSize(int n);

    // Perfiles
    void generateArrivalPatterns(double meanArrival, double stdArrival,
                                  double meanDeparture, double stdDeparture);
    std::vector<ChargingProfile> getFleetChargingProfile(double timeResolution = 0.25); // 15 min

    // Agregacion
    double getFleetSOC() const;
    double getFleetChargePower() const;    // kW total de carga
    double getFleetDischargePower() const; // kW total de descarga V2G
    double getFleetAvailableCapacity() const; // kWh disponible
    int    getNumConnectedVehicles() const;
    int    getNumVehicles() const { return (int)vehicles_.size(); }

private:
    std::vector<EVModel> vehicles_;
    std::vector<EVArrivalPattern> patterns_;
};

// ============================================================================
// Modelo principal V2G
// ============================================================================
class V2GModel {
public:
    V2GModel();
    explicit V2GModel(int numEVs, double fleetPowerRating);

    // Configuracion
    void setFleetSize(int n);
    void setPowerRating(double powerMW);   // MW total del parque
    void setEnergyCapacity(double energyMWh); // MWh total
    void setV2GMode(V2GMode mode);
    void setGridService(const V2GGridService& service);
    void setFrequency(double fHz);
    void setVoltage(double Vpu);
    void setElectricityPrice(double pricePerKWh);

    // Bateria agregada
    void setFleetSOC(double soc);
    double getFleetSOC() const { return fleetSOC_; }

    // Carga/Descarga
    double chargeFleet(double powerMW);    // MW positivo = carga
    double dischargeFleet(double powerMW); // MW positivo = descarga

    // Servicios de red
    double frequencyRegulation(double deltaF, double df_dt); // MW
    double peakShaving(double loadMW, double thresholdMW);   // MW
    double voltageSupport(double Vpu);                       // MVAr
    double spinningReserve(double reserveMW);                // MW
    double energyArbitrage(double priceCurrent, double priceForecast); // MW

    // Calculo de potencia disponible
    double calculateAvailableChargePower() const;
    double calculateAvailableDischargePower() const;
    double calculateAvailableEnergy() const; // MWh

    // Llegada/Salida
    void updateArrivalDeparture(double currentHour);
    double getFleetAvailabilityFactor() const; // 0-1 fraccion conectada

    // Perfil temporal
    std::vector<ChargingProfile> getDailyProfile(double resolutionHours = 0.25);

    // Getters
    double getPout() const { return Pout_; }
    double getQout() const { return Qout_; }
    double getRevenue() const { return revenue_; }
    double getOperatingCost() const { return operatingCost_; }
    V2GMode getMode() const { return v2gMode_; }
    int    getStatus() const { return status_; }
    void setStatus(int s) { status_ = s; }

private:
    // Parametros del parque
    int    fleetSize_;
    double powerRating_;    // MW
    double energyCapacity_; // MWh
    double fleetSOC_;       // 0-1
    double fleetSOH_;       // 0-1
    V2GMode v2gMode_;

    // Estado
    double Pout_;          // MW (positivo = descarga/V2G, negativo = carga)
    double Qout_;          // MVAr
    double revenue_;       // $/hora
    double operatingCost_; // $/hora
    int    status_;

    // Red
    double frequency_;
    double voltage_;
    double electricityPrice_;

    // Servicios
    V2GGridService gridService_;

    // Disponibilidad
    double availabilityFactor_;
    double availabilityHome_;
    double availabilityWork_;
    double availabilityPublic_;

    // Modelo agregador
    EVAggregatorModel aggregator_;

    // Metodos internos
    double calculateDroopResponse(double deltaF) const;
    double calculateVirtualInertia(double df_dt) const;
};

} // namespace powsys365
