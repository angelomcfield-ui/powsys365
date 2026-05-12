#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include "../core/include/powsys365/power_system.h"
#include "../core/include/powsys365/load_flow.h"
#include "../core/commons/types.h"

namespace py = pybind11;
using namespace powsys365;

PYBIND11_MODULE(_core, m) {
    m.doc() = "POWSYS365 C++ core bindings";

    // Bus struct
    py::class_<Bus>(m, "Bus")
        .def(py::init<>())
        .def_readwrite("number", &Bus::number)
        .def_readwrite("name", &Bus::name)
        .def_readwrite("v_base_kv", &Bus::v_base_kv)
        .def_readwrite("vm_pu", &Bus::vm_pu)
        .def_readwrite("va_deg", &Bus::va_deg)
        .def_readwrite("type", &Bus::type);

    // Line struct
    py::class_<Line>(m, "Line")
        .def(py::init<>())
        .def_readwrite("from_bus", &Line::from_bus)
        .def_readwrite("to_bus", &Line::to_bus)
        .def_readwrite("z_pu", &Line::z_pu)
        .def_readwrite("b_pu", &Line::b_pu);

    // Generator struct
    py::class_<Generator>(m, "Generator")
        .def(py::init<>())
        .def_readwrite("bus", &Generator::bus)
        .def_readwrite("p_mw", &Generator::p_mw)
        .def_readwrite("q_mvar", &Generator::q_mvar)
        .def_readwrite("p_max", &Generator::p_max)
        .def_readwrite("p_min", &Generator::p_min)
        .def_readwrite("q_max", &Generator::q_max)
        .def_readwrite("q_min", &Generator::q_min);

    // Load struct
    py::class_<Load>(m, "Load")
        .def(py::init<>())
        .def_readwrite("bus", &Load::bus)
        .def_readwrite("p_mw", &Load::p_mw)
        .def_readwrite("q_mvar", &Load::q_mvar);

    // LoadFlowResult struct
    py::class_<LoadFlowResult>(m, "LoadFlowResult")
        .def_readonly("converged", &LoadFlowResult::converged)
        .def_readonly("iterations", &LoadFlowResult::iterations)
        .def_readonly("vm_pu", &LoadFlowResult::vm_pu)
        .def_readonly("va_deg", &LoadFlowResult::va_deg)
        .def_readonly("p_gen", &LoadFlowResult::p_gen)
        .def_readonly("q_gen", &LoadFlowResult::q_gen)
        .def_readonly("p_load", &LoadFlowResult::p_load)
        .def_readonly("q_load", &LoadFlowResult::q_load)
        .def_readonly("total_ploss", &LoadFlowResult::total_ploss)
        .def_readonly("total_qloss", &LoadFlowResult::total_qloss);

    // PowerSystem class
    py::class_<PowerSystem>(m, "PowerSystem")
        .def(py::init<double, double, double>(),
             py::arg("base_mva") = 100.0,
             py::arg("base_kv") = 69.0,
             py::arg("frequency") = 60.0)
        .def("add_bus", &PowerSystem::addBus)
        .def("add_line", &PowerSystem::addLine)
        .def("add_generator", &PowerSystem::addGenerator)
        .def("add_load", &PowerSystem::addLoad)
        .def("get_buses", &PowerSystem::getBuses)
        .def("get_lines", &PowerSystem::getLines)
        .def("get_generators", &PowerSystem::getGenerators)
        .def("get_loads", &PowerSystem::getLoads)
        .def("get_bus_index", &PowerSystem::getBusIndex)
        .def("get_base_mva", &PowerSystem::getBaseMVA)
        .def("get_base_kv", &PowerSystem::getBaseKV)
        .def("get_frequency", &PowerSystem::getFrequency)
        .def("get_num_buses", &PowerSystem::getNumBuses)
        .def("get_num_lines", &PowerSystem::getNumLines);

    // LoadFlowSolver class
    py::class_<LoadFlowSolver>(m, "LoadFlowSolver")
        .def(py::init<const PowerSystem&, double, int, const std::string&>(),
             py::arg("system"),
             py::arg("tolerance") = 1e-6,
             py::arg("max_iter") = 100,
             py::arg("method") = "NR")
        .def("solve", &LoadFlowSolver::solve);
}