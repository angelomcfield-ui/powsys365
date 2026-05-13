#include "reliability_analyzer.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>

namespace powsys365 {

ReliabilityAnalyzer::ReliabilityAnalyzer()
{
    loadDefaultBenchmarks();
}

bool ReliabilityAnalyzer::validateDataset(const OutageDataset& data) const
{
    if (data.total_customers <= 0) return false;
    if (data.reporting_period_hours <= 0) return false;
    return true;
}

double ReliabilityAnalyzer::hoursBetween(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end) const
{
    auto diff = end - start;
    return std::chrono::duration<double>(diff).count() / 3600.0;
}

double ReliabilityAnalyzer::calculateSAIDI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double customer_minutes = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;  // SAIDI solo considera interrupciones sostenidas
        double dur = rec.duration_hours;
        if (dur <= 0.0 && rec.start_time != rec.end_time) {
            dur = hoursBetween(rec.start_time, rec.end_time);
        }
        customer_minutes += dur * rec.customers_affected;
    }

    return customer_minutes / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateSAIFI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double total_interruptions = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;  // SAIFI solo considera interrupciones sostenidas
        total_interruptions += rec.customers_affected;
    }

    return total_interruptions / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateCAIDI(const OutageDataset& data) const
{
    double saidi = calculateSAIDI(data);
    double saifi = calculateSAIFI(data);

    if (saifi <= 0.0) return 0.0;

    return saidi / saifi;
}

double ReliabilityAnalyzer::calculateMAIFI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double momentary_events = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) {
            momentary_events += rec.customers_affected;
        }
    }

    return momentary_events / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateMAIFIE(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double total_events = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) {
            double dur = rec.duration_hours;
            if (dur > 0.0) {
                total_events += (dur / (5.0 / 60.0)) * rec.customers_affected;
            } else {
                total_events += rec.customers_affected;
            }
        }
    }

    return total_events / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateASAI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double customer_hours_interrupted = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;
        double dur = rec.duration_hours;
        if (dur <= 0.0 && rec.start_time != rec.end_time) {
            dur = hoursBetween(rec.start_time, rec.end_time);
        }
        customer_hours_interrupted += dur * rec.customers_affected;
    }

    double total_customer_hours = static_cast<double>(data.total_customers) *
                                   data.reporting_period_hours;

    if (total_customer_hours <= 0.0) return 1.0;

    return (total_customer_hours - customer_hours_interrupted) / total_customer_hours;
}

double ReliabilityAnalyzer::calculateASIDI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;
    if (data.total_peak_demand_mw <= 0.0) return 0.0;

    double demand_hours_interrupted = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;
        double dur = rec.duration_hours;
        if (dur <= 0.0 && rec.start_time != rec.end_time) {
            dur = hoursBetween(rec.start_time, rec.end_time);
        }
        demand_hours_interrupted += dur * rec.peak_demand_mw;
    }

    return demand_hours_interrupted / data.total_peak_demand_mw;
}

double ReliabilityAnalyzer::calculateASIFI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;
    if (data.total_peak_demand_mw <= 0.0) return 0.0;

    double total_demand_interrupted = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;
        total_demand_interrupted += rec.peak_demand_mw;
    }

    return total_demand_interrupted / data.total_peak_demand_mw;
}

double ReliabilityAnalyzer::calculateENS(const OutageDataset& data) const
{
    double total_ens = 0.0;
    for (const auto& rec : data.records) {
        total_ens += rec.energy_not_supplied_mwh;
    }
    return total_ens;
}

double ReliabilityAnalyzer::calculateAENS(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;
    return calculateENS(data) / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateACCI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double total_curtailed_energy = 0.0;
    for (const auto& rec : data.records) {
        total_curtailed_energy += rec.energy_not_supplied_mwh;
    }

    return total_curtailed_energy / static_cast<double>(data.total_customers);
}

double ReliabilityAnalyzer::calculateBPEI(const OutageDataset& data) const
{
    if (data.total_energy_supplied_mwh <= 0.0) return 0.0;
    return calculateENS(data) / data.total_energy_supplied_mwh;
}

double ReliabilityAnalyzer::calculateBPII(const OutageDataset& data) const
{
    if (data.total_peak_demand_mw <= 0.0) return 0.0;

    double total_demand_interrupted = 0.0;
    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;
        total_demand_interrupted += rec.peak_demand_mw;
    }

    return total_demand_interrupted / data.total_peak_demand_mw;
}

double ReliabilityAnalyzer::calculateMLPI(const OutageDataset& data) const
{
    if (!validateDataset(data)) return 0.0;

    double total_interruption_rate = 0.0;
    std::map<int, std::vector<double>> feeder_durations;

    for (const auto& rec : data.records) {
        if (rec.is_momentary) continue;
        double dur = rec.duration_hours;
        if (dur <= 0.0 && rec.start_time != rec.end_time) {
            dur = hoursBetween(rec.start_time, rec.end_time);
        }
        feeder_durations[rec.feeder_id].push_back(dur);
    }

    for (const auto& [feeder_id, durations] : feeder_durations) {
        double avg_duration = std::accumulate(durations.begin(), durations.end(), 0.0)
                              / static_cast<double>(durations.size());
        total_interruption_rate += avg_duration;
    }

    if (feeder_durations.empty()) return 0.0;
    return total_interruption_rate / static_cast<double>(feeder_durations.size());
}

ReliabilityMetrics ReliabilityAnalyzer::calculateAllMetrics(const OutageDataset& data) const
{
    ReliabilityMetrics metrics;
    metrics.saidi   = calculateSAIDI(data);
    metrics.saifi   = calculateSAIFI(data);
    metrics.caidi   = calculateCAIDI(data);
    metrics.maifi   = calculateMAIFI(data);
    metrics.maifi_e = calculateMAIFIE(data);
    metrics.asai    = calculateASAI(data);
    metrics.asidi   = calculateASIDI(data);
    metrics.asifi   = calculateASIFI(data);
    metrics.ens     = calculateENS(data);
    metrics.aens    = calculateAENS(data);
    metrics.acci    = calculateACCI(data);
    metrics.bpei    = calculateBPEI(data);
    metrics.bpii    = calculateBPII(data);
    metrics.mlpi    = calculateMLPI(data);
    return metrics;
}

void ReliabilityAnalyzer::addBenchmark(const ReliabilityBenchmark& benchmark)
{
    benchmarks_.push_back(benchmark);
}

void ReliabilityAnalyzer::loadDefaultBenchmarks()
{
    benchmarks_.clear();

    benchmarks_.push_back({
        "North America (Urban)", "IEEE 1366 / EIA",
        1.5, 1.0, 1.5, 0.99983,
        "EIA Electric Power Annual / IEEE 1366", 2024
    });
    benchmarks_.push_back({
        "North America (Suburban)", "IEEE 1366 / EIA",
        2.5, 1.2, 2.1, 0.99971,
        "EIA Electric Power Annual / IEEE 1366", 2024
    });
    benchmarks_.push_back({
        "North America (Rural)", "IEEE 1366 / EIA",
        4.0, 2.0, 2.0, 0.99954,
        "EIA Electric Power Annual / IEEE 1366", 2024
    });
    benchmarks_.push_back({
        "Europe (Urban)", "CEER / IEEE 1366",
        0.8, 0.5, 1.6, 0.99991,
        "CEER Benchmarking Report", 2024
    });
    benchmarks_.push_back({
        "Europe (Average)", "CEER / IEEE 1366",
        1.2, 0.7, 1.7, 0.99986,
        "CEER Benchmarking Report", 2024
    });
    benchmarks_.push_back({
        "Latin America (Urban)", "IEEE 1366 / OLADE",
        6.0, 3.0, 2.0, 0.99931,
        "OLADE / World Bank", 2024
    });
    benchmarks_.push_back({
        "Latin America (Rural)", "IEEE 1366 / OLADE",
        12.0, 5.0, 2.4, 0.99863,
        "OLADE / World Bank", 2024
    });
    benchmarks_.push_back({
        "Asia (Developed)", "IEEE 1366 / APEC",
        0.5, 0.3, 1.7, 0.99994,
        "APEC / IEA Statistics", 2024
    });
    benchmarks_.push_back({
        "Asia (Developing)", "IEEE 1366 / APEC",
        8.0, 4.0, 2.0, 0.99909,
        "APEC / IEA Statistics", 2024
    });
    benchmarks_.push_back({
        "World Bank Best Practice", "World Bank",
        1.0, 0.8, 1.25, 0.99989,
        "World Bank Energy Practice", 2024
    });
}

std::string ReliabilityAnalyzer::computeOverallGrade(const BenchmarkComparison& comp) const
{
    int pass_count = 0;
    if (comp.saidi_passed) pass_count++;
    if (comp.saifi_passed) pass_count++;
    if (comp.caidi_passed) pass_count++;
    if (comp.asai_passed) pass_count++;

    switch (pass_count) {
        case 4: return "A";
        case 3: return "B";
        case 2: return "C";
        case 1: return "D";
        default: return "F";
    }
}

std::vector<BenchmarkComparison> ReliabilityAnalyzer::checkBenchmarks(
    const ReliabilityMetrics& metrics) const
{
    std::vector<BenchmarkComparison> results;

    for (const auto& bench : benchmarks_) {
        BenchmarkComparison comp;
        comp.benchmark_name = bench.region + " (" + bench.standard + ")";

        comp.saidi_ratio = (bench.saidi_target > 0.0) ? metrics.saidi / bench.saidi_target : 0.0;
        comp.saifi_ratio = (bench.saifi_target > 0.0) ? metrics.saifi / bench.saifi_target : 0.0;
        comp.caidi_ratio = (bench.caidi_target > 0.0) ? metrics.caidi / bench.caidi_target : 0.0;
        comp.asai_ratio  = (bench.asai_target > 0.0)  ? metrics.asai / bench.asai_target : 0.0;

        comp.saidi_passed = metrics.saidi <= bench.saidi_target;
        comp.saifi_passed = metrics.saifi <= bench.saifi_target;
        comp.caidi_passed = metrics.caidi <= bench.caidi_target;
        comp.asai_passed  = metrics.asai >= bench.asai_target;

        comp.overall_grade = computeOverallGrade(comp);
        results.push_back(comp);
    }

    return results;
}

IEEE1366Class ReliabilityAnalyzer::classifySystem(double customers_per_km2) const
{
    if (customers_per_km2 >= 1500.0) return IEEE1366Class::Class1;
    if (customers_per_km2 >= 500.0)  return IEEE1366Class::Class2;
    if (customers_per_km2 >= 50.0)   return IEEE1366Class::Class3;
    if (customers_per_km2 > 0.0)     return IEEE1366Class::Class4;
    return IEEE1366Class::Unknown;
}

std::string ReliabilityAnalyzer::generateReport(const OutageDataset& data,
                                                  const ReliabilityMetrics& metrics) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    oss << "==================================================================\n";
    oss << "  POWSYS365 - Reliability Analysis Report\n";
    oss << "  Utility: " << data.utility_name << " | Year: " << data.reporting_year << "\n";
    oss << "  Total Customers: " << data.total_customers << "\n";
    oss << "  Total Events: " << data.records.size() << "\n";
    oss << "==================================================================\n\n";

    oss << "--- Core IEEE 1366 Metrics ---\n";
    oss << "SAIDI  : " << std::setw(10) << metrics.saidi  << "  h/customer/year\n";
    oss << "SAIFI  : " << std::setw(10) << metrics.saifi  << "  interruptions/customer/year\n";
    oss << "CAIDI  : " << std::setw(10) << metrics.caidi  << "  h/interruption\n";
    oss << "MAIFI  : " << std::setw(10) << metrics.maifi  << "  momentaries/customer/year\n";
    oss << "MAIFI-E: " << std::setw(10) << metrics.maifi_e << "  equiv. momentaries/customer/year\n";
    oss << "ASAI   : " << std::setw(10) << metrics.asali  << "  (" << metrics.asai * 100.0 << " % availability)\n";
    oss << "\n";

    oss << "--- Extended Metrics ---\n";
    oss << "ENS    : " << std::setw(10) << metrics.ens    << "  MWh/year\n";
    oss << "AENS   : " << std::setw(10) << metrics.aens   << "  MWh/customer/year\n";
    oss << "ACCI   : " << std::setw(10) << metrics.acci   << "  MWh/customer/year\n";
    oss << "ASIDI  : " << std::setw(10) << metrics.asidi  << "  h/load-point/year\n";
    oss << "ASIFI  : " << std::setw(10) << metrics.asifi  << "  interruptions/load-point/year\n";
    oss << "BPEI   : " << std::setw(10) << metrics.bpei   << "  (fraction)\n";
    oss << "BPII   : " << std::setw(10) << metrics.bpii   << "  (fraction)\n";
    oss << "MLPI   : " << std::setw(10) << metrics.mlpi   << "  h/feeder/event\n";
    oss << "\n";

    oss << "--- Benchmark Comparison ---\n";
    auto comparisons = checkBenchmarks(metrics);
    for (const auto& comp : comparisons) {
        oss << comp.benchmark_name << " => Grade: " << comp.overall_grade << "\n";
        oss << "  SAIDI ratio: " << comp.saidi_ratio << " [" << (comp.saidi_passed ? "PASS" : "FAIL") << "]\n";
        oss << "  SAIFI ratio: " << comp.saifi_ratio << " [" << (comp.saifi_passed ? "PASS" : "FAIL") << "]\n";
        oss << "  ASAI  ratio: " << comp.asai_ratio  << " [" << (comp.asai_passed  ? "PASS" : "FAIL") << "]\n";
    }

    return oss.str();
}

std::string ReliabilityAnalyzer::exportToJSON(const OutageDataset& data,
                                                const ReliabilityMetrics& metrics) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    oss << "{\n";
    oss << "  \"utility\": \"" << data.utility_name << "\",\n";
    oss << "  \"year\": " << data.reporting_year << ",\n";
    oss << "  \"total_customers\": " << data.total_customers << ",\n";
    oss << "  \"total_events\": " << data.records.size() << ",\n";
    oss << "  \"metrics\": {\n";
    oss << "    \"SAIDI\": "  << metrics.saidi   << ",\n";
    oss << "    \"SAIFI\": "  << metrics.saifi   << ",\n";
    oss << "    \"CAIDI\": "  << metrics.caidi   << ",\n";
    oss << "    \"MAIFI\": "  << metrics.maifi   << ",\n";
    oss << "    \"MAIFI_E\": " << metrics.maifi_e << ",\n";
    oss << "    \"ASAI\": "   << metrics.asali   << ",\n";
    oss << "    \"ENS\": "    << metrics.ens     << ",\n";
    oss << "    \"AENS\": "   << metrics.aens    << ",\n";
    oss << "    \"ACCI\": "   << metrics.acci    << ",\n";
    oss << "    \"BPEI\": "   << metrics.bpei    << ",\n";
    oss << "    \"ASIDI\": "  << metrics.asidi   << ",\n";
    oss << "    \"ASIFI\": "  << metrics.asifi   << "\n";
    oss << "  },\n";
    oss << "  \"benchmarks\": [\n";

    auto comparisons = checkBenchmarks(metrics);
    for (size_t i = 0; i < comparisons.size(); ++i) {
        const auto& comp = comparisons[i];
        oss << "    {\n";
        oss << "      \"name\": \"" << comp.benchmark_name << "\",\n";
        oss << "      \"grade\": \"" << comp.overall_grade << "\",\n";
        oss << "      \"SAIDI_pass\": "  << (comp.saidi_passed ? "true" : "false") << ",\n";
        oss << "      \"SAIFI_pass\": "  << (comp.saifi_passed ? "true" : "false") << ",\n";
        oss << "      \"ASAI_pass\": "   << (comp.asai_passed  ? "true" : "false") << "\n";
        oss << "    }" << (i + 1 < comparisons.size() ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";

    return oss.str();
}

std::vector<OutageRecord> ReliabilityAnalyzer::filterByCause(
    const OutageDataset& data, const std::string& cause_code) const
{
    std::vector<OutageRecord> result;
    for (const auto& rec : data.records) {
        if (rec.cause_code == cause_code) {
            result.push_back(rec);
        }
    }
    return result;
}

std::vector<OutageRecord> ReliabilityAnalyzer::filterByComponentType(
    const OutageDataset& data, const std::string& comp_type) const
{
    std::vector<OutageRecord> result;
    for (const auto& rec : data.records) {
        if (rec.component_type == comp_type) {
            result.push_back(rec);
        }
    }
    return result;
}

std::vector<OutageRecord> ReliabilityAnalyzer::filterByDateRange(
    const OutageDataset& data,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end) const
{
    std::vector<OutageRecord> result;
    for (const auto& rec : data.records) {
        if (rec.start_time >= start && rec.start_time <= end) {
            result.push_back(rec);
        }
    }
    return result;
}

ReliabilityAnalyzer::TrendAnalysis ReliabilityAnalyzer::compareWithPrevious(
    const ReliabilityMetrics& current, const ReliabilityMetrics& previous) const
{
    TrendAnalysis trend;
    trend.saidi_delta = current.saidi - previous.saidi;
    trend.saifi_delta = current.saifi - previous.saifi;
    trend.caidi_delta = current.caidi - previous.caidi;
    trend.asai_delta  = current.asai  - previous.asai;

    int improving = 0;
    int degrading = 0;
    if (trend.saidi_delta < -0.01) improving++;
    else if (trend.saidi_delta > 0.01) degrading++;
    if (trend.saifi_delta < -0.01) improving++;
    else if (trend.saifi_delta > 0.01) degrading++;
    if (trend.asai_delta > 0.0001) improving++;
    else if (trend.asai_delta < -0.0001) degrading++;

    if (improving >= degrading + 1) {
        trend.trend_direction = "improving";
    } else if (degrading >= improving + 1) {
        trend.trend_direction = "degrading";
    } else {
        trend.trend_direction = "stable";
    }

    return trend;
}

std::vector<ReliabilityAnalyzer::FeederRanking> ReliabilityAnalyzer::rankFeeders(
    const OutageDataset& data, int top_n) const
{
    std::map<int, FeederRanking> feeder_map;

    for (const auto& rec : data.records) {
        auto& rank = feeder_map[rec.feeder_id];
        rank.feeder_id = rec.feeder_id;
        double dur = rec.duration_hours;
        if (dur <= 0.0 && rec.start_time != rec.end_time) {
            dur = hoursBetween(rec.start_time, rec.end_time);
        }
        rank.saidi_contribution += dur * rec.customers_affected;
        rank.saifi_contribution += rec.customers_affected;
        rank.total_events++;
        rank.total_customers_affected += rec.customers_affected;
    }

    std::vector<FeederRanking> rankings;
    for (auto& [id, rank] : feeder_map) {
        rank.saidi_contribution /= static_cast<double>(data.total_customers);
        rank.saifi_contribution /= static_cast<double>(data.total_customers);
        rankings.push_back(rank);
    }

    std::sort(rankings.begin(), rankings.end(),
              [](const FeederRanking& a, const FeederRanking& b) {
                  return a.saidi_contribution > b.saidi_contribution;
              });

    if (static_cast<int>(rankings.size()) > top_n) {
        rankings.resize(top_n);
    }
    return rankings;
}

std::vector<ReliabilityAnalyzer::ImprovementScenario>
ReliabilityAnalyzer::simulateImprovements(const OutageDataset& data,
                                            const ReliabilityMetrics& baseline) const
{
    std::vector<ImprovementScenario> scenarios;

    scenarios.push_back({
        "Underground conversion of worst 10% overhead lines",
        baseline.saidi * 0.30,
        baseline.saifi * 0.25,
        5000000.0,
        0.0
    });

    scenarios.push_back({
        "Automated sectionalizer deployment (smart switches)",
        baseline.saidi * 0.40,
        baseline.saifi * 0.10,
        2500000.0,
        0.0
    });

    scenarios.push_back({
        "Distribution automation with SCADA/ADMS integration",
        baseline.saidi * 0.45,
        baseline.saifi * 0.20,
        8000000.0,
        0.0
    });

    scenarios.push_back({
        "Transformer monitoring and predictive maintenance",
        baseline.saidi * 0.15,
        baseline.saifi * 0.30,
        1500000.0,
        0.0
    });

    scenarios.push_back({
        "Vegetation management enhancement (trim cycle reduction)",
        baseline.saidi * 0.20,
        baseline.saifi * 0.35,
        800000.0,
        0.0
    });

    scenarios.push_back({
        "Microgrid with DER integration for critical loads",
        baseline.saidi * 0.35,
        baseline.saifi * 0.15,
        12000000.0,
        0.0
    });

    // Calcular benefit/cost ratio
    for (auto& scen : scenarios) {
        double total_reduction = scen.estimated_saidi_reduction * data.total_customers
                               + scen.estimated_saifi_reduction * data.total_customers * 10.0;
        scen.benefit_cost_ratio = total_reduction / scen.investment_cost_usd;
    }

    std::sort(scenarios.begin(), scenarios.end(),
              [](const ImprovementScenario& a, const ImprovementScenario& b) {
                  return a.benefit_cost_ratio > b.benefit_cost_ratio;
              });

    return scenarios;
}

} // namespace powsys365
