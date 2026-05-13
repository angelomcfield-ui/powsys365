#pragma once

#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <functional>

namespace powsys365 {

/**
 * @brief Estructura de datos de interrupcion (outage) para analisis de confiabilidad.
 *
 * Cada registro representa un evento de interrupcion en el sistema electrico,
 * con duracion, numero de clientes afectados, energia no suministrada,
 * y clasificacion del evento segun estandares IEEE 1366.
 */
struct OutageRecord {
    std::string event_id;                     ///< Identificador unico del evento
    std::string component_id;                 ///< Componente afectado (linea, transformador, etc.)
    std::string component_type;               ///< Tipo de componente
    std::chrono::system_clock::time_point start_time;  ///< Inicio de la interrupcion
    std::chrono::system_clock::time_point end_time;    ///< Fin de la interrupcion
    double duration_hours = 0.0;              ///< Duracion en horas (r_i)
    int customers_affected = 0;               ///< Numero de clientes afectados (N_i)
    double energy_not_supplied_mwh = 0.0;     ///< Energia no suministrada [MWh]
    double peak_demand_mw = 0.0;              ///< Demanda pico del circuito [MW]
    bool is_momentary = false;                ///< true si es interrupcion momentanea (< 5 min IEEE)
    bool is_scheduled = false;                ///< true si es mantenimiento programado
    std::string cause_code;                   ///< Codigo de causa (IEEE 1366)
    std::string weather_condition;            ///< Condicion meteorologica
    int feeder_id = 0;                        ///< Alimentador afectado
    double restoration_progress = 0.0;        ///< Progreso de restauracion [0-1]
};

/**
 * @brief Dataset completo de interrupciones para un periodo de analisis.
 */
struct OutageDataset {
    std::vector<OutageRecord> records;        ///< Registros de interrupciones
    int total_customers = 0;                  ///< Total de clientes del sistema (N_total)
    double total_peak_demand_mw = 0.0;        ///< Demanda pico total del sistema [MW]
    double total_energy_supplied_mwh = 0.0;   ///< Energia total suministrada en el periodo [MWh]
    double reporting_period_hours = 8760.0;   ///< Periodo de reporte en horas (default 1 año)
    std::string utility_name;                 ///< Nombre de la utility
    int reporting_year = 2024;                ///< Año de reporte
};

/**
 * @brief Resultados de metricas de confiabilidad segun IEEE 1366.
 */
struct ReliabilityMetrics {
    double saidi = 0.0;                       ///< System Average Interruption Duration Index [h/customer/año]
    double saifi = 0.0;                       ///< System Average Interruption Frequency Index [interrupciones/customer/año]
    double caidi = 0.0;                       ///< Customer Average Interruption Duration Index [h/interrupcion]
    double maifi = 0.0;                       ///< Momentary Average Interruption Frequency Index [momentarias/customer/año]
    double maifi_e = 0.0;                     ///< MAIFI with events equivalent
    double asai = 0.0;                        ///< Average Service Availability Index [0-1]
    double asidi = 0.0;                       ///< Average System Interruption Duration Index
    double asifi = 0.0;                       ///< Average System Interruption Frequency Index
    double cemsi = 0.0;                       ///< Customer Experienced Multiple Sustained Interruptions
    double cemsmi = 0.0;                      ///< Customer Experienced Multiple Sustained & Momentary Interruptions
    double ens = 0.0;                         ///< Energy Not Supplied [MWh/año]
    double aens = 0.0;                        ///< Average Energy Not Supplied [MWh/customer/año]
    double acci = 0.0;                        ///< Average Customer Curtailment Index
    double bpei = 0.0;                        ///< Bulk Power Energy Index
    double bpii = 0.0;                        ///< Bulk Power Interruption Index
    double mlpi = 0.0;                        ///< Mean Load Point Interruption Index
};

/**
 * @brief Benchmarks de confiabilidad por region/estandar.
 */
struct ReliabilityBenchmark {
    std::string region;                       ///< Region geografica
    std::string standard;                     ///< Estandar aplicable
    double saidi_target;                      ///< Objetivo SAIDI [h/customer/año]
    double saifi_target;                      ///< Objetivo SAIFI
    double caidi_target;                      ///< Objetivo CAIDI
    double asai_target;                       ///< Objetivo ASAI
    std::string data_source;                  ///< Fuente de datos del benchmark
    int benchmark_year = 2024;                ///< Año del benchmark
};

/**
 * @brief Resultado de comparacion con benchmarks.
 */
struct BenchmarkComparison {
    std::string benchmark_name;
    bool saidi_passed = false;
    bool saifi_passed = false;
    bool caidi_passed = false;
    bool asai_passed = false;
    double saidi_ratio = 0.0;
    double saifi_ratio = 0.0;
    double caidi_ratio = 0.0;
    double asai_ratio = 0.0;
    std::string overall_grade;                ///< A, B, C, D, F
};

/**
 * @brief Clasificacion IEEE 1366 de confiabilidad.
 */
enum class IEEE1366Class {
    Class1,   ///< Sistemas urbanos densos
    Class2,   ///< Sistemas suburbanos
    Class3,   ///< Sistemas rurales
    Class4,   ///< Sistemas muy rurales/remotos
    Unknown
};

/**
 * @brief Analizador de confiabilidad de sistemas electricos de potencia.
 *
 * Implementa las metricas estandar IEEE 1366 para evaluacion de confiabilidad:
 * SAIDI, SAIFI, CAIDI, MAIFI, ASAI, ENS y sus variantes.
 *
 * Las formulas implementadas son:
 *   SAIDI = sum(r_i * N_i) / N_total
 *   SAIFI = sum(N_i) / N_total
 *   CAIDI = SAIDI / SAIFI = sum(r_i * N_i) / sum(N_i)
 *   MAIFI = sum(momentary_events * N_i) / N_total
 *   ASAI = (N_total * T - sum(r_i * N_i)) / (N_total * T)
 *   ENS = sum(E_i) donde E_i es energia no suministrada por evento
 */
class ReliabilityAnalyzer {
public:
    ReliabilityAnalyzer();
    ~ReliabilityAnalyzer() = default;

    // Metricas principales IEEE 1366
    double calculateSAIDI(const OutageDataset& data) const;
    double calculateSAIFI(const OutageDataset& data) const;
    double calculateCAIDI(const OutageDataset& data) const;
    double calculateMAIFI(const OutageDataset& data) const;
    double calculateMAIFIE(const OutageDataset& data) const;
    double calculateASAI(const OutageDataset& data) const;
    double calculateASIDI(const OutageDataset& data) const;
    double calculateASIFI(const OutageDataset& data) const;
    double calculateENS(const OutageDataset& data) const;
    double calculateAENS(const OutageDataset& data) const;
    double calculateACCI(const OutageDataset& data) const;
    double calculateBPEI(const OutageDataset& data) const;
    double calculateBPII(const OutageDataset& data) const;
    double calculateMLPI(const OutageDataset& data) const;

    /**
     * @brief Calcula todas las metricas en una sola llamada.
     */
    ReliabilityMetrics calculateAllMetrics(const OutageDataset& data) const;

    /**
     * @brief Compara metricas calculadas contra benchmarks de la industria.
     */
    std::vector<BenchmarkComparison> checkBenchmarks(const ReliabilityMetrics& metrics) const;

    /**
     * @brief Agrega un benchmark personalizado para comparacion.
     */
    void addBenchmark(const ReliabilityBenchmark& benchmark);

    /**
     * @brief Carga benchmarks predefinidos por region.
     */
    void loadDefaultBenchmarks();

    /**
     * @brief Clasifica el sistema segun IEEE 1366.
     */
    IEEE1366Class classifySystem(double customers_per_km2) const;

    /**
     * @brief Genera reporte detallado en formato texto.
     */
    std::string generateReport(const OutageDataset& data,
                              const ReliabilityMetrics& metrics) const;

    /**
     * @brief Exporta metricas a formato JSON.
     */
    std::string exportToJSON(const OutageDataset& data,
                              const ReliabilityMetrics& metrics) const;

    /**
     * @brief Filtra registros por criterio.
     */
    std::vector<OutageRecord> filterByCause(const OutageDataset& data,
                                             const std::string& cause_code) const;
    std::vector<OutageRecord> filterByComponentType(const OutageDataset& data,
                                                      const std::string& comp_type) const;
    std::vector<OutageRecord> filterByDateRange(
        const OutageDataset& data,
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end) const;

    /**
     * @brief Analisis de tendencia: compara metricas entre periodos.
     */
    struct TrendAnalysis {
        double saidi_delta;
        double saifi_delta;
        double caidi_delta;
        double asai_delta;
        std::string trend_direction;  // "improving", "degrading", "stable"
    };
    TrendAnalysis compareWithPrevious(const ReliabilityMetrics& current,
                                       const ReliabilityMetrics& previous) const;

    /**
     * @brief Identifica peores circuitos/alimentadores.
     */
    struct FeederRanking {
        int feeder_id;
        double saidi_contribution;
        double saifi_contribution;
        int total_events;
        double total_customers_affected;
    };
    std::vector<FeederRanking> rankFeeders(const OutageDataset& data,
                                            int top_n = 10) const;

    /**
     * @brief Estimacion de mejora post-inversion.
     */
    struct ImprovementScenario {
        std::string description;
        double estimated_saidi_reduction;
        double estimated_saifi_reduction;
        double investment_cost_usd;
        double benefit_cost_ratio;
    };
    std::vector<ImprovementScenario> simulateImprovements(
        const OutageDataset& data,
        const ReliabilityMetrics& baseline) const;

private:
    std::vector<ReliabilityBenchmark> benchmarks_;

    // Validacion de datos
    bool validateDataset(const OutageDataset& data) const;

    // Utilidades
    double hoursBetween(const std::chrono::system_clock::time_point& start,
                         const std::chrono::system_clock::time_point& end) const;

    // Clasificacion de calificacion general
    std::string computeOverallGrade(const BenchmarkComparison& comp) const;
};

} // namespace powsys365
