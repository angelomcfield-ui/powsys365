// core/commons/math_utils.h

#ifndef POWSYS365_MATH_UTILS_H
#define POWSYS365_MATH_UTILS_H

#include <vector>
#include <complex>
#include "types.h"

namespace powsys365 {

class MathUtils {
public:
    static double norm(const Complex& z);
    static Complex conj(const Complex& z);
    static Matrix multiply(const Matrix& a, const Matrix& b);
    static Vector solveLinearSystem(const Matrix& a, const Vector& b);
    static Complex polarToRect(double mag, double ang_deg);
    static std::pair<double, double> rectToPolar(const Complex& z);
};

} // namespace powsys365

#endif // POWSYS365_MATH_UTILS_H