#include "powsy365/power_system.h"
#include "powsy365/ybus_builder.h"
#include <queue>
#include <algorithm>
#include <numeric>

namespace powsys365 {

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

PowerSystem::PowerSystem() : baseMVA_(BASE_MVA_DEFAULT) {}

PowerSystem::PowerSystem(double baseMVA) : baseMVA_(baseMVA) {
    if (baseMVA <= 0.0) {
        throw std::invalid_argument("PowerSystem: baseMVA must be positive");
    }
}

// ============================================================================
// ELEMENT ADDERS
// ============================================================================

void PowerSystem::addBus(const Bus& bus) {
    if (bus.id == 0) {
        throw std::invalid_argument("PowerSystem::addBus: bus ID must be >= 1");
    }
    if (bus.id > topology_.buses.size()) {
        topology_.buses.resize(bus.id);
    }
    topology_.buses[bus.id - 1] = bus;
    rebuildBusIndexMap();
    ybusBuilt_ = false;
}

void PowerSystem::addLine(const Line& line) {
    if (line.fromBus == 0 || line.toBus == 0) {
        throw std::invalid_argument("PowerSystem::addLine: bus IDs must be >= 1");
    }
    topology_.lines.push_back(line);
    ybusBuilt_ = false;
}

void PowerSystem::addTransformer(const Transformer& transformer) {
    if (transformer.fromBus == 0 || transformer.toBus == 0) {
        throw std::invalid_argument(
            "PowerSystem::addTransformer: bus IDs must be >= 1");
    }
    topology_.transformers.push_back(transformer);
    ybusBuilt_ = false;
}

void PowerSystem::addGenerator(const Generator& generator) {
    if (generator.busId == 0) {
        throw std::invalid_argument(
            "PowerSystem::addGenerator: bus ID must be >= 1");
    }
    topology_.generators.push_back(generator);
    if (generator.busId <= topology_.buses.size()) {
        Bus& bus = topology_.buses[generator.busId - 1];
        bus.pg_pu += generator.pg_pu;
        bus.qg_pu += generator.qg_pu;
        if (bus.type == BusType::PV || bus.type == BusType::Slack) {
            bus.vm_pu = generator.vmSet_pu;
        }
    }
}

void PowerSystem::addLoad(const Load& load) {
    if (load.busId == 0) {
        throw std::invalid_argument("PowerSystem::addLoad: bus ID must be >= 1");
    }
    topology_.loads.push_back(load);
    if (load.busId <= topology_.buses.size()) {
        Bus& bus = topology_.buses[load.busId - 1];
        bus.pl_pu += load.pl_pu;
        bus.ql_pu += load.ql_pu;
    }
}

void PowerSystem::addShunt(const Shunt& shunt) {
    if (shunt.busId == 0) {
        throw std::invalid_argument("PowerSystem::addShunt: bus ID must be >= 1");
    }
    topology_.shunts.push_back(shunt);
    ybusBuilt_ = false;
}

// ============================================================================
// BUS INDEX MAPPING
// ============================================================================

void PowerSystem::rebuildBusIndexMap() {
    busIndexMap_.clear();
    for (size_t i = 0; i < topology_.buses.size(); ++i) {
        if (topology_.buses[i].id > 0) {
            busIndexMap_[topology_.buses[i].id] = i;
        }
    }
}

size_t PowerSystem::getBusIndex(size_t busId) const {
    auto it = busIndexMap_.find(busId);
    if (it == busIndexMap_.end()) {
        throw std::out_of_range(
            "PowerSystem: bus ID " + std::to_string(busId) + " not found");
    }
    return it->second;
}

bool PowerSystem::busExists(size_t busId) const {
    return busIndexMap_.find(busId) != busIndexMap_.end();
}

Bus* PowerSystem::getBus(size_t id) {
    if (id > 0 && id <= topology_.buses.size()) {
        return &topology_.buses[id - 1];
    }
    return nullptr;
}

const Bus* PowerSystem::getBus(size_t id) const {
    if (id > 0 && id <= topology_.buses.size()) {
        return &topology_.buses[id - 1];
    }
    return nullptr;
}

Line* PowerSystem::getLine(size_t id) {
    for (auto& line : topology_.lines) {
        if (line.id == id) return &line;
    }
    return nullptr;
}

Generator* PowerSystem::getGenerator(size_t id) {
    for (auto& gen : topology_.generators) {
        if (gen.id == id) return &gen;
    }
    return nullptr;
}

// ============================================================================
// YBUS MANAGEMENT
// ============================================================================

void PowerSystem::buildYbus() {
    if (topology_.buses.empty()) {
        throw std::runtime_error("PowerSystem::buildYbus: no buses in system");
    }

    YbusBuilder builder;
    ybus_ = builder.buildYbus(topology_, baseMVA_);
    ybusBuilt_ = true;
}

const SpMatrixC& PowerSystem::getYbus() const {
    if (!ybusBuilt_) {
        throw std::runtime_error(
            "PowerSystem::getYbus: Ybus not built. Call buildYbus() first.");
    }
    return ybus_;
}

SpMatrix PowerSystem::getG() const {
    if (!ybusBuilt_) {
        throw std::runtime_error("PowerSystem::getG: Ybus not built");
    }
    YbusBuilder builder;
    return builder.buildG(ybus_);
}

SpMatrix PowerSystem::getB() const {
    if (!ybusBuilt_) {
        throw std::runtime_error("PowerSystem::getB: Ybus not built");
    }
    YbusBuilder builder;
    return builder.buildB(ybus_);
}

// ============================================================================
// VOLTAGE MANAGEMENT
// ============================================================================

void PowerSystem::initializeVoltages() {
    for (auto& bus : topology_.buses) {
        if (bus.type == BusType::Slack) {
            bus.vm_pu = bus.vm_pu > 0 ? bus.vm_pu : 1.0;
            bus.va_rad = bus.va_deg * DEG_TO_RAD;
        } else if (bus.type == BusType::PV) {
            bus.vm_pu = bus.vm_pu > 0 ? bus.vm_pu : 1.0;
            bus.va_rad = 0.0;
            bus.va_deg = 0.0;
        } else {
            bus.vm_pu = 1.0;
            bus.va_rad = 0.0;
            bus.va_deg = 0.0;
        }
    }
}

void PowerSystem::updateBusVoltages(const DenseVector& vm, const DenseVector& va_rad) {
    const size_t n = topology_.buses.size();
    if (static_cast<size_t>(vm.size()) != n || static_cast<size_t>(va_rad.size()) != n) {
        throw std::invalid_argument(
            "PowerSystem::updateBusVoltages: vector dimension mismatch");
    }

    for (size_t i = 0; i < n; ++i) {
        topology_.buses[i].vm_pu = vm(static_cast<Eigen::Index>(i));
        topology_.buses[i].va_rad = va_rad(static_cast<Eigen::Index>(i));
        topology_.buses[i].va_deg = va_rad(static_cast<Eigen::Index>(i)) * RAD_TO_DEG;
    }
}

DenseVector PowerSystem::getVm() const {
    const size_t n = topology_.buses.size();
    DenseVector vm(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        vm(static_cast<Eigen::Index>(i)) = topology_.buses[i].vm_pu;
    }
    return vm;
}

DenseVector PowerSystem::getVa() const {
    const size_t n = topology_.buses.size();
    DenseVector va(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        va(static_cast<Eigen::Index>(i)) = topology_.buses[i].va_rad;
    }
    return va;
}

DenseVector PowerSystem::getVaDegrees() const {
    const size_t n = topology_.buses.size();
    DenseVector va(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        va(static_cast<Eigen::Index>(i)) = topology_.buses[i].va_deg;
    }
    return va;
}

DenseVectorC PowerSystem::getComplexVoltages() const {
    const size_t n = topology_.buses.size();
    DenseVectorC v(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        const double vm = topology_.buses[i].vm_pu;
        const double va = topology_.buses[i].va_rad;
        v(static_cast<Eigen::Index>(i)) = Complex(vm * std::cos(va), vm * std::sin(va));
    }
    return v;
}

// ============================================================================
// POWER CALCULATIONS
// ============================================================================

void PowerSystem::calculateInjectedPowers(DenseVector& pOut, DenseVector& qOut) const {
    if (!ybusBuilt_) {
        throw std::runtime_error(
            "PowerSystem::calculateInjectedPowers: Ybus not built");
    }

    const size_t n = topology_.buses.size();
    DenseVector vm(static_cast<Eigen::Index>(n));
    DenseVector va(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        vm(static_cast<Eigen::Index>(i)) = topology_.buses[i].vm_pu;
        va(static_cast<Eigen::Index>(i)) = topology_.buses[i].va_rad;
    }

    YbusBuilder builder;
    SpMatrix g = builder.buildG(ybus_);
    SpMatrix b = builder.buildB(ybus_);

    calculate_injected_powers(g, b, vm, va, pOut, qOut);
}

double PowerSystem::getTotalPGen() const {
    return std::accumulate(topology_.generators.begin(), topology_.generators.end(), 0.0,
        [](double sum, const Generator& g) { return sum + g.pg_pu; });
}

double PowerSystem::getTotalQGen() const {
    return std::accumulate(topology_.generators.begin(), topology_.generators.end(), 0.0,
        [](double sum, const Generator& g) { return sum + g.qg_pu; });
}

double PowerSystem::getTotalPLoad() const {
    return std::accumulate(topology_.loads.begin(), topology_.loads.end(), 0.0,
        [](double sum, const Load& l) { return sum + l.pl_pu; });
}

double PowerSystem::getTotalQLoad() const {
    return std::accumulate(topology_.loads.begin(), topology_.loads.end(), 0.0,
        [](double sum, const Load& l) { return sum + l.ql_pu; });
}

double PowerSystem::getTotalShuntPLoss() const {
    double loss = 0.0;
    const auto v = getComplexVoltages();
    for (size_t i = 0; i < topology_.buses.size(); ++i) {
        loss += topology_.buses[i].gsh_pu * std::norm(v(static_cast<Eigen::Index>(i)));
    }
    for (const auto& shunt : topology_.shunts) {
        if (shunt.status == 1 && shunt.busId > 0 &&
            shunt.busId <= topology_.buses.size()) {
            loss += shunt.g_pu *
                std::norm(v(static_cast<Eigen::Index>(shunt.busId - 1)));
        }
    }
    return loss;
}

double PowerSystem::getTotalShuntQInjection() const {
    double q = 0.0;
    const auto v = getComplexVoltages();
    for (size_t i = 0; i < topology_.buses.size(); ++i) {
        q -= topology_.buses[i].bsh_pu * std::norm(v(static_cast<Eigen::Index>(i)));
    }
    for (const auto& shunt : topology_.shunts) {
        if (shunt.status == 1 && shunt.busId > 0 &&
            shunt.busId <= topology_.buses.size()) {
            q -= shunt.b_pu *
                std::norm(v(static_cast<Eigen::Index>(shunt.busId - 1)));
        }
    }
    return q;
}

// ============================================================================
// VALIDATION
// ============================================================================

bool PowerSystem::isValid() const {
    if (topology_.buses.empty()) return false;
    if (!hasSlackBus()) return false;
    return isConnected();
}

bool PowerSystem::hasSlackBus() const {
    for (const auto& bus : topology_.buses) {
        if (bus.type == BusType::Slack) return true;
    }
    return false;
}

bool PowerSystem::isConnected() const {
    if (topology_.buses.empty()) return false;

    size_t slackIdx = 0;
    bool foundSlack = false;
    for (size_t i = 0; i < topology_.buses.size(); ++i) {
        if (topology_.buses[i].type == BusType::Slack) {
            slackIdx = i;
            foundSlack = true;
            break;
        }
    }
    if (!foundSlack) return false;

    std::vector<bool> visited(topology_.buses.size(), false);
    std::queue<size_t> queue;
    queue.push(slackIdx);
    visited[slackIdx] = true;
    size_t visitedCount = 1;

    std::vector<std::vector<size_t>> adj(topology_.buses.size());
    for (const auto& line : topology_.lines) {
        if (line.status != 1) continue;
        size_t fi = line.fromBus - 1;
        size_t ti = line.toBus - 1;
        if (fi < adj.size() && ti < adj.size()) {
            adj[fi].push_back(ti);
            adj[ti].push_back(fi);
        }
    }
    for (const auto& tx : topology_.transformers) {
        if (tx.status != 1) continue;
        size_t fi = tx.fromBus - 1;
        size_t ti = tx.toBus - 1;
        if (fi < adj.size() && ti < adj.size()) {
            adj[fi].push_back(ti);
            adj[ti].push_back(fi);
        }
    }

    while (!queue.empty()) {
        size_t current = queue.front();
        queue.pop();
        for (size_t neighbor : adj[current]) {
            if (!visited[neighbor]) {
                visited[neighbor] = true;
                ++visitedCount;
                queue.push(neighbor);
            }
        }
    }

    size_t activeBuses = 0;
    for (const auto& bus : topology_.buses) {
        if (bus.id > 0) ++activeBuses;
    }

    return visitedCount == activeBuses;
}

std::vector<Violation> PowerSystem::checkVoltageLimits() const {
    std::vector<Violation> violations;
    for (const auto& bus : topology_.buses) {
        if (bus.id == 0) continue;
        if (bus.vm_pu > bus.vmax_pu) {
            Violation v;
            v.type = ViolationType::OverVoltage;
            v.elementId = bus.id;
            v.elementName = bus.name;
            v.value = bus.vm_pu;
            v.limit = bus.vmax_pu;
            v.severity = bus.vm_pu / bus.vmax_pu;
            v.description = "Bus " + bus.name + " voltage " +
                std::to_string(bus.vm_pu) + " pu exceeds maximum " +
                std::to_string(bus.vmax_pu) + " pu";
            violations.push_back(v);
        } else if (bus.vm_pu < bus.vmin_pu) {
            Violation v;
            v.type = ViolationType::UnderVoltage;
            v.elementId = bus.id;
            v.elementName = bus.name;
            v.value = bus.vm_pu;
            v.limit = bus.vmin_pu;
            v.severity = bus.vmin_pu / bus.vm_pu;
            v.description = "Bus " + bus.name + " voltage " +
                std::to_string(bus.vm_pu) + " pu below minimum " +
                std::to_string(bus.vmin_pu) + " pu";
            violations.push_back(v);
        }
    }
    return violations;
}

// ============================================================================
// CLEAR
// ============================================================================

void PowerSystem::clear() {
    topology_.clear();
    ybus_.resize(0, 0);
    ybusBuilt_ = false;
    busIndexMap_.clear();
}

// ============================================================================
// IEEE 14-BUS TEST SYSTEM
// ============================================================================

void PowerSystem::loadIEEE14() {
    clear();
    baseMVA_ = 100.0;

    // Helper lambda to create and push a bus
    auto addBus = [&](size_t id, const char* name, BusType type, double baseV,
                      double vm, double vaDeg, double vaRad,
                      double pg, double qg, double pl, double ql,
                      double gsh, double bsh,
                      double vmin, double vmax, int area, int zone) {
        Bus bus;
        bus.id = id;
        bus.name = name;
        bus.type = type;
        bus.baseVoltage_kV = baseV;
        bus.vm_pu = vm;
        bus.va_deg = vaDeg;
        bus.va_rad = vaRad;
        bus.pg_pu = pg;
        bus.qg_pu = qg;
        bus.pl_pu = pl;
        bus.ql_pu = ql;
        bus.gsh_pu = gsh;
        bus.bsh_pu = bsh;
        bus.vmin_pu = vmin;
        bus.vmax_pu = vmax;
        bus.area = area;
        bus.zone = zone;
        topology_.buses.push_back(bus);
    };

    // Bus 1: Slack
    addBus(1, "Bus 1", BusType::Slack, 69.0, 1.060, 0.0, 0.0,
           2.324, 0.0, 0.0, 0.0, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 2: PV
    addBus(2, "Bus 2", BusType::PV, 69.0, 1.045, -4.98, 0.0,
           0.4, 0.0, 0.217, 0.127, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 3: PV
    addBus(3, "Bus 3", BusType::PV, 69.0, 1.010, -12.72, 0.0,
           0.0, 0.0, 0.942, 0.19, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 4: PQ
    addBus(4, "Bus 4", BusType::PQ, 69.0, 1.019, -10.33, 0.0,
           0.0, 0.0, 0.478, -0.039, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 5: PQ
    addBus(5, "Bus 5", BusType::PQ, 69.0, 1.020, -8.78, 0.0,
           0.0, 0.0, 0.076, 0.016, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 6: PQ
    addBus(6, "Bus 6", BusType::PQ, 13.8, 1.070, -14.22, 0.0,
           0.0, 0.0, 0.112, 0.075, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 7: PQ
    addBus(7, "Bus 7", BusType::PQ, 13.8, 1.062, -13.37, 0.0,
           0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 8: PV
    addBus(8, "Bus 8", BusType::PV, 18.0, 1.090, -13.36, 0.0,
           0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 9: PQ
    addBus(9, "Bus 9", BusType::PQ, 13.8, 1.056, -14.94, 0.0,
           0.0, 0.0, 0.295, 0.166, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 10: PQ
    addBus(10, "Bus 10", BusType::PQ, 13.8, 1.051, -15.10, 0.0,
            0.0, 0.0, 0.09, 0.058, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 11: PQ
    addBus(11, "Bus 11", BusType::PQ, 13.8, 1.057, -14.79, 0.0,
            0.0, 0.0, 0.035, 0.018, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 12: PQ
    addBus(12, "Bus 12", BusType::PQ, 13.8, 1.055, -15.07, 0.0,
            0.0, 0.0, 0.061, 0.016, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 13: PQ
    addBus(13, "Bus 13", BusType::PQ, 13.8, 1.050, -15.16, 0.0,
            0.0, 0.0, 0.135, 0.058, 0.0, 0.0, 1.06, 1.0, 1, 1);
    // Bus 14: PQ
    addBus(14, "Bus 14", BusType::PQ, 13.8, 1.036, -16.04, 0.0,
            0.0, 0.0, 0.149, 0.05, 0.0, 0.0, 1.06, 1.0, 1, 1);

    // Set angles in radians
    for (auto& bus : topology_.buses) {
        bus.va_rad = bus.va_deg * DEG_TO_RAD;
    }

    // Helper lambda for generators
    auto addGen = [&](size_t id, const char* name, size_t busId, GeneratorType gtype,
                      double pg, double qg, double qmax, double qmin,
                      double pgMax, double pgMin, double vmSet, double mbase,
                      double rs, double xs, double xd, double xdp, double xdpp,
                      double xq, double xqp, double xqpp,
                      double td0p, double td0pp, double tq0p, double tq0pp,
                      double h, double d, int status,
                      double c2, double c1, double c0) {
        Generator gen;
        gen.id = id;
        gen.name = name;
        gen.busId = busId;
        gen.genType = gtype;
        gen.pg_pu = pg;
        gen.qg_pu = qg;
        gen.qmax_pu = qmax;
        gen.qmin_pu = qmin;
        gen.pgMax_pu = pgMax;
        gen.pgMin_pu = pgMin;
        gen.vmSet_pu = vmSet;
        gen.mbase_pu = mbase;
        gen.rs_pu = rs;
        gen.xs_pu = xs;
        gen.xd_pu = xd;
        gen.xdPrime_pu = xdp;
        gen.xdDoublePrime_pu = xdpp;
        gen.xq_pu = xq;
        gen.xqPrime_pu = xqp;
        gen.xqDoublePrime_pu = xqpp;
        gen.td0Prime_s = td0p;
        gen.td0DoublePrime_s = td0pp;
        gen.tq0Prime_s = tq0p;
        gen.tq0DoublePrime_s = tq0pp;
        gen.h_inertia_s = h;
        gen.d_damping = d;
        gen.status = status;
        gen.cost_c2 = c2;
        gen.cost_c1 = c1;
        gen.cost_c0 = c0;
        topology_.generators.push_back(gen);
    };

    // Generator 1 at Bus 1
    addGen(1, "Gen 1", 1, GeneratorType::Thermal,
           2.324, 0.0, 999.0, -999.0, 999.0, 0.0, 1.060, 100.0,
           0.0, 0.0, 1.0, 0.3, 0.2, 0.6, 0.4, 0.2,
           5.0, 0.05, 0.5, 0.03, 5.0, 0.0, 1,
           0.043029, 20.0, 0.0);
    // Generator 2 at Bus 2
    addGen(2, "Gen 2", 2, GeneratorType::Thermal,
           0.4, 0.0, 0.5, -0.4, 1.0, 0.0, 1.045, 100.0,
           0.0, 0.0, 1.0, 0.3, 0.2, 0.6, 0.4, 0.2,
           5.0, 0.05, 0.5, 0.03, 4.0, 0.0, 1,
           0.25, 20.0, 0.0);
    // Generator 3 at Bus 3
    addGen(3, "Gen 3", 3, GeneratorType::Thermal,
           0.0, 0.0, 0.4, 0.0, 1.0, 0.0, 1.010, 100.0,
           0.0, 0.0, 1.0, 0.3, 0.2, 0.6, 0.4, 0.2,
           5.0, 0.05, 0.5, 0.03, 6.0, 0.0, 1,
           0.01, 40.0, 0.0);
    // Generator 6 at Bus 6
    addGen(6, "Gen 6", 6, GeneratorType::Thermal,
           0.0, 0.0, 0.24, -0.06, 1.0, 0.0, 1.070, 100.0,
           0.0, 0.0, 1.0, 0.3, 0.2, 0.6, 0.4, 0.2,
           5.0, 0.05, 0.5, 0.03, 5.0, 0.0, 1,
           0.01, 40.0, 0.0);
    // Generator 8 at Bus 8
    addGen(8, "Gen 8", 8, GeneratorType::Thermal,
           0.0, 0.0, 0.24, -0.06, 1.0, 0.0, 1.090, 100.0,
           0.0, 0.0, 1.0, 0.3, 0.2, 0.6, 0.4, 0.2,
           5.0, 0.05, 0.5, 0.03, 5.0, 0.0, 1,
           0.01, 40.0, 0.0);

    // Helper lambda for lines
    auto addLine = [&](size_t id, const char* name, size_t f, size_t t,
                       double r, double x, double bch, double rateA,
                       double rateB, double rateC, double ratio,
                       double angle, int status) {
        Line line;
        line.id = id;
        line.name = name;
        line.fromBus = f;
        line.toBus = t;
        line.r_pu = r;
        line.x_pu = x;
        line.bch_pu = bch;
        line.rateA_pu = rateA;
        line.rateB_pu = rateB;
        line.rateC_pu = rateC;
        line.ratio = ratio;
        line.angle_deg = angle;
        line.status = status;
        line.model = LineModel::PI;
        line.fracBFrom = 0.5;
        line.fracBTo = 0.5;
        topology_.lines.push_back(line);
    };

    // Lines 1-17 (transmission lines)
    addLine(1,  "L1-2",   1,  2,  0.01938, 0.05917, 0.0528,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(2,  "L1-5",   1,  5,  0.05403, 0.22304, 0.0492,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(3,  "L2-3",   2,  3,  0.04699, 0.19797, 0.0438,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(4,  "L2-4",   2,  4,  0.05811, 0.17632, 0.0340,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(5,  "L2-5",   2,  5,  0.05695, 0.17388, 0.0346,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(6,  "L3-4",   3,  4,  0.06701, 0.17103, 0.0128,  999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(7,  "L4-5",   4,  5,  0.01335, 0.04211, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(8,  "L6-11",  6,  11, 0.09498, 0.19890, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(9,  "L6-12",  6,  12, 0.12291, 0.25581, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(10, "L6-13",  6,  13, 0.06615, 0.13027, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(11, "L7-8",   7,  8,  0.0,     0.17615, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(12, "L7-9",   7,  9,  0.0,     0.11001, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(13, "L9-10",  9,  10, 0.03181, 0.08450, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(14, "L9-14",  9,  14, 0.12711, 0.27038, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(15, "L10-11", 10, 11, 0.08205, 0.19207, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(16, "L12-13", 12, 13, 0.22092, 0.19988, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);
    addLine(17, "L13-14", 13, 14, 0.17093, 0.34802, 0.0,     999.0, 0.0, 0.0, 0.0, 0.0, 1);

    // Lines 18-20: Transformers (represented as lines with off-nominal ratio)
    addLine(18, "T4-7", 4, 7, 0.0, 0.20912, 0.0, 999.0, 0.0, 0.0, 0.978, 0.0, 1);
    addLine(19, "T4-9", 4, 9, 0.0, 0.55618, 0.0, 999.0, 0.0, 0.0, 0.969, 0.0, 1);
    addLine(20, "T5-6", 5, 6, 0.0, 0.25202, 0.0, 999.0, 0.0, 0.0, 0.932, 0.0, 1);

    // Shunt capacitor at bus 9
    Shunt sh;
    sh.id = 1;
    sh.busId = 9;
    sh.g_pu = 0.0;
    sh.b_pu = -0.19;
    sh.status = 1;
    topology_.shunts.push_back(sh);

    rebuildBusIndexMap();
}

// ============================================================================
// IEEE 30-BUS TEST SYSTEM (minimal skeleton)
// ============================================================================

void PowerSystem::loadIEEE30() {
    clear();
    baseMVA_ = 100.0;

    for (int i = 1; i <= 30; ++i) {
        Bus bus;
        bus.id = static_cast<size_t>(i);
        bus.name = "Bus " + std::to_string(i);
        bus.type = (i == 1) ? BusType::Slack :
                   (i == 2 || i == 5 || i == 8 || i == 11 || i == 13) ? BusType::PV :
                   BusType::PQ;
        bus.baseVoltage_kV = (i <= 12) ? 132.0 : 33.0;
        bus.vm_pu = 1.0;
        bus.va_deg = 0.0;
        bus.va_rad = 0.0;
        topology_.buses.push_back(bus);
    }

    struct GenData { int bus; double pg; double qmax; double qmin; double vset; };
    std::vector<GenData> genData = {
        {1, 2.6, 999.0, -999.0, 1.06},
        {2, 0.4, 0.5, -0.4, 1.045},
        {5, 0.0, 0.4, -0.4, 1.01},
        {8, 0.0, 0.4, -0.1, 1.01},
        {11, 0.0, 0.24, -0.06, 1.082},
        {13, 0.0, 0.24, -0.06, 1.071}
    };
    for (const auto& gd : genData) {
        Generator gen;
        gen.id = static_cast<size_t>(gd.bus);
        gen.busId = static_cast<size_t>(gd.bus);
        gen.name = "Gen " + std::to_string(gd.bus);
        gen.pg_pu = gd.pg;
        gen.qmax_pu = gd.qmax;
        gen.qmin_pu = gd.qmin;
        gen.vmSet_pu = gd.vset;
        topology_.generators.push_back(gen);

        if (gd.bus <= 30) {
            topology_.buses[gd.bus - 1].pg_pu = gd.pg;
        }
    }

    struct LineData { int id; int f; int t; double r; double x; double b; };
    std::vector<LineData> lineData = {
        {1, 1, 2, 0.0192, 0.0575, 0.0528},
        {2, 1, 3, 0.0452, 0.1852, 0.0408},
        {3, 2, 4, 0.0570, 0.1737, 0.0368},
        {4, 3, 4, 0.0132, 0.0379, 0.0084},
        {5, 2, 5, 0.0472, 0.1983, 0.0418},
        {6, 2, 6, 0.0581, 0.1763, 0.0374},
        {7, 4, 6, 0.0119, 0.0414, 0.0090},
        {8, 5, 7, 0.0460, 0.1160, 0.0204},
        {9, 6, 7, 0.0267, 0.0820, 0.0170},
        {10, 6, 8, 0.0120, 0.0420, 0.0090}
    };
    for (const auto& ld : lineData) {
        Line line;
        line.id = static_cast<size_t>(ld.id);
        line.name = "L" + std::to_string(ld.f) + "-" + std::to_string(ld.t);
        line.fromBus = static_cast<size_t>(ld.f);
        line.toBus = static_cast<size_t>(ld.t);
        line.r_pu = ld.r;
        line.x_pu = ld.x;
        line.bch_pu = ld.b;
        line.rateA_pu = 130.0 / baseMVA_;
        topology_.lines.push_back(line);
    }

    rebuildBusIndexMap();
}

// ============================================================================
// IEEE 57-BUS TEST SYSTEM (skeleton)
// ============================================================================

void PowerSystem::loadIEEE57() {
    clear();
    baseMVA_ = 100.0;

    for (int i = 1; i <= 57; ++i) {
        Bus bus;
        bus.id = static_cast<size_t>(i);
        bus.name = "Bus " + std::to_string(i);
        bus.type = (i == 1) ? BusType::Slack :
                   (i == 2 || i == 3 || i == 6 || i == 8 || i == 9 || i == 12) ? BusType::PV :
                   BusType::PQ;
        bus.baseVoltage_kV = (i <= 16) ? 138.0 : (i <= 48) ? 69.0 : 13.8;
        bus.vm_pu = 1.0;
        topology_.buses.push_back(bus);
    }

    std::vector<int> genBuses = {1, 2, 3, 6, 8, 9, 12};
    for (size_t idx = 0; idx < genBuses.size(); ++idx) {
        Generator gen;
        gen.id = idx + 1;
        gen.busId = static_cast<size_t>(genBuses[idx]);
        gen.name = "Gen " + std::to_string(genBuses[idx]);
        gen.pgMax_pu = 5.0;
        gen.qmax_pu = 2.0;
        gen.qmin_pu = -1.0;
        topology_.generators.push_back(gen);
    }

    rebuildBusIndexMap();
}

// ============================================================================
// IEEE 118-BUS TEST SYSTEM (skeleton)
// ============================================================================

void PowerSystem::loadIEEE118() {
    clear();
    baseMVA_ = 100.0;

    for (int i = 1; i <= 118; ++i) {
        Bus bus;
        bus.id = static_cast<size_t>(i);
        bus.name = "Bus " + std::to_string(i);
        bus.type = (i == 69) ? BusType::Slack :
                   (i == 10 || i == 12 || i == 25 || i == 26 || i == 31 ||
                    i == 46 || i == 49 || i == 54 || i == 59 || i == 61 ||
                    i == 65 || i == 66 || i == 80 || i == 87 || i == 89 ||
                    i == 100 || i == 103 || i == 111 || i == 116) ? BusType::PV :
                   BusType::PQ;
        bus.vm_pu = 1.0;
        topology_.buses.push_back(bus);
    }

    std::vector<int> genBuses = {
        1, 4, 6, 8, 10, 12, 15, 18, 19, 24, 25, 26, 27, 31, 32,
        34, 36, 40, 42, 46, 49, 54, 55, 56, 59, 61, 62, 65, 66, 69,
        70, 72, 73, 74, 76, 77, 80, 85, 87, 89, 90, 91, 92, 99, 100,
        103, 104, 105, 107, 110, 111, 112, 113, 116
    };
    for (size_t idx = 0; idx < genBuses.size(); ++idx) {
        Generator gen;
        gen.id = idx + 1;
        gen.busId = static_cast<size_t>(genBuses[idx]);
        gen.name = "Gen " + std::to_string(genBuses[idx]);
        gen.pgMax_pu = 10.0;
        gen.qmax_pu = 5.0;
        gen.qmin_pu = -2.0;
        topology_.generators.push_back(gen);
    }

    rebuildBusIndexMap();
}

} // namespace powsys365
