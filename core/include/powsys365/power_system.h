// core/include/powsys365/power_system.h

#ifndef POWSYS365_POWER_SYSTEM_H
#define POWSYS365_POWER_SYSTEM_H

#include <vector>
#include <string>
#include <unordered_map>
#include "../../commons/types.h"

namespace powsys365 {

class PowerSystem {
private:
    std::vector<Bus> buses_;
    std::vector<Line> lines_;
    std::vector<Generator> generators_;
    std::vector<Load> loads_;
    std::unordered_map<int, size_t> bus_index_; // bus_number -> index in buses_
    double base_mva_;
    double base_kv_;
    double frequency_;

public:
    PowerSystem(double base_mva = 100.0, double base_kv = 69.0, double frequency = 60.0);

    void addBus(const Bus& bus);
    void addLine(const Line& line);
    void addGenerator(const Generator& gen);
    void addLoad(const Load& load);

    const std::vector<Bus>& getBuses() const { return buses_; }
    const std::vector<Line>& getLines() const { return lines_; }
    const std::vector<Generator>& getGenerators() const { return generators_; }
    const std::vector<Load>& getLoads() const { return loads_; }

    size_t getBusIndex(int bus_number) const;

    double getBaseMVA() const { return base_mva_; }
    double getBaseKV() const { return base_kv_; }
    double getFrequency() const { return frequency_; }

    size_t getNumBuses() const { return buses_.size(); }
    size_t getNumLines() const { return lines_.size(); }
};

} // namespace powsys365

#endif // POWSYS365_POWER_SYSTEM_H