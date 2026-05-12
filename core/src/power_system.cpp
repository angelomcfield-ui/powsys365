// core/src/power_system.cpp

#include "powsys365/power_system.h"
#include <stdexcept>

namespace powsys365 {

PowerSystem::PowerSystem(double base_mva, double base_kv, double frequency)
    : base_mva_(base_mva), base_kv_(base_kv), frequency_(frequency) {}

void PowerSystem::addBus(const Bus& bus) {
    if (bus_index_.find(bus.number) != bus_index_.end()) {
        throw std::invalid_argument("Bus number already exists");
    }
    bus_index_[bus.number] = buses_.size();
    buses_.push_back(bus);
}

void PowerSystem::addLine(const Line& line) {
    // Validar que las barras existen
    if (bus_index_.find(line.from_bus) == bus_index_.end() ||
        bus_index_.find(line.to_bus) == bus_index_.end()) {
        throw std::invalid_argument("Line connects to non-existent bus");
    }
    lines_.push_back(line);
}

void PowerSystem::addGenerator(const Generator& gen) {
    if (bus_index_.find(gen.bus) == bus_index_.end()) {
        throw std::invalid_argument("Generator connected to non-existent bus");
    }
    generators_.push_back(gen);
}

void PowerSystem::addLoad(const Load& load) {
    if (bus_index_.find(load.bus) == bus_index_.end()) {
        throw std::invalid_argument("Load connected to non-existent bus");
    }
    loads_.push_back(load);
}

size_t PowerSystem::getBusIndex(int bus_number) const {
    auto it = bus_index_.find(bus_number);
    if (it == bus_index_.end()) {
        throw std::invalid_argument("Bus number not found");
    }
    return it->second;
}

} // namespace powsys365