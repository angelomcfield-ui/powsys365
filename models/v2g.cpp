// ============================================================================
// v2g.cpp
// Implementacion del modelo Vehicle-to-Grid (V2G)
// ============================================================================

#include "v2g.h"
#include <numeric>
#include <algorithm>

namespace powsys365 {

// ============================================================================
// EVModel - Vehiculo individual
// ============================================================================

EVModel::EVModel()
    : params_(), soc_(0.5), stateOfHealth_(1.0),
      connStatus_(EV_DISCONNECTED), chargeCycles_(0.0) {}

EVModel::EVModel(const EVBatteryParameters& params)
    : params_(params), soc_(params.socInitial), stateOfHealth_(1.0),
      connStatus_(EV_DISCONNECTED), chargeCycles_(0.0) {}

void EVModel::setBatteryParameters(const EVBatteryParameters& params) {
    params_ = params;
}

void EVModel::setSOC(double soc) {
    soc_ = std::max(params_.socMin, std::min(params_.socMax, soc));
}

void EVModel::setChargePower(double powerKW) {
    (void)powerKW; // Se usa en charge()
}

void EVModel::setDischargePower(double powerKW) {
    (void)powerKW; // Se usa en discharge()
}

void EVModel::setConnectionStatus(EVConnectionStatus status) {
    connStatus_ = status;
}

void EVModel::setArrivalPattern(const EVArrivalPattern& pattern) {
    arrival_ = pattern;
}

double EVModel::getEnergyStored() const {
    return params_.capacity * soc_ * stateOfHealth_;
}

double EVModel::getAvailableEnergy() const {
    return params_.capacity * (soc_ - params_.socMin) * stateOfHealth_;
}

double EVModel::getRangeKm() const {
    double efficiency = 0.15; // kWh/km tipico
    return getEnergyStored() / efficiency;
}

double EVModel::getMaxChargePower() const {
    if (connStatus_ == EV_DISCONNECTED || connStatus_ == EV_DRIVING)
        return 0.0;
    if (soc_ >= params_.socMax) return 0.0;
    return params_.chargePowerMax;
}

double EVModel::getMaxDischargePower() const {
    if (connStatus_ == EV_DISCONNECTED || connStatus_ == EV_DRIVING)
        return 0.0;
    if (params_.type == EV_PHEV) return 0.0; // PHEV no V2G tipicamente
    if (soc_ <= params_.socMin) return 0.0;
    return params_.dischargePowerMax;
}

double EVModel::getChargeTimeToFull() const {
    double energyNeeded = params_.capacity * (params_.socMax - soc_);
    double P = params_.chargePowerMax * params_.chargeEfficiency;
    if (P < 1e-6) return std::numeric_limits<double>::infinity();
    return energyNeeded / P;
}

double EVModel::getDegradationFactor() const {
    return std::max(0.5, 1.0 - chargeCycles_ * params_.degradationRate);
}

double EVModel::charge(double powerKW, double dt) {
    if (connStatus_ == EV_DISCONNECTED || connStatus_ == EV_DRIVING)
        return 0.0;
    double P = std::min(powerKW, getMaxChargePower());
    if (P < 1e-6) return 0.0;

    double energyIn = P * params_.chargeEfficiency * dt; // kWh
    double deltaSOC = energyIn
                      / (params_.capacity * stateOfHealth_);
    double oldSOC = soc_;
    soc_ = std::min(params_.socMax, soc_ + deltaSOC);
    double actualEnergy = params_.capacity * stateOfHealth_ * (soc_ - oldSOC);

    // Conteo de ciclos
    chargeCycles_ += (soc_ - oldSOC) / 2.0; // Mitad de ciclo por carga
    return actualEnergy;
}

double EVModel::discharge(double powerKW, double dt) {
    if (connStatus_ == EV_DISCONNECTED || connStatus_ == EV_DRIVING)
        return 0.0;
    if (params_.type != EV_BEV) return 0.0; // Solo BEV para V2G
    double P = std::min(powerKW, getMaxDischargePower());
    if (P < 1e-6) return 0.0;

    double energyOut = P / params_.dischargeEfficiency * dt; // kWh
    double deltaSOC = energyOut
                      / (params_.capacity * stateOfHealth_);
    double oldSOC = soc_;
    soc_ = std::max(params_.socMin, soc_ - deltaSOC);
    double actualEnergy = params_.capacity * stateOfHealth_ * (oldSOC - soc_);

    chargeCycles_ += (oldSOC - soc_) / 2.0;
    return actualEnergy;
}

// ============================================================================
// EVAggregatorModel - Agregador de flota
// ============================================================================

EVAggregatorModel::EVAggregatorModel() {}

EVAggregatorModel::EVAggregatorModel(int numVehicles) {
    vehicles_.resize(numVehicles);
    patterns_.resize(numVehicles);
}

void EVAggregatorModel::addVehicle(const EVModel& ev) {
    vehicles_.push_back(ev);
    patterns_.push_back(EVArrivalPattern());
}

void EVAggregatorModel::removeVehicle(int index) {
    if (index >= 0 && index < (int)vehicles_.size()) {
        vehicles_.erase(vehicles_.begin() + index);
        if (index < (int)patterns_.size())
            patterns_.erase(patterns_.begin() + index);
    }
}

void EVAggregatorModel::setFleetSize(int n) {
    vehicles_.resize(n);
    patterns_.resize(n);
}

void EVAggregatorModel::generateArrivalPatterns(double meanArrival,
                                                 double stdArrival,
                                                 double meanDeparture,
                                                 double stdDeparture) {
    std::default_random_engine gen(42);
    std::normal_distribution<double> arrivalDist(meanArrival, stdArrival);
    std::normal_distribution<double> departureDist(meanDeparture, stdDeparture);

    for (size_t i = 0; i < vehicles_.size(); ++i) {
        EVArrivalPattern pat;
        pat.arrivalTime = std::max(0.0, std::min(24.0, arrivalDist(gen)));
        pat.departureTime = std::max(0.0, std::min(24.0, departureDist(gen)));
        if (pat.departureTime <= pat.arrivalTime)
            pat.departureTime = pat.arrivalTime + 8.0;
        pat.arrivalSOC = 0.2 + 0.5 * ((double)i / vehicles_.size());
        pat.desiredSOC = 0.9;
        pat.parkingDuration = pat.departureTime - pat.arrivalTime;
        pat.location = (pat.arrivalTime < 12.0) ? 0 : 1; // home or work
        patterns_[i] = pat;
    }
}

std::vector<ChargingProfile> EVAggregatorModel::getFleetChargingProfile(
    double timeResolution) {
    std::vector<ChargingProfile> profile;
    int steps = (int)(24.0 / timeResolution);
    profile.reserve(steps);

    for (int s = 0; s < steps; ++s) {
        double t = s * timeResolution;
        ChargingProfile cp;
        cp.timeHour = t;
        cp.chargePower = 0.0;
        cp.soc = 0.0;
        cp.gridPower = 0.0;
        cp.cost = 0.0;
        cp.revenue = 0.0;

        int count = 0;
        for (size_t i = 0; i < vehicles_.size(); ++i) {
            const auto& pat = patterns_[i];
            if (t >= pat.arrivalTime && t < pat.departureTime) {
                // Vehicle is connected
                double Pcharge = vehicles_[i].getMaxChargePower();
                cp.chargePower += Pcharge;
                cp.soc += vehicles_[i].getSOC();
                cp.gridPower += Pcharge;
                count++;
            }
        }
        if (count > 0) cp.soc /= count;
        profile.push_back(cp);
    }
    return profile;
}

double EVAggregatorModel::getFleetSOC() const {
    if (vehicles_.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& v : vehicles_) sum += v.getSOC();
    return sum / vehicles_.size();
}

double EVAggregatorModel::getFleetChargePower() const {
    double sum = 0.0;
    for (const auto& v : vehicles_) sum += v.getMaxChargePower();
    return sum / 1000.0; // MW
}

double EVAggregatorModel::getFleetDischargePower() const {
    double sum = 0.0;
    for (const auto& v : vehicles_) sum += v.getMaxDischargePower();
    return sum / 1000.0; // MW
}

double EVAggregatorModel::getFleetAvailableCapacity() const {
    double sum = 0.0;
    for (const auto& v : vehicles_) sum += v.getAvailableEnergy();
    return sum / 1000.0; // MWh
}

int EVAggregatorModel::getNumConnectedVehicles() const {
    int count = 0;
    for (const auto& v : vehicles_) {
        if (v.getConnectionStatus() != EV_DISCONNECTED
            && v.getConnectionStatus() != EV_DRIVING)
            count++;
    }
    return count;
}

// ============================================================================
// V2GModel - Modelo principal
// ============================================================================

V2GModel::V2GModel()
    : fleetSize_(100), powerRating_(1.0), energyCapacity_(10.0),
      fleetSOC_(0.5), fleetSOH_(1.0), v2gMode_(V2G_BIDIRECTIONAL),
      Pout_(0.0), Qout_(0.0), revenue_(0.0), operatingCost_(0.0),
      status_(1), frequency_(60.0), voltage_(1.0),
      electricityPrice_(0.15), availabilityFactor_(0.3),
      availabilityHome_(0.6), availabilityWork_(0.3),
      availabilityPublic_(0.1) {}

V2GModel::V2GModel(int numEVs, double fleetPowerRating)
    : fleetSize_(numEVs), powerRating_(fleetPowerRating),
      energyCapacity_(numEVs * 60.0 / 1000.0), fleetSOC_(0.5),
      fleetSOH_(1.0), v2gMode_(V2G_BIDIRECTIONAL), Pout_(0.0),
      Qout_(0.0), revenue_(0.0), operatingCost_(0.0), status_(1),
      frequency_(60.0), voltage_(1.0), electricityPrice_(0.15),
      availabilityFactor_(0.3), availabilityHome_(0.6),
      availabilityWork_(0.3), availabilityPublic_(0.1) {}

void V2GModel::setFleetSize(int n) {
    fleetSize_ = n;
    energyCapacity_ = n * 60.0 / 1000.0; // MWh
}

void V2GModel::setPowerRating(double powerMW) { powerRating_ = powerMW; }
void V2GModel::setEnergyCapacity(double energyMWh) { energyCapacity_ = energyMWh; }
void V2GModel::setV2GMode(V2GMode mode) { v2gMode_ = mode; }
void V2GModel::setGridService(const V2GGridService& service) {
    gridService_ = service;
}
void V2GModel::setFrequency(double fHz) { frequency_ = fHz; }
void V2GModel::setVoltage(double Vpu) { voltage_ = Vpu; }
void V2GModel::setElectricityPrice(double pricePerKWh) {
    electricityPrice_ = pricePerKWh;
}
void V2GModel::setFleetSOC(double soc) {
    fleetSOC_ = std::max(0.1, std::min(0.95, soc));
}

double V2GModel::chargeFleet(double powerMW) {
    if (status_ == 0 || fleetSOC_ >= 0.95) return 0.0;
    double P = std::min(powerMW, calculateAvailableChargePower());
    double energyIn = P * 0.95; // 95% eficiencia
    fleetSOC_ += energyIn / energyCapacity_;
    fleetSOC_ = std::min(0.95, fleetSOC_);
    Pout_ = -P; // Negativo = carga
    operatingCost_ += P * electricityPrice_ * 1000.0; // $/h
    return P;
}

double V2GModel::dischargeFleet(double powerMW) {
    if (status_ == 0 || v2gMode_ == V2G_UNIDIRECTIONAL) return 0.0;
    if (fleetSOC_ <= 0.1) return 0.0;
    double P = std::min(powerMW, calculateAvailableDischargePower());
    double energyOut = P / 0.95;
    fleetSOC_ -= energyOut / energyCapacity_;
    fleetSOC_ = std::max(0.1, fleetSOC_);
    Pout_ = P;
    revenue_ += P * electricityPrice_ * 1000.0 * 1.2; // 20% premium V2G
    return P;
}

double V2GModel::calculateDroopResponse(double deltaF) const {
    if (std::abs(deltaF) < 0.001) return 0.0;
    double droop = 0.05; // 5%
    return -deltaF / droop * powerRating_ * availabilityFactor_;
}

double V2GModel::calculateVirtualInertia(double df_dt) const {
    double H = 2.0;
    return -2.0 * H * df_dt * powerRating_ * availabilityFactor_;
}

double V2GModel::frequencyRegulation(double deltaF, double df_dt) {
    if (!gridService_.frequencyRegulation) return 0.0;
    double P_droop = calculateDroopResponse(deltaF);
    double P_inertia = calculateVirtualInertia(df_dt);
    double P = P_droop + P_inertia;
    if (P > 0) return dischargeFleet(P);
    return -chargeFleet(-P);
}

double V2GModel::peakShaving(double loadMW, double thresholdMW) {
    if (!gridService_.peakShaving || loadMW <= thresholdMW) return 0.0;
    double P = loadMW - thresholdMW;
    return dischargeFleet(std::min(P, powerRating_ * availabilityFactor_));
}

double V2GModel::voltageSupport(double Vpu) {
    if (!gridService_.voltageSupport) return 0.0;
    if (Vpu < 0.95) {
        Qout_ = 0.44 * powerRating_ * availabilityFactor_;
    } else if (Vpu > 1.05) {
        Qout_ = -0.44 * powerRating_ * availabilityFactor_;
    } else {
        Qout_ = 0.0;
    }
    return Qout_;
}

double V2GModel::spinningReserve(double reserveMW) {
    if (!gridService_.spinningReserve) return 0.0;
    double P = std::min(reserveMW,
                         powerRating_ * availabilityFactor_);
    return dischargeFleet(P);
}

double V2GModel::energyArbitrage(double priceCurrent,
                                  double priceForecast) {
    if (!gridService_.energyArbitrage) return 0.0;
    if (priceForecast > priceCurrent * 1.2) {
        // Precio subira: cargar ahora
        return -chargeFleet(powerRating_ * availabilityFactor_);
    } else if (priceForecast < priceCurrent * 0.8) {
        // Precio bajara: descargar ahora
        return dischargeFleet(powerRating_ * availabilityFactor_);
    }
    return 0.0;
}

double V2GModel::calculateAvailableChargePower() const {
    double Pmax = powerRating_ * availabilityFactor_;
    if (fleetSOC_ >= 0.95) return 0.0;
    double Eneeded = energyCapacity_ * (0.95 - fleetSOC_);
    double PbyEnergy = Eneeded; // Aprox 1h
    return std::min(Pmax, PbyEnergy);
}

double V2GModel::calculateAvailableDischargePower() const {
    if (v2gMode_ == V2G_UNIDIRECTIONAL) return 0.0;
    double Pmax = powerRating_ * availabilityFactor_;
    if (fleetSOC_ <= 0.1) return 0.0;
    double Eavail = energyCapacity_ * (fleetSOC_ - 0.1);
    double PbyEnergy = Eavail;
    return std::min(Pmax, PbyEnergy);
}

double V2GModel::calculateAvailableEnergy() const {
    return energyCapacity_ * (fleetSOC_ - 0.1) * fleetSOH_;
}

void V2GModel::updateArrivalDeparture(double currentHour) {
    // Fraccion conectada varia segun hora
    // Tipico: 60% en casa 18h-8h, 30% en trabajo 8h-18h
    if (currentHour >= 0.0 && currentHour < 8.0) {
        availabilityFactor_ = availabilityHome_;
    } else if (currentHour >= 8.0 && currentHour < 18.0) {
        availabilityFactor_ = availabilityWork_;
    } else {
        availabilityFactor_ = availabilityHome_ * 0.8
                              + availabilityPublic_ * 0.2;
    }
}

double V2GModel::getFleetAvailabilityFactor() const {
    return availabilityFactor_;
}

std::vector<ChargingProfile> V2GModel::getDailyProfile(
    double resolutionHours) {
    std::vector<ChargingProfile> profile;
    int steps = (int)(24.0 / resolutionHours);
    profile.reserve(steps);

    for (int s = 0; s < steps; ++s) {
        double t = s * resolutionHours;
        updateArrivalDeparture(t);

        ChargingProfile cp;
        cp.timeHour = t;
        cp.soc = fleetSOC_;

        // Precio segun hora (TOU)
        if (t >= 0.0 && t < 6.0) cp.cost = 0.08;   // Valley
        else if (t >= 6.0 && t < 10.0) cp.cost = 0.20; // Peak AM
        else if (t >= 10.0 && t < 17.0) cp.cost = 0.12; // Midday
        else if (t >= 17.0 && t < 22.0) cp.cost = 0.25; // Peak PM
        else cp.cost = 0.15; // Evening

        double P_charge = calculateAvailableChargePower();
        double P_discharge = calculateAvailableDischargePower();

        if (cp.cost < 0.12) {
            // Precio bajo: cargar
            cp.chargePower = -P_charge;
            cp.gridPower = -P_charge;
        } else if (cp.cost > 0.18) {
            // Precio alto: descargar V2G
            cp.chargePower = P_discharge;
            cp.gridPower = P_discharge;
            cp.revenue = P_discharge * cp.cost * 1000.0 * 0.2;
        } else {
            cp.chargePower = 0.0;
            cp.gridPower = 0.0;
        }

        profile.push_back(cp);
    }
    return profile;
}

} // namespace powsys365
