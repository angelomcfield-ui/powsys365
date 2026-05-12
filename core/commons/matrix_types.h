// core/commons/matrix_types.h

#ifndef POWSYS365_MATRIX_TYPES_H
#define POWSYS365_MATRIX_TYPES_H

#include <vector>
#include <complex>

namespace powsys365 {

using RealMatrix = std::vector<std::vector<double>>;
using ComplexMatrix = std::vector<std::vector<std::complex<double>>>;
using SparseMatrix = std::vector<std::vector<std::pair<size_t, double>>>; // Lista de adyacencia para matrices dispersas

} // namespace powsys365

#endif // POWSYS365_MATRIX_TYPES_H