#pragma once

#include <vector>
#include <string>
#include <random>
#include <functional>
#include <memory>
#include <chrono>
#include <map>

namespace powsys365 {

/**
 * @brief Tipos de componentes del sistema de potencia.
 */
enum class ComponentType {
    GENERATOR,
    TRANSMISSION_LINE,
    TRANSFORMER,
    CIRCUIT_BREAKER,
    BUS_BAR,
    LOAD,
    CAPACITOR_BANK,
    REACTOR,
    STATIC_VAR_COMPENSATOR,
    HVDC_CONVERTER,
    PROTECTION_RELAY,
    SHUNT_REACTOR
};

/**
 * @brief Modelo de componente para simulacion de confiabilidad.
 *
 * Cada componente tiene tasas de falla (lambda), tiempo medio de reparacion (MTTR),
 * tiempo medio entre fallas (MTBF), y parametros para modelado Markoviano.
 */
struct PowerComponent {
    std::string id;
    std::string name;
    ComponentType type;
    double capacity_mw = 0.0;           ///< Capacidad nominal [MW]
    double lambda_per_year = 0.0;       ///< Tasa de falla [fallas/año]
    double mttr_hours = 0.0;            ///< Mean Time To Repair [hours]
    double mtbf_hours = 0.0;            ///< Mean Time Between Failures [hours]
    double forced_outage_rate = 0.0;    ///< Forced Outage Rate = lambda*MTTR / (1 + lambda*MTTR)
    double scheduled_outage_rate = 0.0; ///< Tasa de mantenimiento programado
    double repair_cost_usd = 0.0;       ///< Costo promedio de reparacion [USD]
    double switching_time_hours = 0.0;  ///< Tiempo de conmutacion [hours]
    int num_redundant_units = 1;        ///< Numero de unidades redundantes (N)
    int min_operating_units = 1;        ///< Minimo de unidades requeridas (k en k-de-N)
    double derating_factor = 1.0;       ///< Factor de reduccion de capacidad [0-1]
    bool is_repairable = true;          ///< true si el componente es reparable
    double gamma_transitions = 0.0;     ///< Tasa de transicion a estado degradado
    double mu_repair = 0.0;             ///< Tasa de reparacion = 1/MTTR
    std::vector<std::string> dependent_components; ///< Componentes dependientes en cascada
};

/**
 * @brief Estado del sistema en un punto de tiempo.
 */
struct SystemState {
    double total_capacity_mw = 0.0;
    double available_capacity_mw = 0.0;
    double total_load_mw = 0.0;
    double load_loss_mw = 0.0;
    double energy_not_supplied_mwh = 0.0;
    bool is_load_loss = false;
    std::vector<std::string> failed_components;
    std::vector<std::string> degraded_components;
    double time_hour = 0.0;
    int year = 0;
};

/**
 * @brief Resultados de simulacion Monte Carlo.
 */
struct MonteCarloResults {
    // Loss of Load Probability
    double lolp = 0.0;                  ///< Probabilidad de perdida de carga [0-1]
    double lolp_std = 0.0;              ///< Desviacion estandar de LOLP

    // Loss of Load Expectation
    double lole_hours_per_year = 0.0;   ///< Horas de perdida de carga esperadas [h/año]
    double lole_std = 0.0;              ///< Desviacion estandar de LOLE

    // Expected Energy Not Supplied
    double eens_mwh_per_year = 0.0;     ///< Energia esperada no suministrada [MWh/año]
    double eens_std = 0.0;              ///< Desviacion estandar de EENS

    // Indices adicionales
    double edlc_hours_per_year = 0.0;   ///< Expected Duration of Load Curtailment
    double eflc_events_per_year = 0.0;  ///< Expected Frequency of Load Curtailment
    double edns_mw = 0.0;               ///< Expected Demand Not Supplied [MW]
    double siip = 0.0;                  ///< Severity Index (System Minutes) = EENS * 60 / peak_load
    double bulk_power_interruption_index = 0.0; ///< BPII
    double modified_bulk_energy_index = 0.0;    ///< MBEI

    // Estadisticas de convergencia
    double convergence_beta = 0.0;      ///< Coeficiente de variacion alcanzado
    int iterations_to_converge = 0;     ///< Iteraciones hasta convergencia
    bool converged = false;             ///< Si convergio

    // Histogramas
    std::map<double, int> load_loss_histogram;     ///< Histograma de MW perdidos
    std::map<int, int> annual_outage_histogram;    ///< Histograma de eventos/año

    // Intervalos de confianza 95%
    double lolp_ci_low = 0.0;
    double lolp_ci_high = 0.0;
    double lole_ci_low = 0.0;
    double lole_ci_high = 0.0;
    double eens_ci_low = 0.0;
    double eens_ci_high = 0.0;

    // Tiempo de simulacion
    double simulation_wall_time_seconds = 0.0;
    int total_iterations = 0;
};

/**
 * @brief Curva de duracion de carga (Load Duration Curve).
 */
struct LoadDurationCurve {
    std::vector<double> load_mw;        ///< Valores de carga ordenados descendentemente
    std::vector<double> duration_frac;  ///< Fraccion de tiempo [0-1]
    double peak_load_mw = 0.0;
    double min_load_mw = 0.0;
    double average_load_mw = 0.0;
    double total_energy_mwh = 0.0;
};

/**
 * @brief Parametros de simulacion Monte Carlo.
 */
struct SimulationParameters {
    int max_iterations = 100000;        ///< Maximo de iteraciones
    double convergence_threshold = 0.01; ///< Umbral de convergencia (coef. variacion)
    int min_iterations = 1000;          ///< Minimo de iteraciones
    int years_per_simulation = 1;       ///< Años simulados por iteracion
    int hours_per_year = 8760;          ///< Horas por año (8760 o 8784 bisiesto)
    double time_step_hours = 1.0;       ///< Paso de tiempo [hours]
    bool use_antithetic_variates = false; ///< Usar variables antiteticas
    bool use_importance_sampling = false; ///< Usar muestreo de importancia
    bool use_stratified_sampling = false; ///< Usar muestreo estratificado
    bool use_control_variates = false;    ///< Usar variables de control
    int random_seed = -1;               ///< Semilla (-1 = aleatoria)
    bool parallel_execution = true;     ///< Ejecutar en paralelo
    int num_threads = 4;                ///< Numero de hilos
    double peak_load_mw = 0.0;          ///< Carga pico del sistema [MW]
    bool sequential_simulation = true;  ///< true = secuencial, false = no-secuencial
};

/**
 * @brief Evento de transicion de estado.
 */
struct StateTransition {
    double transition_time;             ///< Tiempo de la transicion [hours]
    std::string component_id;           ///< Componente afectado
    std::string from_state;             ///< Estado anterior (UP, DOWN, DEGRADED)
    std::string to_state;               ///< Estado nuevo
    double capacity_impact_mw = 0.0;    ///< Impacto en capacidad [MW]
};

/**
 * @brief Simulador Monte Carlo para confiabilidad de sistemas de potencia.
 *
 * Implementa simulacion Monte Carlo secuencial y no-secuencial para:
 * - LOLP (Loss of Load Probability)
 * - LOLE (Loss of Load Expectation)
 * - EENS (Expected Energy Not Supplied)
 * - Indices derivados (EDLC, EFLC, EDNS, SIIP)
 *
 * Modelo de componentes basado en cadenas de Markov de 2 y 3 estados
 * con soporte para componentes reparables, redundancia k-de-N, y
 * fallos en cascada.
 */
class MonteCarloReliability {
public:
    MonteCarloReliability();
    ~MonteCarloReliability() = default;

    /**
     * @brief Agrega un componente al sistema.
     */
    void addComponent(const PowerComponent& component);

    /**
     * @brief Configura la curva de duracion de carga.
     */
    void setLoadDurationCurve(const LoadDurationCurve& ldc);

    /**
     * @brief Establece parametros de simulacion.
     */
    void setParameters(const SimulationParameters& params);

    /**
     * @brief Ejecuta la simulacion Monte Carlo completa.
     * @param n_iterations Numero de iteraciones (sobreescribe params.max_iterations)
     * @return Resultados completos de la simulacion
     */
    MonteCarloResults runSimulation(int n_iterations = -1);

    /**
     * @brief Simula fallas aleatorias para una iteracion.
     * @return Vector de transiciones de estado
     */
    std::vector<StateTransition> simulateOutages();

    /**
     * @brief Calcula LOLP: Loss of Load Probability.
     * @param results Resultados de simulacion previa
     * @return Probabilidad de perdida de carga [0-1]
     */
    double calculateLOLP(const MonteCarloResults& results) const;

    /**
     * @brief Calcula LOLE: Loss of Load Expectation.
     * @param results Resultados de simulacion previa
     * @return Horas de perdida de carga esperadas [h/año]
     */
    double calculateLOLE(const MonteCarloResults& results) const;

    /**
     * @brief Calcula EENS: Expected Energy Not Supplied.
     * @param results Resultados de simulacion previa
     * @return Energia esperada no suministrada [MWh/año]
     */
    double calculateEENS(const MonteCarloResults& results) const;

    /**
     * @brief Ejecuta simulacion secuencial Monte Carlo.
     *
     * Simula el comportamiento del sistema en el tiempo, considerando
     * transiciones de estado y reparaciones.
     */
    MonteCarloResults runSequentialSimulation(int n_iterations = -1);

    /**
     * @brief Ejecuta simulacion no-secuencial Monte Carlo.
     *
     * Calcula probabilidades por muestreo de estados sin evolucion temporal.
     */
    MonteCarloResults runNonSequentialSimulation(int n_iterations = -1);

    /**
     * @brief Genera una trayectoria de estados para un año.
     */
    std::vector<SystemState> generateAnnualTrajectory();

    /**
     * @brief Genera reporte detallado de resultados.
     */
    std::string generateReport(const MonteCarloResults& results) const;

    /**
     * @brief Exporta curva de duracion de carga.
     */
    LoadDurationCurve generateLoadDurationCurve(const std::vector<double>& hourly_loads) const;

    /**
     * @brief Analisis de sensibilidad: varia lambda de un componente.
     */
    struct SensitivityResult {
        std::string component_id;
        double lambda_multiplier;
        double lolp;
        double lole;
        double eens;
    };
    std::vector<SensitivityResult> runSensitivityAnalysis(
        const std::string& component_id,
        const std::vector<double>& lambda_multipliers);

    /**
     * @brief Calculo de indices compuestos.
     */
    double calculateEDLC(const MonteCarloResults& results) const;
    double calculateEFLC(const MonteCarloResults& results) const;
    double calculateEDNS(const MonteCarloResults& results) const;
    double calculateSIIP(const MonteCarloResults& results) const;

    /**
     * @brief Validacion del modelo de componentes.
     */
    bool validateModel() const;

    /**
     * @brief Obtiene componentes agregados.
     */
    const std::vector<PowerComponent>& getComponents() const { return components_; }

    /**
     * @brief Limpia todos los componentes.
     */
    void clearComponents() { components_.clear(); }

private:
    std::vector<PowerComponent> components_;
    LoadDurationCurve load_curve_;
    SimulationParameters params_;
    std::mt19937 rng_;  ///< Generador Mersenne Twister

    // Distribuciones
    std::uniform_real_distribution<double> uniform_dist_;
    std::exponential_distribution<double> exponential_dist_;
    std::weibull_distribution<double> weibull_dist_;
    std::lognormal_distribution<double> lognormal_dist_;

    // Metodos internos
    void initializeRandomGenerator();
    double sampleUniform();
    double sampleExponential(double lambda);
    double sampleWeibull(double shape, double scale);
    double sampleLognormal(double mu, double sigma);
    double sampleRepairTime(const PowerComponent& comp);
    double sampleTimeToFailure(const PowerComponent& comp);

    // Simulacion secuencial
    std::vector<StateTransition> simulateSequentialOutages(int year);
    SystemState evaluateSystemState(const std::vector<StateTransition&>& active_failures,
                                    double time_hour);
    double getLoadAtHour(int hour_of_year) const;
    bool checkLoadLoss(const SystemState& state) const;

    // Simulacion no-secuencial
    std::vector<bool> sampleComponentStatesNonSequential();
    SystemState evaluateNonSequentialState(const std::vector<bool>& states);

    // Redundancia k-de-N
    double evaluateKofNCapacity(const PowerComponent& comp,
                                  int num_failed) const;

    // Cascada
    std::vector<StateTransition> propagateCascadingFailures(
        const StateTransition& initial_failure);

    // Convergencia
    bool checkConvergence(const std::vector<double>& lolp_history,
                            const std::vector<double>& eens_history) const;

    // Intervalos de confianza
    double computeConfidenceIntervalLow(const std::vector<double>& samples) const;
    double computeConfidenceIntervalHigh(const std::vector<double>& samples) const;

    // Factor de carga horaria
    double computeLoadFactor(int hour) const;

    // Actualizar resultados parciales
    void updateResults(MonteCarloResults& results,
                        const std::vector<SystemState>& annual_states);

    // Calcular capacidad disponible dado componentes fallados
    double computeAvailableCapacity(const std::vector<std::string>& failed_ids) const;
};

} // namespace powsys365
