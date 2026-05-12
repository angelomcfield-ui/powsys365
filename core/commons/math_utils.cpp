// core/commons/math_utils.cpp

#include "math_utils.h"
#include <cmath>
#include <stdexcept>

namespace powsys365 {

double MathUtils::norm(const Complex& z) {
    return std::abs(z);
}

Complex MathUtils::conj(const Complex& z) {
    return std::conj(z);
}

Matrix MathUtils::multiply(const Matrix& a, const Matrix& b) {
    // Implementacion basica de multiplicacion de matrices
    size_t n = a.size();
    size_t m = b[0].size();
    size_t p = b.size();
    Matrix result(n, Vector(m, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < m; ++j) {
            for (size_t k = 0; k < p; ++k) {
                result[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    return result;
}

Vector MathUtils::solveLinearSystem(const Matrix& a, const Vector& b) {
    // Implementacion simple usando eliminacion gaussiana
    // Nota: Para produccion, usar Eigen o similar
    size_t n = a.size();
    Matrix aug = a;
    Vector res = b;
    for (size_t i = 0; i < n; ++i) {
        aug[i].push_back(res[i]);
    }
    // Eliminacion hacia adelante
    for (size_t i = 0; i < n; ++i) {
        // Encontrar pivote
        size_t max_row = i;
        for (size_t k = i + 1; k < n; ++k) {
            if (std::abs(aug[k][i]) > std::abs(aug[max_row][i])) {
                max_row = k;
            }
        }
        // Intercambiar filas
        std::swap(aug[i], aug[max_row]);
        // Eliminacion
        for (size_t k = i + 1; k < n; ++k) {
            double factor = aug[k][i] / aug[i][i];
            for (size_t j = i; j <= n; ++j) {
                aug[k][j] -= factor * aug[i][j];
            }
        }
    }
    // Sustitucion hacia atras
    Vector x(n);
    for (int i = n - 1; i >= 0; --i) {
        x[i] = aug[i][n];
        for (size_t j = i + 1; j < n; ++j) {
            x[i] -= aug[i][j] * x[j];
        }
        x[i] /= aug[i][i];
    }
    return x;
}

Complex MathUtils::polarToRect(double mag, double ang_deg) {
    double ang_rad = ang_deg * M_PI / 180.0;
    return Complex(mag * std::cos(ang_rad), mag * std::sin(ang_rad));
}

std::pair<double, double> MathUtils::rectToPolar(const Complex& z) {
    double mag = std::abs(z);
    double ang_deg = std::arg(z) * 180.0 / M_PI;
    return {mag, ang_deg};
}

} // namespace powsys365