#pragma once

#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <vector>
#include <string>
#include <complex>
#include <functional>
#include <map>
#include <set>
#include <memory>

namespace powsys365 {

/**
 * @brief Tipos de mediciones soportados por el estimador WLS.
 */
enum class MeasurementType {
    VOLTAGE_MAG,           ///< Magnitud de voltaje |V_i|
    VOLTAGE_ANGLE,         ///< Angulo de voltaje theta_i [rad]
    POWER_INJECTION_P,     ///< Inyeccion de potencia activa P_i
    POWER_INJECTION_Q,     ///< Inyeccion de potencia reactiva Q_i
    POWER_FLOW_P,          ///< Flujo de potencia activa P_ij
    POWER_FLOW_Q,          ///< Flujo de potencia reactiva Q_ij
    CURRENT_MAG,           ///< Magnitud de corriente |I_ij|
    PMU_VOLTAGE_MAG,       ///< Sincrofasorial: magnitud de voltaje
    PMU_VOLTAGE_ANGLE,     ///< Sincrofasorial: angulo de voltaje
    PMU_CURRENT_MAG,       ///< Sincrofasorial: magnitud de corriente
    PMU_CURRENT_ANGLE      ///< Sincrofasorial: angulo de corriente
};

/**
 * @brief Representa una medicion del sistema electrico.
 */
struct Measurement {
    int id = -1;                          ///< Identificador unico
    MeasurementType type;                 ///< Tipo de medicion
    double value = 0.0;                   ///< Valor medido z
    double sigma = 0.02;                  ///< Desviacion estandar (precision)
    double weight = 2500.0;               ///< Peso w = 1/sigma^2
    int bus_from = -1;                    ///< Barra origen (para flujos, corrientes)
    int bus_to = -1;                      ///< Barra destino
    bool is_pmu = false;                  ///< true si es medicion sincrofasorial
    bool is_pseudo = false;               ///< true si es pseudo-medicion
    double confidence_level = 0.95;       ///< Nivel de confianza
    std::string device_id;                ///< ID del dispositivo fisico
    std::chrono::system_clock::time_point timestamp; ///< Timestamp
};

/**
 * @brief Resultado de la estimacion de estado.
 */
struct StateEstimate {
    std::vector<double> voltage_magnitude;    ///< |V_i| en p.u.
    std::vector<double> voltage_angle;        ///< theta_i en radianes
    std::vector<std::complex<double>> complex_voltage; ///< V_i = |V_i| * e^(j*theta_i)
    int iterations = 0;                       ///< Numero de iteraciones
    bool converged = false;                   ///< Convergio?
    double max_residual = 0.0;                ///< Residual maximo
    double chi_square = 0.0;                  ///< Chi-cuadrado de los residuos
    double objective_value = 0.0;             ///< Funcion objetivo J(x)
    double convergence_tolerance = 1e-6;      ///< Tolerancia alcanzada
    double computation_time_ms = 0.0;         ///< Tiempo de computo [ms]
    std::vector<double> residuals;            ///< Vector de residuos r = z - h(x)
    std::vector<double> normalized_residuals; ///< Residuos normalizados r_n = r_i / sigma_i
    std::vector<int> bad_data_detected;       ///< IDs de mediciones con bad data
    std::vector<double> state_covariance;     ///< Varianza de los estados estimados
    int degrees_of_freedom = 0;               ///< Grados de libertad
    double condition_number = 0.0;            ///< Numero de condicion de la matriz ganancia
};

/**
 * @brief Parametros del estimador WLS.
 */
struct WLSParameters {
    int max_iterations = 20;              ///< Maximo de iteraciones
    double convergence_tolerance = 1e-6;  ///< Tolerancia de convergencia
    double chi_square_confidence = 0.95;  ///< Nivel de confianza chi-cuadrado
    double bad_data_threshold = 3.0;      ///< Umbral para bad data (sigma)
    bool use_sparse_lu = true;            ///< Usar sparse LU
    bool use_pivoting = true;             ///< Usar pivoting parcial
    bool detect_bad_data = true;          ///< Habilitar deteccion de bad data
    bool iterative_refinement = true;     ///< Refinamiento iterativo
    int max_bad_data_iterations = 5;      ///< Maximo de rondas de bad data removal
    double min_measurable_value = 1e-10;  ///< Valor minimo medible
    bool enable_pmu = true;               ///< Habilitar integracion PMU
    double pmu_weight_multiplier = 5.0;   ///< Multiplicador de peso para PMU
    bool use_flat_start = true;           ///< Usar flat start (1.0 p.u., 0 rad)
    bool normalize_jacobian = true;       ///< Normalizar Jacobiano
    int num_buses = 0;                    ///< Numero de barras del sistema
};

/**
 * @brief Datos de la topologia del sistema.
 */
struct SystemTopology {
    int num_buses = 0;                    ///< Numero de barras
    int num_branches = 0;                 ///< Numero de ramas
    std::vector<std::pair<int,int>> branches; ///< (from, to) para cada rama
    std::vector<std::complex<double>> y_series; ///< Admittancia serie Y_ij
    std::vector<std::complex<double>> y_shunt;  ///< Admittancia shunt Y_sh,i
    std::vector<int> slack_bus;           ///< Barras slack (referencia)
    std::vector<double> bus_base_kv;      ///< Tension base por barra [kV]
    std::vector<int> bus_types;           ///< 1=PQ, 2=PV, 3=slack
};

/**
 * @brief Estimador de Estado Weighted Least Squares (WLS).
 *
 * Implementa el estimador de estado clasico Gauss-Newton con:
 * - Matriz de pesos W = diag(1/sigma_i^2)
 * - Funcion de medicion h(x) no lineal
 * - Jacobiano H(x) = dh/dx
 * - Matriz ganancia G = H' * W * H
 * - Correccion dx = G^-1 * H' * W * (z - h(x))
 *
 * Resolucion mediante Sparse LU para sistemas de gran escala.
 * Integracion con mediciones sincrofasoriales (PMU).
 */
class StateEstimatorWLS {
public:
    StateEstimatorWLS();
    explicit StateEstimatorWLS(const WLSParameters& params);
    ~StateEstimatorWLS() = default;

    // Configuracion
    void setParameters(const WLSParameters& params);
    void setTopology(const SystemTopology& topology);
    void addMeasurement(const Measurement& measurement);
    void addMeasurements(const std::vector<Measurement>& measurements);
    void clearMeasurements();

    /**
     * @brief Estimacion de estado principal (Gauss-Newton iterativo).
     * @param initial_guess Estado inicial (opcional, puede ser vacio para flat start)
     * @return Resultado de la estimacion
     */
    StateEstimate estimate(const std::vector<double>& initial_guess = {});

    /**
     * @brief Funcion de medicion h(x): calcula valores esperados dado el estado.
     * @param voltage_magnitude |V| en p.u.
     * @param voltage_angle theta en radianes
     * @return Vector h(x) del mismo tamano que mediciones
     */
    std::vector<double> computeMeasurementFunction(
        const std::vector<double>& voltage_magnitude,
        const std::vector<double>& voltage_angle) const;

    /**
     * @brief Jacobiano H(x) = dh/dx.
     * @param voltage_magnitude |V| en p.u.
     * @param voltage_angle theta en radianes
     * @return Matriz Jacobiana sparse (m x 2n)
     */
    Eigen::SparseMatrix<double> computeJacobian(
        const std::vector<double>& voltage_magnitude,
        const std::vector<double>& voltage_angle) const;

    /**
     * @brief Matriz ganancia G = H' * R^-1 * H.
     * @param H Matriz Jacobiana
     * @return Matriz ganancia sparse
     */
    Eigen::SparseMatrix<double> computeGainMatrix(
        const Eigen::SparseMatrix<double>& H) const;

    /**
     * @brief Calcula la correccion del estado dx.
     * @param H Matriz Jacobiana
     * @param residual Vector de residuos (z - h(x))
     * @return Vector de correccion dx
     */
    Eigen::VectorXd computeCorrection(
        const Eigen::SparseMatrix<double>& H,
        const Eigen::VectorXd& residual) const;

    /**
     * @brief Detecta bad data usando test de residuos normalizados.
     * @param state Estado actual estimado
     * @param threshold Umbral en multiplos de sigma
     * @return Lista de IDs de mediciones sospechosas
     */
    std::vector<int> detectBadData(const StateEstimate& state, double threshold);

    /**
     * @brief Detecta bad data con iteraciones de eliminacion.
     * @return Estado corregido sin bad data
     */
    StateEstimate estimateWithBadDataRemoval();

    /**
     * @brief Obtiene residuos: r = z - h(x).
     */
    std::vector<double> getResiduals(
        const std::vector<double>& voltage_magnitude,
        const std::vector<double>& voltage_angle) const;

    /**
     * @brief Calcula estadistico chi-cuadrado.
     */
    double getChiSquare(
        const std::vector<double>& voltage_magnitude,
        const std::vector<double>& voltage_angle) const;

    /**
     * @brief Obtiene residuos normalizados: r_n = r_i / sqrt(R_ii).
     */
    std::vector<double> getNormalizedResiduals(
        const std::vector<double>& voltage_magnitude,
        const std::vector<double>& voltage_angle) const;

    // Acceso a datos
    const std::vector<Measurement>& getMeasurements() const { return measurements_; }
    int getNumBuses() const { return params_.num_buses; }
    int getNumMeasurements() const { return static_cast<int>(measurements_.size()); }
    const WLSParameters& getParameters() const { return params_; }

    /**
     * @brief Construye matriz de covarianza de mediciones R.
     */
    Eigen::SparseMatrix<double> buildMeasurementCovarianceMatrix() const;

    /**
     * @brief Construye matriz de pesos W = R^-1.
     */
    Eigen::SparseMatrix<double> buildWeightMatrix() const;

    /**
     * @brief Verifica observabilidad del sistema.
     * @return true si el sistema es observable
     */
    bool checkObservability() const;

    /**
     * @brief Identifica islas no observables.
     */
    std::vector<std::vector<int>> findUnobservableIslands() const;

    /**
     * @brief Pseudo-mediciones para barras no observables.
     */
    void addPseudoMeasurements();

    /**
     * @brief PMU: procesa mediciones sincrofasoriales.
     */
    void processPMUMeasurements();

    /**
     * @brief PMU: convierte mediciones rectangulares a polares.
     */
    std::pair<double, double> pmuRectangularToPolar(double re, double im) const;

    /**
     * @brief Actualiza la medicion de una barra especifica.
     */
    void updateMeasurement(int measurement_id, double new_value);

    /**
     * @brief Obtiene el estado anterior (para tracking).
     */
    StateEstimate getPreviousEstimate() const { return previous_estimate_; }

    /**
     * @brief Analisis de covarianza de los estados estimados.
     */
    std::vector<double> computeStateCovariance(
        const Eigen::SparseMatrix<double>& gain_matrix) const;

    /**
     * @brief Calcula el numero de condicion de la matriz ganancia.
     */
    double computeConditionNumber(const Eigen::SparseMatrix<double>& G) const;

    /**
     * @brief Reporte detallado de la estimacion.
     */
    std::string generateEstimationReport(const StateEstimate& estimate) const;

private:
    WLSParameters params_;
    SystemTopology topology_;
    std::vector<Measurement> measurements_;
    std::vector<double> initial_voltage_mag_;
    std::vector<double> initial_voltage_ang_;
    StateEstimate previous_estimate_;
    bool topology_set_ = false;

    // Funciones internas de calculo de flujo
    std::complex<double> getYBus(int i, int j) const;
    double computePInjection(int bus_i,
                              const std::vector<double>& vm,
                              const std::vector<double>& va) const;
    double computeQInjection(int bus_i,
                              const std::vector<double>& vm,
                              const std::vector<double>& va) const;
    double computePFlow(int from, int to,
                         const std::vector<double>& vm,
                         const std::vector<double>& va) const;
    double computeQFlow(int from, int to,
                         const std::vector<double>& vm,
                         const std::vector<double>& va) const;
    double computeCurrentMagnitude(int from, int to,
                                    const std::vector<double>& vm,
                                    const std::vector<double>& va) const;

    // Derivadas parciales para Jacobiano
    double dPi_dThi(int i, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dPi_dThj(int i, int j, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dPi_dVi(int i, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dPi_dVj(int i, int j, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dQi_dThi(int i, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dQi_dThj(int i, int j, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dQi_dVi(int i, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dQi_dVj(int i, int j, const std::vector<double>& vm,
                     const std::vector<double>& va) const;
    double dPij_dThi(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;
    double dPij_dThj(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;
    double dPij_dVi(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;
    double dPij_dVj(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;
    double dIij_dThi(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;
    double dIij_dVi(int i, int j, const std::vector<double>& vm,
                      const std::vector<double>& va) const;

    // Ybus construction
    Eigen::SparseMatrix<std::complex<double>> buildYBus() const;

    // Sparse LU solver
    Eigen::VectorXd solveSparseLU(const Eigen::SparseMatrix<double>& A,
                                   const Eigen::VectorXd& b) const;

    // Flat start initialization
    void initializeFlatStart();

    // Normalize state variables
    void normalizeState(std::vector<double>& vm, std::vector<double>& va) const;

    // PMU weight scaling
    void applyPMUWeightScaling();
};

} // namespace powsys365
