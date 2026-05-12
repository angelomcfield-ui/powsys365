#pragma once
#include <cmath>
#include <limits>

namespace powsys365 {

// Mathematical constants
constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;
constexpr double SQRT2 = 1.41421356237309504880;
constexpr double SQRT3 = 1.73205080756887729353;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

// Electrical system defaults
constexpr double BASE_MVA_DEFAULT = 100.0;
constexpr double FREQ_DEFAULT = 60.0;
constexpr double TOLERANCE_DEFAULT = 1e-6;
constexpr int MAX_ITERATIONS_DEFAULT = 30;

// Physical constants
constexpr double MU_0 = 4.0 * PI * 1e-7;           // Permeability of free space [H/m]
constexpr double EPSILON_0 = 8.854187817e-12;       // Permittivity of free space [F/m]
constexpr double C_LIGHT = 299792458.0;              // Speed of light [m/s]

// Convergence and solver settings
constexpr double CONVERGENCE_TOLERANCE_NM = 1e-6;     // Newton-Raphson tolerance
constexpr double CONVERGENCE_TOLERANCE_FD = 1e-5;     // Fast Decoupled tolerance
constexpr double CONVERGENCE_TOLERANCE_GS = 1e-6;     // Gauss-Seidel tolerance
constexpr int MAX_ITERATIONS_NR = 30;
constexpr int MAX_ITERATIONS_FD = 60;
constexpr int MAX_ITERATIONS_GS = 100;

// Voltage limits in per-unit
constexpr double VMIN_DEFAULT = 0.9;
constexpr double VMAX_DEFAULT = 1.1;
constexpr double VMIN_EMERGENCY = 0.85;
constexpr double VMAX_EMERGENCY = 1.15;

// Line loading limits
constexpr double LINE_LOADING_NORMAL = 1.0;           // 100% normal loading
constexpr double LINE_LOADING_EMERGENCY = 1.3;        // 130% emergency loading

// Transformer tap limits
constexpr double TAP_MIN = 0.9;
constexpr double TAP_MAX = 1.1;
constexpr double TAP_STEP = 0.00625;

// Generator reactive power limits margin
constexpr double Q_LIMIT_MARGIN_PU = 1e-4;

// Numerical safeguards
constexpr double MIN_VOLTAGE_PU = 0.01;               // Minimum acceptable voltage
constexpr double MAX_VOLTAGE_PU = 2.0;                // Maximum acceptable voltage
constexpr double ZERO_IMPEDANCE_THRESHOLD = 1e-12;    // Threshold for near-zero impedance
constexpr double MIN_X_R_RATIO = 0.001;               // Minimum X/R ratio for numerical stability

} // namespace powsys365
