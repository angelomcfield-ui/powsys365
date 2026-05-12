/**
 * @file bindings.cpp
 * @brief Python bindings for POWSYS365 C++ core engine using pybind11.
 *
 * Exposes power system modeling, load flow solvers, short-circuit
 * analysis, and network building utilities to Python 3.13+.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/eigen.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>

#include "powsy365/core/bus.h"
#include "powsy365/core/line.h"
#include "powsy365/core/transformer.h"
#include "powsy365/core/generator.h"
#include "powsy365/core/load.h"
#include "powsy365/core/solver_config.h"
#include "powsy365/core/power_system.h"
#include "powsy365/core/ybus_builder.h"
#include "powsy365/core/load_flow_solver.h"
#include "powsy365/core/short_circuit_solver.h"
#include "powsy365/core/results.h"

namespace py = pybind11;
using namespace powsy365;

/* ------------------------------------------------------------------ */
/*  Enums                                                              */
/* ------------------------------------------------------------------ */

void bind_enums(py::module_& m) {
    py::enum_<BusType>(m, "BusType",
                       "Enumeration of electrical bus types.")
        .value("PQ", BusType::PQ,
               "Load bus (P and Q specified).")
        .value("PV", BusType::PV,
               "Generator bus (P and |V| specified).")
        .value("Slack", BusType::Slack,
               "Slack / swing bus (|V| and angle specified).");

    py::enum_<FaultType>(m, "FaultType",
                         "Enumeration of short-circuit fault types.")
        .value("ThreePhase", FaultType::ThreePhase,
               "Balanced three-phase fault (L-L-L).")
        .value("SinglePhase", FaultType::SinglePhase,
               "Single line-to-ground fault (L-G).")
        .value("TwoPhase", FaultType::TwoPhase,
               "Line-to-line fault (L-L).")
        .value("TwoPhaseG", FaultType::TwoPhaseG,
               "Double line-to-ground fault (L-L-G).");

    py::enum_<SolverMethod>(m, "SolverMethod",
                            "Enumeration of power flow solution methods.")
        .value("NewtonRaphson", SolverMethod::NewtonRaphson,
               "Full Newton-Raphson (most accurate).")
        .value("FastDecoupled", SolverMethod::FastDecoupled,
               "Fast-decoupled BX / XB method.")
        .value("GaussSeidel", SolverMethod::GaussSeidel,
               "Gauss-Seidel (legacy, slow convergence).");
}

/* ------------------------------------------------------------------ */
/*  Structs                                                            */
/* ------------------------------------------------------------------ */

void bind_bus(py::module_& m) {
    py::class_<Bus>(m, "Bus",
                    "Represents an electrical bus (node) in the power system.")
        .def(py::init<int, std::string, BusType, double, double,
                      std::complex<double>, std::complex<double>>(),
             py::arg("id"), py::arg("name") = "",
             py::arg("type") = BusType::PQ,
             py::arg("base_kv") = 1.0,
             py::arg("vmin") = 0.9, py::arg("vmax") = 1.1,
             py::arg("voltage") = std::complex<double>(1.0, 0.0),
             py::arg("generation") = std::complex<double>(0.0, 0.0),
             "Construct a Bus with full parameters.")
        .def_readwrite("id", &Bus::id,
                       "Unique bus identifier (integer).")
        .def_readwrite("name", &Bus::name,
                       "Human-readable bus name.")
        .def_readwrite("type", &Bus::type,
                       "Bus type (PQ, PV, or Slack).")
        .def_readwrite("base_kv", &Bus::base_kv,
                       "Base voltage in kilovolts [kV].")
        .def_readwrite("vmin", &Bus::vmin,
                       "Minimum voltage magnitude in p.u.")
        .def_readwrite("vmax", &Bus::vmax,
                       "Maximum voltage magnitude in p.u.")
        .def_readwrite("voltage", &Bus::voltage,
                       "Complex voltage in per-unit (V = |V|e^(j*theta)).")
        .def_readwrite("generation", &Bus::generation,
                       "Complex generation at this bus [p.u.].")
        .def_readwrite("shunt_conductance", &Bus::shunt_conductance,
                       "Shunt conductance G [p.u.].")
        .def_readwrite("shunt_susceptance", &Bus::shunt_susceptance,
                       "Shunt susceptance B [p.u.].")
        .def_readwrite("area", &Bus::area,
                       "Control area number.")
        .def_readwrite("zone", &Bus::zone,
                       "Loss zone number.")
        .def("__repr__",
             [](const Bus& b) {
                 return "<Bus id=" + std::to_string(b.id) +
                        " name='" + b.name + "' type=" +
                        (b.type == BusType::PQ     ? "PQ"
                         : b.type == BusType::PV   ? "PV"
                                                   : "Slack") +
                        " base_kv=" + std::to_string(b.base_kv) + ">";
             },
             "String representation of the bus.");
}

void bind_line(py::module_& m) {
    py::class_<Line>(m, "Line",
                     "Represents a transmission line between two buses.")
        .def(py::init<int, int, int, double, double, double, double>(),
             py::arg("id"), py::arg("from_bus"), py::arg("to_bus"),
             py::arg("r") = 0.0, py::arg("x") = 0.1,
             py::arg("b") = 0.0, py::arg("rate_a") = 0.0,
             "Construct a transmission line.")
        .def_readwrite("id", &Line::id, "Line identifier.")
        .def_readwrite("from_bus", &Line::from_bus, "From bus ID.")
        .def_readwrite("to_bus", &Line::to_bus, "To bus ID.")
        .def_readwrite("r", &Line::r, "Series resistance [p.u.].")
        .def_readwrite("x", &Line::x, "Series reactance [p.u.].")
        .def_readwrite("b", &Line::b,
                       "Total line charging susceptance [p.u.].")
        .def_readwrite("rate_a", &Line::rate_a,
                       "Thermal rating (MVA) - normal operation.")
        .def_readwrite("rate_b", &Line::rate_b,
                       "Thermal rating (MVA) - short-term.")
        .def_readwrite("rate_c", &Line::rate_c,
                       "Thermal rating (MVA) - emergency.")
        .def_readwrite("status", &Line::status,
                       "In-service flag (1=in, 0=out).")
        .def("__repr__",
             [](const Line& l) {
                 return "<Line id=" + std::to_string(l.id) +
                        " from=" + std::to_string(l.from_bus) +
                        " to=" + std::to_string(l.to_bus) +
                        " r=" + std::to_string(l.r) +
                        " x=" + std::to_string(l.x) + ">";
             });
}

void bind_transformer(py::module_& m) {
    py::class_<Transformer>(m, "Transformer",
                            "Represents a two-winding transformer.")
        .def(py::init<int, int, int, double, double, double, double>(),
             py::arg("id"), py::arg("from_bus"), py::arg("to_bus"),
             py::arg("r") = 0.0, py::arg("x") = 0.1,
             py::arg("tap") = 1.0, py::arg("shift") = 0.0,
             "Construct a two-winding transformer.")
        .def_readwrite("id", &Transformer::id)
        .def_readwrite("from_bus", &Transformer::from_bus)
        .def_readwrite("to_bus", &Transformer::to_bus)
        .def_readwrite("r", &Transformer::r, "Series resistance [p.u.].")
        .def_readwrite("x", &Transformer::x, "Series reactance [p.u.].")
        .def_readwrite("tap", &Transformer::tap,
                       "Off-nominal turns ratio.")
        .def_readwrite("shift", &Transformer::shift,
                       "Phase shift angle [degrees].")
        .def_readwrite("rate_a", &Transformer::rate_a, "Rating MVA.")
        .def_readwrite("status", &Transformer::status)
        .def("__repr__",
             [](const Transformer& t) {
                 return "<Transformer id=" + std::to_string(t.id) +
                        " from=" + std::to_string(t.from_bus) +
                        " to=" + std::to_string(t.to_bus) +
                        " tap=" + std::to_string(t.tap) + ">";
             });
}

void bind_generator(py::module_& m) {
    py::class_<Generator>(m, "Generator",
                          "Represents a synchronous generator.")
        .def(py::init<int, int, double, double, double, double,
                      double, double>(),
             py::arg("id"), py::arg("bus_id"),
             py::arg("pg") = 0.0, py::arg("qg") = 0.0,
             py::arg("qmin") = -9999.0, py::arg("qmax") = 9999.0,
             py::arg("vg") = 1.0, py::arg("mbase") = 100.0,
             py::arg("pg_min") = 0.0, py::arg("pg_max") = 9999.0,
             py::arg("cost_a") = 0.0, py::arg("cost_b") = 0.0,
             py::arg("cost_c") = 0.0,
             "Construct a generator unit.")
        .def_readwrite("id", &Generator::id)
        .def_readwrite("bus_id", &Generator::bus_id)
        .def_readwrite("pg", &Generator::pg,
                       "Active power output [MW].")
        .def_readwrite("qg", &Generator::qg,
                       "Reactive power output [MVAr].")
        .def_readwrite("qmin", &Generator::qmin,
                       "Min reactive power limit [MVAr].")
        .def_readwrite("qmax", &Generator::qmax,
                       "Max reactive power limit [MVAr].")
        .def_readwrite("vg", &Generator::vg,
                       "Voltage setpoint [p.u.].")
        .def_readwrite("mbase", &Generator::mbase,
                       "Machine base MVA.")
        .def_readwrite("pg_min", &Generator::pg_min,
                       "Min active power [MW].")
        .def_readwrite("pg_max", &Generator::pg_max,
                       "Max active power [MW].")
        .def_readwrite("status", &Generator::status,
                       "In-service flag.")
        .def_readwrite("cost_a", &Generator::cost_a,
                       "Quadratic cost coefficient $/MW^2.")
        .def_readwrite("cost_b", &Generator::cost_b,
                       "Linear cost coefficient $/MW.")
        .def_readwrite("cost_c", &Generator::cost_c,
                       "Constant cost coefficient $.")
        .def("__repr__",
             [](const Generator& g) {
                 return "<Generator id=" + std::to_string(g.id) +
                        " bus=" + std::to_string(g.bus_id) +
                        " pg=" + std::to_string(g.pg) + "MW>";
             });
}

void bind_load(py::module_& m) {
    py::class_<Load>(m, "Load",
                     "Represents a power load/demand at a bus.")
        .def(py::init<int, int, double, double, double, double>(),
             py::arg("id"), py::arg("bus_id"),
             py::arg("pd") = 0.0, py::arg("qd") = 0.0,
             py::arg("ip") = 0.0, py::arg("iq") = 0.0,
             py::arg("yp") = 0.0, py::arg("yq") = 0.0,
             "Construct a load.")
        .def_readwrite("id", &Load::id)
        .def_readwrite("bus_id", &Load::bus_id)
        .def_readwrite("pd", &Load::pd,
                       "Constant active power demand [MW].")
        .def_readwrite("qd", &Load::qd,
                       "Constant reactive power demand [MVAr].")
        .def_readwrite("ip", &Load::ip,
                       "Constant-current active component [MW @ 1 p.u.].")
        .def_readwrite("iq", &Load::iq,
                       "Constant-current reactive component.")
        .def_readwrite("yp", &Load::yp,
                       "Constant-impedance active component [MW @ 1 p.u.].")
        .def_readwrite("yq", &Load::yq,
                       "Constant-impedance reactive component.")
        .def_readwrite("status", &Load::status)
        .def("__repr__",
             [](const Load& ld) {
                 return "<Load id=" + std::to_string(ld.id) +
                        " bus=" + std::to_string(ld.bus_id) +
                        " pd=" + std::to_string(ld.pd) +
                        " qd=" + std::to_string(ld.qd) + "MVA>";
             });
}

void bind_solver_config(py::module_& m) {
    py::class_<SolverConfig>(m, "SolverConfig",
                             "Configuration for load-flow solvers.")
        .def(py::init<>(),
             "Create solver config with sensible defaults.")
        .def_readwrite("tolerance", &SolverConfig::tolerance,
                       "Mismatch tolerance (default 1e-6).")
        .def_readwrite("max_iterations", &SolverConfig::max_iterations,
                       "Maximum solver iterations (default 30).")
        .def_readwrite("method", &SolverConfig::method,
                       "Solver method enumeration.")
        .def_readwrite("flat_start", &SolverConfig::flat_start,
                       "Use flat-start voltages (1.0 + j0.0).")
        .def_readwrite("damping_factor", &SolverConfig::damping_factor,
                       "Newton step damping [0..1].")
        .def_readwrite("enforce_q_limits",
                       &SolverConfig::enforce_q_limits,
                       "Enforce generator Q limits (PV->PQ).")
        .def_readwrite("base_mva", &SolverConfig::base_mva,
                       "System base MVA (default 100).")
        .def("__repr__",
             [](const SolverConfig& sc) {
                 return "<SolverConfig tol=" +
                        std::to_string(sc.tolerance) +
                        " max_iter=" +
                        std::to_string(sc.max_iterations) + ">";
             });
}

/* ------------------------------------------------------------------ */
/*  Results                                                            */
/* ------------------------------------------------------------------ */

void bind_results(py::module_& m) {
    py::class_<PowerFlowBusResult>(m, "PowerFlowBusResult",
                                    "Per-bus power-flow results.")
        .def(py::init<>())
        .def_readwrite("bus_id", &PowerFlowBusResult::bus_id)
        .def_readwrite("vm", &PowerFlowBusResult::vm,
                       "Voltage magnitude [p.u.].")
        .def_readwrite("va", &PowerFlowBusResult::va,
                       "Voltage angle [degrees].")
        .def_readwrite("p_gen", &PowerFlowBusResult::p_gen,
                       "Net active generation [MW].")
        .def_readwrite("q_gen", &PowerFlowBusResult::q_gen,
                       "Net reactive generation [MVAr].")
        .def_readwrite("p_load", &PowerFlowBusResult::p_load,
                       "Net active load [MW].")
        .def_readwrite("q_load", &PowerFlowBusResult::q_load,
                       "Net reactive load [MVAr].")
        .def_readwrite("p_injected", &PowerFlowBusResult::p_injected,
                       "Net active injection [MW].")
        .def_readwrite("q_injected", &PowerFlowBusResult::q_injected,
                       "Net reactive injection [MVAr].")
        .def("__repr__",
             [](const PowerFlowBusResult& r) {
                 return "<PowerFlowBusResult bus=" +
                        std::to_string(r.bus_id) +
                        " Vm=" + std::to_string(r.vm) +
                        " Va=" + std::to_string(r.va) + ">";
             });

    py::class_<PowerFlowLineResult>(m, "PowerFlowLineResult",
                                     "Per-branch power-flow results.")
        .def(py::init<>())
        .def_readwrite("line_id", &PowerFlowLineResult::line_id)
        .def_readwrite("from_bus", &PowerFlowLineResult::from_bus)
        .def_readwrite("to_bus", &PowerFlowLineResult::to_bus)
        .def_readwrite("p_from", &PowerFlowLineResult::p_from,
                       "Active power at from end [MW].")
        .def_readwrite("q_from", &PowerFlowLineResult::q_from,
                       "Reactive power at from end [MVAr].")
        .def_readwrite("p_to", &PowerFlowLineResult::p_to,
                       "Active power at to end [MW].")
        .def_readwrite("q_to", &PowerFlowLineResult::q_to,
                       "Reactive power at to end [MVAr].")
        .def_readwrite("s_apparent", &PowerFlowLineResult::s_apparent,
                       "Apparent power flow [MVA].")
        .def_readwrite("loss_p", &PowerFlowLineResult::loss_p,
                       "Active power loss [MW].")
        .def_readwrite("loss_q", &PowerFlowLineResult::loss_q,
                       "Reactive power loss [MVAr].")
        .def_readwrite("loading_percent",
                       &PowerFlowLineResult::loading_percent,
                       "Line loading [% of rating].")
        .def("__repr__",
             [](const PowerFlowLineResult& r) {
                 return "<PowerFlowLineResult line=" +
                        std::to_string(r.line_id) +
                        " from=" + std::to_string(r.from_bus) +
                        " to=" + std::to_string(r.to_bus) +
                        " loading=" +
                        std::to_string(r.loading_percent) + "%>";
             });

    py::class_<PowerFlowResult>(m, "PowerFlowResult",
                                 "Aggregated power-flow solution.")
        .def(py::init<>())
        .def_readwrite("converged", &PowerFlowResult::converged,
                       "True if solver converged.")
        .def_readwrite("iterations", &PowerFlowResult::iterations,
                       "Number of iterations performed.")
        .def_readwrite("elapsed_ms", &PowerFlowResult::elapsed_ms,
                       "Wall-clock time [milliseconds].")
        .def_readwrite("final_mismatch", &PowerFlowResult::final_mismatch,
                       "Final max mismatch value.")
        .def_readwrite("method", &PowerFlowResult::method,
                       "Solver method name.")
        .def_readwrite("bus_results", &PowerFlowResult::bus_results,
                       py::return_value_policy::reference,
                       "List of per-bus results.")
        .def_readwrite("line_results", &PowerFlowResult::line_results,
                       py::return_value_policy::reference,
                       "List of per-line results.")
        .def_readwrite("total_pgen", &PowerFlowResult::total_pgen,
                       "Total active generation [MW].")
        .def_readwrite("total_pload", &PowerFlowResult::total_pload,
                       "Total active load [MW].")
        .def_readwrite("total_qgen", &PowerFlowResult::total_qgen,
                       "Total reactive generation [MVAr].")
        .def_readwrite("total_qload", &PowerFlowResult::total_qload,
                       "Total reactive load [MVAr].")
        .def_readwrite("total_ploss", &PowerFlowResult::total_ploss,
                       "Total active losses [MW].")
        .def_readwrite("total_qloss", &PowerFlowResult::total_qloss,
                       "Total reactive losses [MVAr].")
        .def("__repr__",
             [](const PowerFlowResult& r) {
                 return "<PowerFlowResult converged=" +
                        std::string(r.converged ? "True" : "False") +
                        " iterations=" + std::to_string(r.iterations) +
                        " Ploss=" + std::to_string(r.total_ploss) +
                        "MW>";
             });
}

/* ------------------------------------------------------------------ */
/*  PowerSystem                                                        */
/* ------------------------------------------------------------------ */

void bind_power_system(py::module_& m) {
    py::class_<PowerSystem>(m, "PowerSystem",
                             "Container and manager for power system data.")
        .def(py::init<double>(),
             py::arg("base_mva") = 100.0,
             "Create an empty power system model.")
        /* -- Bus management -- */
        .def("addBus", &PowerSystem::addBus,
             py::arg("bus"),
             "Add a bus to the system.")
        .def("getBus", &PowerSystem::getBus,
             py::arg("id"),
             py::return_value_policy::reference,
             "Retrieve a bus by ID (reference).")
        .def("getBusCount", &PowerSystem::getBusCount,
             "Number of buses in the system.")
        .def("getAllBuses", &PowerSystem::getAllBuses,
             py::return_value_policy::reference,
             "Return list of all buses.")
        /* -- Line management -- */
        .def("addLine", &PowerSystem::addLine,
             py::arg("line"),
             "Add a transmission line.")
        .def("getLineCount", &PowerSystem::getLineCount,
             "Number of lines.")
        /* -- Transformer management -- */
        .def("addTransformer", &PowerSystem::addTransformer,
             py::arg("transformer"),
             "Add a two-winding transformer.")
        /* -- Generator management -- */
        .def("addGenerator", &PowerSystem::addGenerator,
             py::arg("generator"),
             "Add a generator.")
        .def("getGeneratorCount", &PowerSystem::getGeneratorCount,
             "Number of generators.")
        .def("getTotalPGen", &PowerSystem::getTotalPGen,
             "Sum of active generation [MW].")
        /* -- Load management -- */
        .def("addLoad", &PowerSystem::addLoad,
             py::arg("load"),
             "Add a load.")
        .def("getTotalPLoad", &PowerSystem::getTotalPLoad,
             "Sum of active load demand [MW].")
        /* -- System operations -- */
        .def("buildYbus", &PowerSystem::buildYbus,
             "Build the bus admittance matrix Ybus.")
        .def("getYbus", &PowerSystem::getYbus,
             py::return_value_policy::reference,
             "Get the sparse Ybus matrix (reference).")
        .def("initializeVoltages", &PowerSystem::initializeVoltages,
             py::arg("flat_start") = true,
             "Set initial voltage guess.")
        .def("getVoltageVector", &PowerSystem::getVoltageVector,
             "Get complex voltage vector.")
        /* -- Validation -- */
        .def("isValid", &PowerSystem::isValid,
             "Check model consistency and data validity.")
        .def("checkVoltageLimits",
             &PowerSystem::checkVoltageLimits,
             "Return buses outside voltage limits.")
        /* -- IEEE test cases -- */
        .def("loadIEEE14", &PowerSystem::loadIEEE14,
             "Populate with IEEE 14-bus test case.")
        .def("loadIEEE30", &PowerSystem::loadIEEE30,
             "Populate with IEEE 30-bus test case.")
        .def("loadIEEE57", &PowerSystem::loadIEEE57,
             "Populate with IEEE 57-bus test case.")
        .def("loadIEEE118", &PowerSystem::loadIEEE118,
             "Populate with IEEE 118-bus test case.")
        /* -- Properties -- */
        .def_readwrite("base_mva", &PowerSystem::base_mva,
                       "System base MVA.")
        .def_readwrite("name", &PowerSystem::name,
                       "System name / description.")
        .def("__repr__",
             [](const PowerSystem& ps) {
                 return "<PowerSystem buses=" +
                        std::to_string(ps.getBusCount()) +
                        " lines=" + std::to_string(ps.getLineCount()) +
                        " gens=" + std::to_string(ps.getGeneratorCount()) +
                        " base_mva=" + std::to_string(ps.base_mva) + ">";
             });
}

/* ------------------------------------------------------------------ */
/*  YbusBuilder                                                        */
/* ------------------------------------------------------------------ */

void bind_ybus_builder(py::module_& m) {
    py::class_<YbusBuilder>(m, "YbusBuilder",
                             "Utility for constructing Ybus matrices.")
        .def(py::init<const PowerSystem&>(),
             py::arg("system"),
             "Create builder from a power system.")
        .def("build", &YbusBuilder::build,
             "Build and return the sparse Ybus matrix.")
        .def("getYbus", &YbusBuilder::getYbus,
             py::return_value_policy::reference,
             "Get Ybus after build() has been called.")
        .def("getBranchCount", &YbusBuilder::getBranchCount,
             "Number of branches (lines + transformers).")
        .def("addShuntContributions",
             &YbusBuilder::addShuntContributions,
             "Incorporate bus shunt admittances.");
}

/* ------------------------------------------------------------------ */
/*  LoadFlowSolver                                                     */
/* ------------------------------------------------------------------ */

void bind_load_flow_solver(py::module_& m) {
    py::class_<LoadFlowSolver>(m, "LoadFlowSolver",
                                "Power flow (load flow) solver engine.")
        .def(py::init<PowerSystem&>(),
             py::arg("system"),
             "Create solver bound to a power system.")
        /* -- Configuration -- */
        .def("setConfig", &LoadFlowSolver::setConfig,
             py::arg("config"),
             "Apply solver configuration.")
        .def("getConfig", &LoadFlowSolver::getConfig,
             "Current solver configuration.")
        /* -- Solution methods -- */
        .def("solve", &LoadFlowSolver::solve,
             py::arg("method") = SolverMethod::NewtonRaphson,
             "Solve power flow with chosen method.")
        .def("newtonRaphson", &LoadFlowSolver::newtonRaphson,
             "Full Newton-Raphson iteration.")
        .def("fastDecoupledFDXB", &LoadFlowSolver::fastDecoupledFDXB,
             "Fast-decoupled FDXB method.")
        .def("fastDecoupledFXB", &LoadFlowSolver::fastDecoupledFXB,
             "Fast-decoupled FXB method.")
        .def("gaussSeidel", &LoadFlowSolver::gaussSeidel,
             "Gauss-Seidel iteration.")
        /* -- Results -- */
        .def("lastResult", &LoadFlowSolver::lastResult,
             py::return_value_policy::reference,
             "Get the most recent power-flow result.")
        .def("getConvergenceHistory",
             &LoadFlowSolver::getConvergenceHistory,
             "Return mismatch history per iteration.")
        /* -- Utilities -- */
        .def("checkReactiveLimits",
             &LoadFlowSolver::checkReactiveLimits,
             "Enforce Q-limits and convert PV->PQ if needed.");
}

/* ------------------------------------------------------------------ */
/*  ShortCircuitSolver                                                 */
/* ------------------------------------------------------------------ */

void bind_short_circuit_solver(py::module_& m) {
    py::class_<ShortCircuitSolver>(m, "ShortCircuitSolver",
                                    "Short-circuit (fault) analysis.")
        .def(py::init<const PowerSystem&>(),
             py::arg("system"),
             "Create solver from a power system.")
        .def("solveSymmetrical",
             &ShortCircuitSolver::solveSymmetrical,
             py::arg("fault_bus_id"),
             py::arg("fault_impedance") = std::complex<double>(0.0, 0.0),
             "Three-phase symmetrical short-circuit calculation.")
        .def("solveUnsymmetrical",
             &ShortCircuitSolver::solveUnsymmetrical,
             py::arg("fault_bus_id"),
             py::arg("fault_type"),
             py::arg("fault_impedance") = std::complex<double>(0.0, 0.0),
             "Unsymmetrical fault analysis (SLG, LL, LLG).")
        .def("getFaultCurrent",
             &ShortCircuitSolver::getFaultCurrent,
             "Fault current at faulted bus [kA].")
        .def("getBusVoltagesDuringFault",
             &ShortCircuitSolver::getBusVoltagesDuringFault,
             "Voltage at all buses during fault [p.u.].")
        .def("getBranchCurrentsDuringFault",
             &ShortCircuitSolver::getBranchCurrentsDuringFault,
             "Currents in all branches during fault [kA].");
}

/* ------------------------------------------------------------------ */
/*  Module definition                                                  */
/* ------------------------------------------------------------------ */

PYBIND11_MODULE(powsy365_core, m) {
    m.doc() =
        R"doc(
        powsy365_core - C++ engine bindings for POWSYS365
        =================================================

        Python bindings for the high-performance C++ power system
        analysis engine.  Provides:

        * Network modeling (buses, lines, transformers, generators, loads)
        * Newton-Raphson / Fast-decoupled / Gauss-Seidel power flow
        * Symmetrical and unsymmetrical short-circuit analysis
        * Ybus matrix construction
        * IEEE 14 / 30 / 57 / 118 test cases

        Quick-start
        -----------
        >>> import powsy365_core as psc
        >>> ps = psc.PowerSystem()
        >>> ps.loadIEEE14()
        >>> solver = psc.LoadFlowSolver(ps)
        >>> result = solver.solve(psc.SolverMethod.NewtonRaphson)
        >>> print(result)
        <PowerFlowResult converged=True iterations=4 Ploss=13.4MW>
        )doc";

    m.attr("__version__") = "3.0.0";
    m.attr("__author__") = "POWSYS365 Team";

    /* Enums */
    bind_enums(m);

    /* Data structures */
    bind_bus(m);
    bind_line(m);
    bind_transformer(m);
    bind_generator(m);
    bind_load(m);
    bind_solver_config(m);

    /* Results */
    bind_results(m);

    /* Core classes */
    bind_power_system(m);
    bind_ybus_builder(m);
    bind_load_flow_solver(m);
    bind_short_circuit_solver(m);
}
