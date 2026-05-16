/**
 * @file bindings.cpp
 * @brief Python bindings for POWSYS365 C++ core engine using pybind11.
 *
 * Exposes power system modeling, load flow solvers, short-circuit
 * analysis, and network building utilities to Python 3.8+.
 *
 * Quick-start
 * -----------
 * >>> import powsy365_core as psc
 * >>> ps = psc.PowerSystem()
 * >>> ps.loadIEEE14()
 * >>> solver = psc.LoadFlowSolver(ps)
 * >>> config = psc.SolverConfig()
 * >>> config.method = psc.SolverMethod.NewtonRaphson
 * >>> result = solver.solve(config)
 * >>> print(result)
 * <PowerFlowResult converged=True iterations=4 Ploss=0.0523pu>
 * >>> for br in result.lineResults:
 * ...     print(f"Line {br.lineId}: loading={br.loading_pu*100:.1f}%")
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/eigen.h>
#include <pybind11/functional.h>
#include <pybind11/complex.h>

#include "core/commons/types.h"
#include "core/commons/math_utils.h"
#include "core/commons/constants.h"
#include "core/include/powsy365/power_system.h"
#include "core/include/powsy365/load_flow.h"
#include "core/include/powsy365/short_circuit.h"

namespace py = pybind11;
using namespace powsy365;

/* ------------------------------------------------------------------ */
/*  std::complex<double> registration                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Register std::complex<double> with pybind11 so it can be used
 *        as a field type in exported structs (e.g. Ybus elements, line
 *        flow results, etc.).
 */
void bind_complex(py::module_& m) {
    py::class_<std::complex<double>>(m, "Complex",
        "Complex number (double precision). Used for admittance, "
        "power and voltage phasors.")
        .def(py::init<>(),
             "Default construct 0 + j0.")
        .def(py::init<double, double>(),
             py::arg("real") = 0.0, py::arg("imag") = 0.0,
             "Construct from real and imaginary parts.")
        .def(py::init<double>(),
             py::arg("real"),
             "Construct from real part (imag = 0).")
        .def("real", py::overload_cast<>(&std::complex<double>::real, py::const_),
             "Return the real part.")
        .def("imag", py::overload_cast<>(&std::complex<double>::imag, py::const_),
             "Return the imaginary part.")
        .def_readwrite("re", &std::complex<double>::real,
             "Real component.")
        .def_readwrite("im", &std::complex<double>::imag,
             "Imaginary component.")
        .def("__repr__",
             [](const std::complex<double>& c) {
                 return "Complex(" + std::to_string(c.real()) + ", " +
                        std::to_string(c.imag()) + ")";
             })
        .def("__str__",
             [](const std::complex<double>& c) {
                 return std::to_string(c.real()) + "+j" +
                        std::to_string(c.imag());
             });
}

/* ------------------------------------------------------------------ */
/*  ConvergenceStatus enum                                            */
/* ------------------------------------------------------------------ */

void bind_convergence_status(py::module_& m) {
    py::enum_<ConvergenceStatus>(m, "ConvergenceStatus",
        "Enumeration of solver convergence outcomes.")
        .value("Converged", ConvergenceStatus::Converged,
               "Solution converged within tolerance.")
        .value("MaxIterationsReached", ConvergenceStatus::MaxIterationsReached,
               "Maximum number of iterations exceeded.")
        .value("Diverged", ConvergenceStatus::Diverged,
               "Solution diverged (mismatch increasing).")
        .value("SingularJacobian", ConvergenceStatus::SingularJacobian,
               "Jacobian matrix is singular.")
        .value("InvalidInput", ConvergenceStatus::InvalidInput,
               "Invalid power system data (e.g. no slack bus).");
}

/* ------------------------------------------------------------------ */
/*  BusType enum                                                      */
/* ------------------------------------------------------------------ */

void bind_bus_type(py::module_& m) {
    py::enum_<BusType>(m, "BusType",
        "Enumeration of electrical bus (node) types.")
        .value("PQ", BusType::PQ,
               "Load bus: active (P) and reactive (Q) power specified.")
        .value("PV", BusType::PV,
               "Generator bus: active power (P) and voltage magnitude (|V|) specified.")
        .value("Slack", BusType::Slack,
               "Slack / swing bus: voltage magnitude and angle specified.");
}

/* ------------------------------------------------------------------ */
/*  FaultType enum                                                    */
/* ------------------------------------------------------------------ */

void bind_fault_type(py::module_& m) {
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
}

/* ------------------------------------------------------------------ */
/*  SolverMethod enum                                                 */
/* ------------------------------------------------------------------ */

void bind_solver_method(py::module_& m) {
    py::enum_<SolverMethod>(m, "SolverMethod",
        "Enumeration of power flow (load flow) solution methods.")
        .value("NewtonRaphson", SolverMethod::NewtonRaphson,
               "Full Newton-Raphson: most accurate, robust convergence.")
        .value("FastDecoupled", SolverMethod::FastDecoupled,
               "Fast-decoupled BX / XB method: faster for large systems.")
        .value("GaussSeidel", SolverMethod::GaussSeidel,
               "Gauss-Seidel: legacy method, slow but simple.");
}

/* ------------------------------------------------------------------ */
/*  Bus struct                                                        */
/* ------------------------------------------------------------------ */

void bind_bus(py::module_& m) {
    py::class_<Bus>(m, "Bus",
        "Represents an electrical bus (node) in the power system network.")
        .def(py::init<int, std::string, BusType, double, double,
                      double, double, double, double, double,
                      double, double, double, double, double,
                      double, int, int>(),
             py::arg("id") = 0,
             py::arg("name") = "",
             py::arg("type") = BusType::PQ,
             py::arg("baseVoltage_kV") = 1.0,
             py::arg("vm_pu") = 1.0,
             py::arg("va_deg") = 0.0,
             py::arg("va_rad") = 0.0,
             py::arg("pg_pu") = 0.0,
             py::arg("qg_pu") = 0.0,
             py::arg("pl_pu") = 0.0,
             py::arg("ql_pu") = 0.0,
             py::arg("gsh_pu") = 0.0,
             py::arg("bsh_pu") = 0.0,
             py::arg("vmin_pu") = VMIN_DEFAULT,
             py::arg("vmax_pu") = VMAX_DEFAULT,
             py::arg("area") = 1,
             py::arg("zone") = 1,
             "Construct a Bus with all parameters.")
        .def_readwrite("id", &Bus::id,
             "Unique bus identifier (integer).")
        .def_readwrite("name", &Bus::name,
             "Human-readable bus name.")
        .def_readwrite("type", &Bus::type,
             "Bus type (PQ, PV, or Slack).")
        .def_readwrite("baseVoltage_kV", &Bus::baseVoltage_kV,
             "Base voltage level in kilovolts [kV].")
        .def_readwrite("vm_pu", &Bus::vm_pu,
             "Voltage magnitude in per-unit [p.u.].")
        .def_readwrite("va_deg", &Bus::va_deg,
             "Voltage angle in degrees [deg].")
        .def_readwrite("va_rad", &Bus::va_rad,
             "Voltage angle in radians [rad].")
        .def_readwrite("pg_pu", &Bus::pg_pu,
             "Active power generation at this bus [p.u.].")
        .def_readwrite("qg_pu", &Bus::qg_pu,
             "Reactive power generation at this bus [p.u.].")
        .def_readwrite("pl_pu", &Bus::pl_pu,
             "Active power load at this bus [p.u.].")
        .def_readwrite("ql_pu", &Bus::ql_pu,
             "Reactive power load at this bus [p.u.].")
        .def_readwrite("gsh_pu", &Bus::gsh_pu,
             "Shunt conductance G [p.u.].")
        .def_readwrite("bsh_pu", &Bus::bsh_pu,
             "Shunt susceptance B [p.u.].")
        .def_readwrite("vmin_pu", &Bus::vmin_pu,
             "Minimum voltage magnitude in per-unit [p.u.].")
        .def_readwrite("vmax_pu", &Bus::vmax_pu,
             "Maximum voltage magnitude in per-unit [p.u.].")
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
                        " baseVoltage_kV=" + std::to_string(b.baseVoltage_kV) +
                        " vm_pu=" + std::to_string(b.vm_pu) + ">";
             },
             "String representation of the bus.");
}

/* ------------------------------------------------------------------ */
/*  Line struct                                                       */
/* ------------------------------------------------------------------ */

void bind_line(py::module_& m) {
    py::class_<Line>(m, "Line",
        "Represents a transmission line connecting two buses.")
        .def(py::init<int, std::string, int, int, double, double,
                      double, double, double, double, double,
                      double, int>(),
             py::arg("id") = 0,
             py::arg("name") = "",
             py::arg("fromBus") = 0,
             py::arg("toBus") = 0,
             py::arg("r_pu") = 0.0,
             py::arg("x_pu") = 0.01,
             py::arg("bch_pu") = 0.0,
             py::arg("rateA_pu") = 0.0,
             py::arg("rateB_pu") = 0.0,
             py::arg("rateC_pu") = 0.0,
             py::arg("ratio") = 1.0,
             py::arg("angle_deg") = 0.0,
             py::arg("status") = 1,
             "Construct a transmission line.")
        .def_readwrite("id", &Line::id,
             "Line identifier.")
        .def_readwrite("name", &Line::name,
             "Human-readable line name.")
        .def_readwrite("fromBus", &Line::fromBus,
             "From bus ID.")
        .def_readwrite("toBus", &Line::toBus,
             "To bus ID.")
        .def_readwrite("r_pu", &Line::r_pu,
             "Series resistance [p.u.].")
        .def_readwrite("x_pu", &Line::x_pu,
             "Series reactance [p.u.].")
        .def_readwrite("bch_pu", &Line::bch_pu,
             "Total line charging susceptance [p.u.].")
        .def_readwrite("rateA_pu", &Line::rateA_pu,
             "Thermal rating A (normal operation) [p.u.].")
        .def_readwrite("rateB_pu", &Line::rateB_pu,
             "Thermal rating B (short-term) [p.u.].")
        .def_readwrite("rateC_pu", &Line::rateC_pu,
             "Thermal rating C (emergency) [p.u.].")
        .def_readwrite("ratio", &Line::ratio,
             "Off-nominal turns ratio (for in-line transformer).")
        .def_readwrite("angle_deg", &Line::angle_deg,
             "Phase shift angle [degrees].")
        .def_readwrite("status", &Line::status,
             "In-service flag (1 = in service, 0 = out of service).")
        .def("__repr__",
             [](const Line& l) {
                 return "<Line id=" + std::to_string(l.id) +
                        " name='" + l.name + "' from=" +
                        std::to_string(l.fromBus) + " to=" +
                        std::to_string(l.toBus) + " r_pu=" +
                        std::to_string(l.r_pu) + " x_pu=" +
                        std::to_string(l.x_pu) + ">";
             },
             "String representation of the line.");
}

/* ------------------------------------------------------------------ */
/*  Transformer struct                                                */
/* ------------------------------------------------------------------ */

void bind_transformer(py::module_& m) {
    py::class_<Transformer>(m, "Transformer",
        "Represents a two-winding power transformer.")
        .def(py::init<int, std::string, int, int, double, double,
                      double, double, double, double, double,
                      int, int>(),
             py::arg("id") = 0,
             py::arg("name") = "",
             py::arg("fromBus") = 0,
             py::arg("toBus") = 0,
             py::arg("r_pu") = 0.0,
             py::arg("x_pu") = 0.01,
             py::arg("ratio") = 1.0,
             py::arg("phaseShift_deg") = 0.0,
             py::arg("rateA_pu") = 0.0,
             py::arg("tapMin") = TAP_MIN,
             py::arg("tapMax") = TAP_MAX,
             py::arg("numTaps") = 33,
             py::arg("status") = 1,
             "Construct a two-winding transformer.")
        .def_readwrite("id", &Transformer::id,
             "Transformer identifier.")
        .def_readwrite("name", &Transformer::name,
             "Human-readable transformer name.")
        .def_readwrite("fromBus", &Transformer::fromBus,
             "From (primary) bus ID.")
        .def_readwrite("toBus", &Transformer::toBus,
             "To (secondary) bus ID.")
        .def_readwrite("r_pu", &Transformer::r_pu,
             "Series resistance [p.u.].")
        .def_readwrite("x_pu", &Transformer::x_pu,
             "Series reactance [p.u.].")
        .def_readwrite("ratio", &Transformer::ratio,
             "Off-nominal turns ratio.")
        .def_readwrite("phaseShift_deg", &Transformer::phaseShift_deg,
             "Phase shift angle [degrees].")
        .def_readwrite("rateA_pu", &Transformer::rateA_pu,
             "Rating A (normal operation) [p.u.].")
        .def_readwrite("tapMin", &Transformer::tapMin,
             "Minimum tap position.")
        .def_readwrite("tapMax", &Transformer::tapMax,
             "Maximum tap position.")
        .def_readwrite("numTaps", &Transformer::numTaps,
             "Number of tap positions.")
        .def_readwrite("status", &Transformer::status,
             "In-service flag (1 = in service, 0 = out of service).")
        .def("__repr__",
             [](const Transformer& t) {
                 return "<Transformer id=" + std::to_string(t.id) +
                        " name='" + t.name + "' from=" +
                        std::to_string(t.fromBus) + " to=" +
                        std::to_string(t.toBus) + " ratio=" +
                        std::to_string(t.ratio) + ">";
             },
             "String representation of the transformer.");
}

/* ------------------------------------------------------------------ */
/*  Generator struct                                                  */
/* ------------------------------------------------------------------ */

void bind_generator(py::module_& m) {
    py::class_<Generator>(m, "Generator",
        "Represents a synchronous generator unit.")
        .def(py::init<int, std::string, int, int, double, double,
                      double, double, double, double, double,
                      double, double, double, double, int>(),
             py::arg("id") = 0,
             py::arg("name") = "",
             py::arg("busId") = 0,
             py::arg("genType") = 0,
             py::arg("pg_pu") = 0.0,
             py::arg("qg_pu") = 0.0,
             py::arg("qmax_pu") = 9999.0,
             py::arg("qmin_pu") = -9999.0,
             py::arg("pgMax_pu") = 9999.0,
             py::arg("pgMin_pu") = 0.0,
             py::arg("vmSet_pu") = 1.0,
             py::arg("mbase_pu") = 100.0,
             py::arg("cost_c2") = 0.0,
             py::arg("cost_c1") = 0.0,
             py::arg("cost_c0") = 0.0,
             py::arg("status") = 1,
             "Construct a generator unit.")
        .def_readwrite("id", &Generator::id,
             "Generator identifier.")
        .def_readwrite("name", &Generator::name,
             "Human-readable generator name.")
        .def_readwrite("busId", &Generator::busId,
             "Connected bus ID.")
        .def_readwrite("genType", &Generator::genType,
             "Generator type code.")
        .def_readwrite("pg_pu", &Generator::pg_pu,
             "Active power output [p.u.].")
        .def_readwrite("qg_pu", &Generator::qg_pu,
             "Reactive power output [p.u.].")
        .def_readwrite("qmax_pu", &Generator::qmax_pu,
             "Maximum reactive power limit [p.u.].")
        .def_readwrite("qmin_pu", &Generator::qmin_pu,
             "Minimum reactive power limit [p.u.].")
        .def_readwrite("pgMax_pu", &Generator::pgMax_pu,
             "Maximum active power limit [p.u.].")
        .def_readwrite("pgMin_pu", &Generator::pgMin_pu,
             "Minimum active power limit [p.u.].")
        .def_readwrite("vmSet_pu", &Generator::vmSet_pu,
             "Voltage setpoint [p.u.].")
        .def_readwrite("mbase_pu", &Generator::mbase_pu,
             "Machine base MVA [MVA].")
        .def_readwrite("cost_c2", &Generator::cost_c2,
             "Quadratic cost coefficient [$/MW^2].")
        .def_readwrite("cost_c1", &Generator::cost_c1,
             "Linear cost coefficient [$/MW].")
        .def_readwrite("cost_c0", &Generator::cost_c0,
             "Constant cost coefficient [$].")
        .def_readwrite("status", &Generator::status,
             "In-service flag (1 = in service, 0 = out of service).")
        .def("__repr__",
             [](const Generator& g) {
                 return "<Generator id=" + std::to_string(g.id) +
                        " name='" + g.name + "' bus=" +
                        std::to_string(g.busId) + " pg_pu=" +
                        std::to_string(g.pg_pu) + ">";
             },
             "String representation of the generator.");
}

/* ------------------------------------------------------------------ */
/*  Load struct                                                       */
/* ------------------------------------------------------------------ */

void bind_load(py::module_& m) {
    py::class_<Load>(m, "Load",
        "Represents a power load (demand) connected to a bus.")
        .def(py::init<int, std::string, int, double, double, int, int>(),
             py::arg("id") = 0,
             py::arg("name") = "",
             py::arg("busId") = 0,
             py::arg("pl_pu") = 0.0,
             py::arg("ql_pu") = 0.0,
             py::arg("model") = 0,
             py::arg("status") = 1,
             "Construct a load.")
        .def_readwrite("id", &Load::id,
             "Load identifier.")
        .def_readwrite("name", &Load::name,
             "Human-readable load name.")
        .def_readwrite("busId", &Load::busId,
             "Connected bus ID.")
        .def_readwrite("pl_pu", &Load::pl_pu,
             "Active power demand [p.u.].")
        .def_readwrite("ql_pu", &Load::ql_pu,
             "Reactive power demand [p.u.].")
        .def_readwrite("model", &Load::model,
             "Load model type code (0 = constant power).")
        .def_readwrite("status", &Load::status,
             "In-service flag (1 = in service, 0 = out of service).")
        .def("__repr__",
             [](const Load& ld) {
                 return "<Load id=" + std::to_string(ld.id) +
                        " name='" + ld.name + "' bus=" +
                        std::to_string(ld.busId) + " pl_pu=" +
                        std::to_string(ld.pl_pu) + " ql_pu=" +
                        std::to_string(ld.ql_pu) + ">";
             },
             "String representation of the load.");
}

/* ------------------------------------------------------------------ */
/*  Shunt struct                                                      */
/* ------------------------------------------------------------------ */

void bind_shunt(py::module_& m) {
    py::class_<Shunt>(m, "Shunt",
        "Represents a shunt compensation element (capacitor/reactor).")
        .def(py::init<int, int, double, double, int>(),
             py::arg("id") = 0,
             py::arg("busId") = 0,
             py::arg("g_pu") = 0.0,
             py::arg("b_pu") = 0.0,
             py::arg("status") = 1,
             "Construct a shunt element.")
        .def_readwrite("id", &Shunt::id,
             "Shunt identifier.")
        .def_readwrite("busId", &Shunt::busId,
             "Connected bus ID.")
        .def_readwrite("g_pu", &Shunt::g_pu,
             "Shunt conductance G [p.u.].")
        .def_readwrite("b_pu", &Shunt::b_pu,
             "Shunt susceptance B [p.u.] (positive = capacitor).")
        .def_readwrite("status", &Shunt::status,
             "In-service flag (1 = in service, 0 = out of service).")
        .def("__repr__",
             [](const Shunt& s) {
                 return "<Shunt id=" + std::to_string(s.id) +
                        " bus=" + std::to_string(s.busId) +
                        " g_pu=" + std::to_string(s.g_pu) +
                        " b_pu=" + std::to_string(s.b_pu) + ">";
             },
             "String representation of the shunt.");
}

/* ------------------------------------------------------------------ */
/*  SolverConfig struct                                               */
/* ------------------------------------------------------------------ */

void bind_solver_config(py::module_& m) {
    py::class_<SolverConfig>(m, "SolverConfig",
        "Configuration parameters for load-flow solvers.")
        .def(py::init<>(),
             "Create solver config with sensible defaults.")
        .def_readwrite("method", &SolverConfig::method,
             "Solver method enumeration (default: NewtonRaphson).")
        .def_readwrite("tolerance", &SolverConfig::tolerance,
             "Mismatch tolerance (default: 1e-6).")
        .def_readwrite("maxIterations", &SolverConfig::maxIterations,
             "Maximum number of iterations (default: 30).")
        .def_readwrite("enforceQLimits", &SolverConfig::enforceQLimits,
             "Enforce generator Q-limits (PV -> PQ conversion).")
        .def_readwrite("flatStart", &SolverConfig::flatStart,
             "Use flat-start voltages (1.0 + j0.0 for all buses).")
        .def_readwrite("baseMVA", &SolverConfig::baseMVA,
             "System base MVA (default: 100.0).")
        .def_readwrite("verbose", &SolverConfig::verbose,
             "Print iteration details to stdout.")
        .def("__repr__",
             [](const SolverConfig& sc) {
                 return "<SolverConfig method=" +
                        std::to_string(static_cast<int>(sc.method)) +
                        " tol=" + std::to_string(sc.tolerance) +
                        " maxIter=" + std::to_string(sc.maxIterations) + ">";
             },
             "String representation of the solver config.");
}

/* ------------------------------------------------------------------ */
/*  PowerFlowBusResult struct                                         */
/* ------------------------------------------------------------------ */

void bind_power_flow_bus_result(py::module_& m) {
    py::class_<PowerFlowBusResult>(m, "PowerFlowBusResult",
        "Per-bus power-flow solution results.")
        .def(py::init<>())
        .def_readwrite("busId", &PowerFlowBusResult::busId,
             "Bus identifier.")
        .def_readwrite("vm_pu", &PowerFlowBusResult::vm_pu,
             "Voltage magnitude [p.u.].")
        .def_readwrite("va_deg", &PowerFlowBusResult::va_deg,
             "Voltage angle [degrees].")
        .def_readwrite("va_rad", &PowerFlowBusResult::va_rad,
             "Voltage angle [radians].")
        .def_readwrite("pg_pu", &PowerFlowBusResult::pg_pu,
             "Active power generation [p.u.].")
        .def_readwrite("qg_pu", &PowerFlowBusResult::qg_pu,
             "Reactive power generation [p.u.].")
        .def_readwrite("pl_pu", &PowerFlowBusResult::pl_pu,
             "Active power load [p.u.].")
        .def_readwrite("ql_pu", &PowerFlowBusResult::ql_pu,
             "Reactive power load [p.u.].")
        .def_readwrite("pInj_pu", &PowerFlowBusResult::pInj_pu,
             "Net active power injection [p.u.].")
        .def_readwrite("qInj_pu", &PowerFlowBusResult::qInj_pu,
             "Net reactive power injection [p.u.].")
        .def("__repr__",
             [](const PowerFlowBusResult& r) {
                 return "<PowerFlowBusResult bus=" +
                        std::to_string(r.busId) +
                        " vm_pu=" + std::to_string(r.vm_pu) +
                        " va_deg=" + std::to_string(r.va_deg) + ">";
             },
             "String representation of the bus result.");
}

/* ------------------------------------------------------------------ */
/*  PowerFlowLineResult struct                                        */
/* ------------------------------------------------------------------ */

void bind_power_flow_line_result(py::module_& m) {
    py::class_<PowerFlowLineResult>(m, "PowerFlowLineResult",
        "Per-branch (line or transformer) power-flow results.")
        .def(py::init<>())
        .def_readwrite("lineId", &PowerFlowLineResult::lineId,
             "Line / transformer identifier.")
        .def_readwrite("fromBus", &PowerFlowLineResult::fromBus,
             "From bus ID.")
        .def_readwrite("toBus", &PowerFlowLineResult::toBus,
             "To bus ID.")
        .def_readwrite("pFrom_pu", &PowerFlowLineResult::pFrom_pu,
             "Active power at from end [p.u.].")
        .def_readwrite("qFrom_pu", &PowerFlowLineResult::qFrom_pu,
             "Reactive power at from end [p.u.].")
        .def_readwrite("sFrom_pu", &PowerFlowLineResult::sFrom_pu,
             "Apparent power at from end [p.u.].")
        .def_readwrite("pTo_pu", &PowerFlowLineResult::pTo_pu,
             "Active power at to end [p.u.].")
        .def_readwrite("qTo_pu", &PowerFlowLineResult::qTo_pu,
             "Reactive power at to end [p.u.].")
        .def_readwrite("sTo_pu", &PowerFlowLineResult::sTo_pu,
             "Apparent power at to end [p.u.].")
        .def_readwrite("pLoss_pu", &PowerFlowLineResult::pLoss_pu,
             "Active power loss [p.u.].")
        .def_readwrite("qLoss_pu", &PowerFlowLineResult::qLoss_pu,
             "Reactive power loss [p.u.].")
        .def_readwrite("loading_pu", &PowerFlowLineResult::loading_pu,
             "Line loading as fraction of rateA (1.0 = 100%%).")
        .def("__repr__",
             [](const PowerFlowLineResult& r) {
                 return "<PowerFlowLineResult line=" +
                        std::to_string(r.lineId) +
                        " from=" + std::to_string(r.fromBus) +
                        " to=" + std::to_string(r.toBus) +
                        " loading_pu=" + std::to_string(r.loading_pu) + ">";
             },
             "String representation of the line result.");
}

/* ------------------------------------------------------------------ */
/*  SystemSummary struct                                              */
/* ------------------------------------------------------------------ */

void bind_system_summary(py::module_& m) {
    py::class_<SystemSummary>(m, "SystemSummary",
        "Aggregated system-level summary from a power-flow solution.")
        .def(py::init<>())
        .def_readwrite("totalPg_pu", &SystemSummary::totalPg_pu,
             "Total active power generation [p.u.].")
        .def_readwrite("totalPl_pu", &SystemSummary::totalPl_pu,
             "Total active power load [p.u.].")
        .def_readwrite("totalPloss_pu", &SystemSummary::totalPloss_pu,
             "Total active power losses [p.u.].")
        .def_readwrite("totalQloss_pu", &SystemSummary::totalQloss_pu,
             "Total reactive power losses [p.u.].")
        .def("__repr__",
             [](const SystemSummary& s) {
                 return "<SystemSummary Pgen=" +
                        std::to_string(s.totalPg_pu) +
                        " Pload=" + std::to_string(s.totalPl_pu) +
                        " Ploss=" + std::to_string(s.totalPloss_pu) + ">";
             },
             "String representation of the system summary.");
}

/* ------------------------------------------------------------------ */
/*  PowerFlowResult struct                                            */
/* ------------------------------------------------------------------ */

void bind_power_flow_result(py::module_& m) {
    py::class_<PowerFlowResult>(m, "PowerFlowResult",
        "Aggregated power-flow solution result.")
        .def(py::init<>())
        .def_readwrite("status", &PowerFlowResult::status,
             "Convergence status enumeration.")
        .def_readwrite("iterations", &PowerFlowResult::iterations,
             "Number of iterations performed.")
        .def_readwrite("finalMismatch", &PowerFlowResult::finalMismatch,
             "Final maximum mismatch value.")
        .def_readwrite("solveTime_ms", &PowerFlowResult::solveTime_ms,
             "Wall-clock solve time [milliseconds].")
        .def_readwrite("busResults", &PowerFlowResult::busResults,
             py::return_value_policy::reference,
             "List of per-bus results.")
        .def_readwrite("lineResults", &PowerFlowResult::lineResults,
             py::return_value_policy::reference,
             "List of per-branch results.")
        .def_readwrite("summary", &PowerFlowResult::summary,
             "Aggregated system summary.")
        .def_readwrite("message", &PowerFlowResult::message,
             "Human-readable status message.")
        .def("converged", &PowerFlowResult::converged,
             "Return True if the solver converged successfully.")
        .def("__repr__",
             [](const PowerFlowResult& r) {
                 return "<PowerFlowResult converged=" +
                        std::string(r.converged() ? "True" : "False") +
                        " iterations=" + std::to_string(r.iterations) +
                        " finalMismatch=" + std::to_string(r.finalMismatch) +
                        " time_ms=" + std::to_string(r.solveTime_ms) + ">";
             },
             "String representation of the power-flow result.");
}

/* ------------------------------------------------------------------ */
/*  ShortCircuitResult struct                                         */
/* ------------------------------------------------------------------ */

void bind_short_circuit_result(py::module_& m) {
    py::class_<ShortCircuitResult>(m, "ShortCircuitResult",
        "Short-circuit (fault) analysis results.")
        .def(py::init<>())
        .def_readwrite("faultBusId", &ShortCircuitResult::faultBusId,
             "Faulted bus identifier.")
        .def_readwrite("faultType", &ShortCircuitResult::faultType,
             "Type of fault applied.")
        .def_readwrite("faultCurrent_pu", &ShortCircuitResult::faultCurrent_pu,
             "Fault current in per-unit [p.u.].")
        .def_readwrite("faultCurrent_kA", &ShortCircuitResult::faultCurrent_kA,
             "Fault current in kiloamperes [kA].")
        .def_readwrite("busVoltages_pu", &ShortCircuitResult::busVoltages_pu,
             py::return_value_policy::reference,
             "Bus voltages during fault [p.u.].")
        .def_readwrite("branchCurrents_pu", &ShortCircuitResult::branchCurrents_pu,
             py::return_value_policy::reference,
             "Branch currents during fault [p.u.].")
        .def_readwrite("message", &ShortCircuitResult::message,
             "Human-readable status message.")
        .def("__repr__",
             [](const ShortCircuitResult& r) {
                 return "<ShortCircuitResult bus=" +
                        std::to_string(r.faultBusId) +
                        " type=" + std::to_string(static_cast<int>(r.faultType)) +
                        " If_pu=" + std::to_string(r.faultCurrent_pu) + ">";
             },
             "String representation of the short-circuit result.");
}

/* ------------------------------------------------------------------ */
/*  PowerSystem class                                                 */
/* ------------------------------------------------------------------ */

void bind_power_system(py::module_& m) {
    py::class_<PowerSystem>(m, "PowerSystem",
        "Container and manager for power system network data.\n\n"
        "Provides methods to build a network from scratch or load\n"
        "standard IEEE test cases (14, 30, 57, 118 buses).")
        .def(py::init<double>(),
             py::arg("baseMVA") = BASE_MVA_DEFAULT,
             "Create an empty power system model.")
        /* -- Bus management -- */
        .def("addBus", &PowerSystem::addBus,
             py::arg("bus"),
             "Add a bus to the system.")
        .def("getBuses", &PowerSystem::getBuses,
             py::return_value_policy::reference,
             "Return reference to the vector of all buses.")
        .def("numBuses", &PowerSystem::numBuses,
             "Number of buses in the system.")
        /* -- Line management -- */
        .def("addLine", &PowerSystem::addLine,
             py::arg("line"),
             "Add a transmission line.")
        .def("numLines", &PowerSystem::numLines,
             "Number of lines in the system.")
        /* -- Transformer management -- */
        .def("addTransformer", &PowerSystem::addTransformer,
             py::arg("transformer"),
             "Add a two-winding transformer.")
        /* -- Generator management -- */
        .def("addGenerator", &PowerSystem::addGenerator,
             py::arg("generator"),
             "Add a generator.")
        .def("numGenerators", &PowerSystem::numGenerators,
             "Number of generators in the system.")
        .def("getTotalPGen", &PowerSystem::getTotalPGen,
             "Sum of active generation [p.u.].")
        /* -- Load management -- */
        .def("addLoad", &PowerSystem::addLoad,
             py::arg("load"),
             "Add a load.")
        .def("getTotalPLoad", &PowerSystem::getTotalPLoad,
             "Sum of active load demand [p.u.].")
        /* -- Ybus construction -- */
        .def("buildYbus", &PowerSystem::buildYbus,
             "Build the bus admittance matrix Ybus from current network data.")
        .def("getYbus", &PowerSystem::getYbus,
             py::return_value_policy::reference,
             "Get the sparse Ybus matrix (reference).")
        /* -- Voltage initialization -- */
        .def("initializeVoltages", &PowerSystem::initializeVoltages,
             "Initialize voltage magnitudes and angles for all buses.")
        /* -- Validation -- */
        .def("isValid", &PowerSystem::isValid,
             "Check model consistency and data validity.")
        .def("checkVoltageLimits", &PowerSystem::checkVoltageLimits,
             "Return list of buses outside voltage limits.")
        .def("hasSlackBus", &PowerSystem::hasSlackBus,
             "Return True if the system has at least one slack bus.")
        .def("isConnected", &PowerSystem::isConnected,
             "Return True if the network graph is connected.")
        /* -- IEEE test cases -- */
        .def("loadIEEE14", &PowerSystem::loadIEEE14,
             "Populate the system with the IEEE 14-bus test case.")
        .def("loadIEEE30", &PowerSystem::loadIEEE30,
             "Populate the system with the IEEE 30-bus test case.")
        .def("loadIEEE57", &PowerSystem::loadIEEE57,
             "Populate the system with the IEEE 57-bus test case.")
        .def("loadIEEE118", &PowerSystem::loadIEEE118,
             "Populate the system with the IEEE 118-bus test case.")
        /* -- Base MVA -- */
        .def("getBaseMVA", &PowerSystem::getBaseMVA,
             "Get the system base MVA.")
        .def("setBaseMVA", &PowerSystem::setBaseMVA,
             py::arg("baseMVA"),
             "Set the system base MVA.")
        .def("clear", &PowerSystem::clear,
             "Clear all network data (buses, lines, transformers, etc.).")
        .def("__repr__",
             [](const PowerSystem& ps) {
                 return "<PowerSystem buses=" +
                        std::to_string(ps.numBuses()) +
                        " lines=" + std::to_string(ps.numLines()) +
                        " generators=" + std::to_string(ps.numGenerators()) +
                        " baseMVA=" + std::to_string(ps.getBaseMVA()) + ">";
             },
             "String representation of the power system.");
}

/* ------------------------------------------------------------------ */
/*  LoadFlowSolver class                                              */
/* ------------------------------------------------------------------ */

void bind_load_flow_solver(py::module_& m) {
    py::class_<LoadFlowSolver>(m, "LoadFlowSolver",
        "Power flow (load flow) solver engine.\n\n"
        "Supports Newton-Raphson, Fast-Decoupled (FDXB/BX) and\n"
        "Gauss-Seidel methods.")
        .def(py::init<PowerSystem&>(),
             py::arg("system"),
             "Create solver bound to a power system.")
        /* -- Main solve -- */
        .def("solve", &LoadFlowSolver::solve,
             py::arg("config"),
             "Solve power flow with the given configuration.\n\n"
             "Returns a PowerFlowResult containing convergence status,\n"
             "bus voltages, line flows, and system summary.")
        /* -- Individual methods -- */
        .def("newtonRaphson", &LoadFlowSolver::newtonRaphson,
             py::arg("config"),
             "Full Newton-Raphson iteration.")
        .def("fastDecoupledFDXB", &LoadFlowSolver::fastDecoupledFDXB,
             py::arg("config"),
             "Fast-decoupled FDXB method (form B' and B'' matrices).")
        .def("fastDecoupledFDBX", &LoadFlowSolver::fastDecoupledFDBX,
             py::arg("config"),
             "Fast-decoupled FDBX method (alternative formulation).")
        .def("gaussSeidel", &LoadFlowSolver::gaussSeidel,
             py::arg("config"),
             "Gauss-Seidel iteration.")
        /* -- Post-processing -- */
        .def("calculateLineFlows", &LoadFlowSolver::calculateLineFlows,
             py::arg("vm"), py::arg("va_rad"),
             "Calculate power flows on all lines for given voltages.\n\n"
             "Parameters\n"
             "----------\n"
             "vm : DenseVector\n"
             "    Voltage magnitude vector [p.u.].\n"
             "va_rad : DenseVector\n"
             "    Voltage angle vector [radians].\n\n"
             "Returns\n"
             "-------\n"
             "list[PowerFlowLineResult]\n"
             "    Per-branch active/reactive power flows and losses.")
        .def("calculateSystemSummary", &LoadFlowSolver::calculateSystemSummary,
             "Calculate aggregated system summary (total gen, load, losses).");
}

/* ------------------------------------------------------------------ */
/*  ShortCircuitSolver class                                          */
/* ------------------------------------------------------------------ */

void bind_short_circuit_solver(py::module_& m) {
    py::class_<ShortCircuitSolver>(m, "ShortCircuitSolver",
        "Short-circuit (fault) analysis solver.\n\n"
        "Supports symmetrical (three-phase) and unsymmetrical\n"
        "(single-line-to-ground, line-to-line, double-line-to-ground)\n"
        "fault calculations using symmetrical components.")
        .def(py::init<PowerSystem&>(),
             py::arg("system"),
             "Create solver from a power system.")
        .def("solveSymmetrical", &ShortCircuitSolver::solveSymmetrical,
             py::arg("faultBusId"),
             py::arg("faultImpedance") = std::complex<double>(0.0, 0.0),
             "Three-phase symmetrical short-circuit calculation.\n\n"
             "Parameters\n"
             "----------\n"
             "faultBusId : int\n"
             "    Bus ID where the fault is applied.\n"
             "faultImpedance : Complex\n"
             "    Fault impedance Zf = Rf + jXf (default 0+j0 for bolted fault).")
        .def("solveUnsymmetrical", &ShortCircuitSolver::solveUnsymmetrical,
             py::arg("faultBusId"),
             py::arg("faultType"),
             py::arg("faultImpedance") = std::complex<double>(0.0, 0.0),
             "Unsymmetrical fault analysis.\n\n"
             "Parameters\n"
             "----------\n"
             "faultBusId : int\n"
             "    Bus ID where the fault is applied.\n"
             "faultType : FaultType\n"
             "    Type of unsymmetrical fault (SinglePhase, TwoPhase, TwoPhaseG).\n"
             "faultImpedance : Complex\n"
             "    Fault impedance Zf = Rf + jXf (default 0+j0 for bolted fault).");
}

/* ------------------------------------------------------------------ */
/*  Module definition                                                 */
/* ------------------------------------------------------------------ */

PYBIND11_MODULE(powsy365_core, m) {
    m.doc() =
        R"doc(
        powsy365_core - High-performance C++ power system analysis engine
        ================================================================

        Python bindings for the POWSYS365 C++ core library providing:

        * **Network modeling**: buses, lines, transformers, generators, loads, shunts
        * **Power flow solvers**: Newton-Raphson, Fast-Decoupled (FDXB/BX), Gauss-Seidel
        * **Short-circuit analysis**: symmetrical and unsymmetrical fault calculations
        * **Ybus matrix**: automatic sparse admittance matrix construction
        * **IEEE test cases**: 14, 30, 57, and 118 bus systems ready to load

        All power quantities are expressed in per-unit (p.u.) unless otherwise noted.
        The system base MVA defaults to 100 MVA.

        Quick-start
        -----------
        >>> import powsy365_core as psc
        >>>
        >>> # Load IEEE 14-bus test case
        >>> ps = psc.PowerSystem()
        >>> ps.loadIEEE14()
        >>>
        >>> # Configure and run Newton-Raphson power flow
        >>> config = psc.SolverConfig()
        >>> config.method = psc.SolverMethod.NewtonRaphson
        >>> config.tolerance = 1e-6
        >>> config.maxIterations = 30
        >>> config.verbose = True
        >>>
        >>> solver = psc.LoadFlowSolver(ps)
        >>> result = solver.solve(config)
        >>>
        >>> if result.converged():
        ...     print(f"Converged in {result.iterations} iterations")
        ...     print(f"Solve time: {result.solveTime_ms:.2f} ms")
        ...     print(f"Final mismatch: {result.finalMismatch:.2e}")
        ...     for br in result.lineResults:
        ...         print(f"  Line {br.lineId}: loading={br.loading_pu*100:.1f}%")
        ...     summary = result.summary
        ...     print(f"Total generation: {summary.totalPg_pu:.4f} pu")
        ...     print(f"Total losses:     {summary.totalPloss_pu:.4f} pu")
        ... else:
        ...     print(f"Failed to converge: {result.message}")
        )doc";

    m.attr("__version__") = "3.0.0";
    m.attr("__author__")  = "POWSYS365 Team";

    /* Complex number type (must be bound before structs that use it) */
    bind_complex(m);

    /* Enums */
    bind_convergence_status(m);
    bind_bus_type(m);
    bind_fault_type(m);
    bind_solver_method(m);

    /* Data structures */
    bind_bus(m);
    bind_line(m);
    bind_transformer(m);
    bind_generator(m);
    bind_load(m);
    bind_shunt(m);
    bind_solver_config(m);

    /* Results */
    bind_power_flow_bus_result(m);
    bind_power_flow_line_result(m);
    bind_system_summary(m);
    bind_power_flow_result(m);
    bind_short_circuit_result(m);

    /* Core solver classes */
    bind_power_system(m);
    bind_load_flow_solver(m);
    bind_short_circuit_solver(m);
}
