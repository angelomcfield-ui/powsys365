#pragma once
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <Eigen/Dense>
#include <complex>

namespace powsys365 {

// Scalar type aliases
using Real = double;
using Complex = std::complex<Real>;
using uint = unsigned int;

// Dense matrix/vector types (real)
using DenseMatrix = Eigen::MatrixXd;
using DenseVector = Eigen::VectorXd;

// Dense matrix/vector types (complex)
using DenseMatrixC = Eigen::MatrixXcd;
using DenseVectorC = Eigen::VectorXcd;

// Sparse matrix types (real)
using SpMatrix = Eigen::SparseMatrix<Real>;
using SpVector = Eigen::SparseVector<Real>;

// Sparse matrix types (complex)
using SpMatrixC = Eigen::SparseMatrix<Complex>;
using SpVectorC = Eigen::SparseVector<Complex>;

// Triplet types for sparse matrix construction
using Triplet = Eigen::Triplet<Real>;
using TripletC = Eigen::Triplet<Complex>;

// Sparse LU solver types
using SpSolver = Eigen::SparseLU<SpMatrix>;
using SpSolverC = Eigen::SparseLU<SpMatrixC, Eigen::COLAMDOrdering<int>>;

// Map types for dense views
using DenseMap = Eigen::Map<DenseVector>;
using DenseMapC = Eigen::Map<DenseVectorC>;

} // namespace powsys365
