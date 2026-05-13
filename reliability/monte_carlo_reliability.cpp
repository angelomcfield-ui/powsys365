#include "monte_carlo_reliability.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <cstring>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace powsys365 {

MonteCarloReliability::MonteCarloReliability()
    : uniform_dist_(0.0, 1.0)
    , exponential_dist_(1.0)
    , weibull_dist_(1.0, 1.0)
    , lognormal_dist_(0.0, 1.0)
{
    initializeRandomGenerator();
    params_.hours_per_year = 8760;
    params_.time_step_hours = 1.0;
    params_.peak_load_mw = 1000.0;
    params_.sequential_simulation = true;
}

void MonteCarloReliability::addComponent(const PowerComponent& component)
{
    PowerComponent comp = component;
    // Calcular MTBF si no esta proporcionado
    if (comp.mtbf_hours <= 0.0 && comp.lambda_per_year > 0.0) {
        comp.mtbf_hours = 8760.0 / comp.lambda_per_year;
    }
    // Calcular MTTR si no esta proporcionado
    if (comp.mttr_hours <= 0.0 && comp.mu_repair > 0.0) {
        comp.mttr_hours = 1.0 / comp.mu_repair;
    }
    // Calcular tasa de reparacion
    if (comp.mu_repair <= 0.0 && comp.mttr_hours > 0.0) {
        comp.mu_repair = 1.0 / comp.mttr_hours;
    }
    // Calcular FOR
    double lambda_h = comp.lambda_per_year / 8760.0;
    if (comp.forced_outage_rate <= 0.0 && lambda_h > 0.0 && comp.mttr_hours > 0.0) {
        comp.forced_outage_rate = (lambda_h * comp.mttr_hours) / (1.0 + lambda_h * comp.mttr_hours);
    }
    components_.push_back(comp);
}

void MonteCarloReliability::setLoadDurationCurve(const LoadDurationCurve& ldc)
{
    load_curve_ = ldc;
}

void MonteCarloReliability::setParameters(const SimulationParameters& params)
{
    params_ = params;
    if (params_.random_seed >= 0) {
        rng_.seed(static_cast<unsigned>(params_.random_seed));
    }
}

void MonteCarloReliability::initializeRandomGenerator()
{
    std::random_device rd;
    if (params_.random_seed >= 0) {
        rng_.seed(static_cast<unsigned>(params_.random_seed));
    } else {
        rng_.seed(rd());
    }
}

double MonteCarloReliability::sampleUniform()
{
    return uniform_dist_(rng_);
}

double MonteCarloReliability::sampleExponential(double lambda)
{
    if (lambda <= 0.0) return std::numeric_limits<double>::infinity();
    std::exponential_distribution<double> dist(lambda);
    return dist(rng_);
}

double MonteCarloReliability::sampleWeibull(double shape, double scale)
{
    if (shape <= 0.0 || scale <= 0.0) return scale;
    std::weibull_distribution<double> dist(shape, scale);
    return dist(rng_);
}

double MonteCarloReliability::sampleLognormal(double mu, double sigma)
{
    if (sigma <= 0.0) return std::exp(mu);
    std::lognormal_distribution<double> dist(mu, sigma);
    return dist(rng_);
}

double MonteCarloReliability::sampleRepairTime(const PowerComponent& comp)
{
    if (!comp.is_repairable) {
        return params_.hours_per_year * 10.0; // No reparable -> muy largo
    }
    // Usar lognormal para tiempos de reparacion (mas realista)
    if (comp.mttr_hours > 0.0) {
        double mu = std::log(comp.mttr_hours);
        double sigma = 0.5; // Coeficiente de variacion tipico
        return sampleLognormal(mu, sigma);
    }
    if (comp.mu_repair > 0.0) {
        return sampleExponential(comp.mu_repair);
    }
    return 24.0; // Default 24 horas
}

double MonteCarloReliability::sampleTimeToFailure(const PowerComponent& comp)
{
    double lambda_h = comp.lambda_per_year / static_cast<double>(params_.hours_per_year);
    if (lambda_h <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return sampleExponential(lambda_h);
}

double MonteCarloReliability::getLoadAtHour(int hour_of_year) const
{
    if (load_curve_.load_mw.empty()) {
        // Sin curva de carga, usar carga constante
        return params_.peak_load_mw;
    }
    if (hour_of_year < 0) hour_of_year = 0;
    if (hour_of_year >= params_.hours_per_year) {
        hour_of_year = params_.hours_per_year - 1;
    }
    // Interpolacion lineal en la curva de duracion de carga
    double frac = static_cast<double>(hour_of_year) / static_cast<double>(params_.hours_per_year);
    size_t idx = static_cast<size_t>(frac * (load_curve_.load_mw.size() - 1));
    if (idx >= load_curve_.load_mw.size() - 1) {
        return load_curve_.load_mw.back();
    }
    double f = frac * (load_curve_.load_mw.size() - 1) - idx;
    return load_curve_.load_mw[idx] * (1.0 - f) + load_curve_.load_mw[idx + 1] * f;
}

double MonteCarloReliability::computeLoadFactor(int hour) const
{
    return getLoadAtHour(hour) / params_.peak_load_mw;
}

double MonteCarloReliability::evaluateKofNCapacity(const PowerComponent& comp,
                                                       int num_failed) const
{
    int available = comp.num_redundant_units - num_failed;
    if (available < comp.min_operating_units) {
        return 0.0;
    }
    double fraction = static_cast<double>(available) / static_cast<double>(comp.num_redundant_units);
    return comp.capacity_mw * fraction * comp.derating_factor;
}

double MonteCarloReliability::computeAvailableCapacity(
    const std::vector<std::string>& failed_ids) const
{
    double available = 0.0;
    for (const auto& comp : components_) {
        int num_failed = 0;
        for (const auto& fid : failed_ids) {
            if (fid == comp.id) num_failed++;
        }
        // Para componentes no-generadores, solo falla completa
        if (comp.type == ComponentType::GENERATOR) {
            available += evaluateKofNCapacity(comp, num_failed);
        } else {
            if (num_failed == 0) {
                available += comp.capacity_mw * comp.derating_factor;
            }
        }
    }
    return available;
}

std::vector<MonteCarloReliability::StateTransition>
MonteCarloReliability::propagateCascadingFailures(
    const StateTransition& initial_failure)
{
    std::vector<StateTransition> cascading;
    cascading.push_back(initial_failure);

    // Encontrar el componente inicial
    const PowerComponent* init_comp = nullptr;
    for (const auto& comp : components_) {
        if (comp.id == initial_failure.component_id) {
            init_comp = &comp;
            break;
        }
    }
    if (!init_comp) return cascading;

    // Para cada componente dependiente, calcular probabilidad de cascada
    for (const auto& dep_id : init_comp->dependent_components) {
        const PowerComponent* dep_comp = nullptr;
        for (const auto& comp : components_) {
            if (comp.id == dep_id) {
                dep_comp = &comp;
                break;
            }
        }
        if (!dep_comp) continue;

        // Probabilidad de cascada basada en la capacidad perdida
        double lost_capacity = initial_failure.capacity_impact_mw;
        double cascade_prob = std::min(0.3, lost_capacity / (dep_comp->capacity_mw + 1.0));

        if (sampleUniform() < cascade_prob) {
            StateTransition cascade;
            cascade.transition_time = initial_failure.transition_time + sampleUniform() * 0.5;
            cascade.component_id = dep_id;
            cascade.from_state = "UP";
            cascade.to_state = "DOWN";
            cascade.capacity_impact_mw = dep_comp->capacity_mw;
            cascading.push_back(cascade);
        }
    }

    return cascading;
}

std::vector<SystemState> MonteCarloReliability::generateAnnualTrajectory()
{
    std::vector<SystemState> trajectory;
    trajectory.reserve(params_.hours_per_year);

    // Inicializar estado de cada componente
    struct CompState {
        bool is_up = true;
        double time_to_next_transition = 0.0;
        double accumulated_downtime = 0.0;
    };
    std::vector<CompState> comp_states(components_.size());

    // Generar tiempos iniciales a falla
    for (size_t i = 0; i < components_.size(); ++i) {
        comp_states[i].time_to_next_transition = sampleTimeToFailure(components_[i]);
    }

    std::vector<StateTransition> active_events;

    for (int h = 0; h < params_.hours_per_year; ++h) {
        double current_time = static_cast<double>(h);
        double next_time = static_cast<double>(h + 1);

        // Verificar transiciones de componentes
        for (size_t i = 0; i < components_.size(); ++i) {
            auto& cs = comp_states[i];
            const auto& comp = components_[i];

            if (cs.is_up) {
                // Verificar si falla en este intervalo
                if (cs.time_to_next_transition <= next_time) {
                    // Componente falla
                    cs.is_up = false;
                    StateTransition tr;
                    tr.transition_time = cs.time_to_next_transition;
                    tr.component_id = comp.id;
                    tr.from_state = "UP";
                    tr.to_state = "DOWN";
                    tr.capacity_impact_mw = comp.capacity_mw;
                    active_events.push_back(tr);

                    // Tiempo de reparacion
                    double repair_time = sampleRepairTime(comp);
                    cs.time_to_next_transition = cs.time_to_next_transition + repair_time;
                    cs.accumulated_downtime = 0.0;

                    // Propagacion en cascada
                    auto cascades = propagateCascadingFailures(tr);
                    for (size_t c = 1; c < cascades.size(); ++c) {
                        active_events.push_back(cascades[c]);
                    }
                }
            } else {
                // Componente en reparacion
                cs.accumulated_downtime += (next_time - current_time);
                if (cs.time_to_next_transition <= next_time) {
                    // Reparacion completa
                    cs.is_up = true;
                    StateTransition tr;
                    tr.transition_time = cs.time_to_next_transition;
                    tr.component_id = comp.id;
                    tr.from_state = "DOWN";
                    tr.to_state = "UP";
                    tr.capacity_impact_mw = -comp.capacity_mw; // Recuperacion
                    active_events.push_back(tr);

                    // Nuevo tiempo a falla
                    cs.time_to_next_transition = next_time + sampleTimeToFailure(comp);
                    cs.accumulated_downtime = 0.0;
                }
            }
        }

        // Limpiar eventos resueltos
        active_events.erase(
            std::remove_if(active_events.begin(), active_events.end(),
                [&](const StateTransition& ev) {
                    return ev.to_state == "UP" && ev.transition_time <= next_time;
                }),
            active_events.end());

        // Evaluar estado del sistema
        SystemState state;
        state.time_hour = current_time;
        state.total_capacity_mw = 0.0;
        state.available_capacity_mw = 0.0;

        // Calcular capacidades
        for (size_t i = 0; i < components_.size(); ++i) {
            const auto& comp = components_[i];
            if (comp.type == ComponentType::GENERATOR) {
                state.total_capacity_mw += comp.capacity_mw;
                if (comp_states[i].is_up) {
                    state.available_capacity_mw += comp.capacity_mw * comp.derating_factor;
                }
            } else {
                state.total_capacity_mw += comp.capacity_mw;
                if (comp_states[i].is_up) {
                    state.available_capacity_mw += comp.capacity_mw;
                }
            }
        }

        // Carga en esta hora
        state.total_load_mw = getLoadAtHour(h);

        // Verificar perdida de carga
        if (state.available_capacity_mw < state.total_load_mw) {
            state.is_load_loss = true;
            state.load_loss_mw = state.total_load_mw - state.available_capacity_mw;
            state.energy_not_supplied_mwh = state.load_loss_mw * params_.time_step_hours;
        }

        // Guardar componentes fallados
        for (size_t i = 0; i < components_.size(); ++i) {
            if (!comp_states[i].is_up) {
                state.failed_components.push_back(components_[i].id);
            }
        }

        trajectory.push_back(state);
    }

    return trajectory;
}

MonteCarloResults MonteCarloReliability::runSequentialSimulation(int n_iterations)
{
    int iters = (n_iterations > 0) ? n_iterations : params_.max_iterations;
    MonteCarloResults results;
    results.total_iterations = iters;

    auto start_time = std::chrono::steady_clock::now();

    std::vector<double> lolp_samples;
    std::vector<double> eens_samples;
    std::vector<double> lole_samples;
    lolp_samples.reserve(iters);
    eens_samples.reserve(iters);
    lole_samples.reserve(iters);

    for (int iter = 0; iter < iters; ++iter) {
        auto trajectory = generateAnnualTrajectory();

        // Agregar a histogramas
        double iter_eens = 0.0;
        double iter_lole = 0.0;
        for (const auto& state : trajectory) {
            if (state.is_load_loss) {
                iter_lole += params_.time_step_hours;
                iter_eens += state.energy_not_supplied_mwh;
                results.load_loss_histogram[state.load_loss_mw]++;
            }
        }

        double iter_lolp = iter_lole / static_cast<double>(params_.hours_per_year);
        lolp_samples.push_back(iter_lolp);
        eens_samples.push_back(iter_eens);
        lole_samples.push_back(iter_lole);

        // Verificar convergencia cada 100 iteraciones
        if (iter > params_.min_iterations && iter % 100 == 0) {
            if (checkConvergence(lolp_samples, eens_samples)) {
                results.iterations_to_converge = iter;
                results.converged = true;
                break;
            }
        }
    }

    // Calcular estadisticas
    int n = static_cast<int>(lolp_samples.size());
    if (n > 0) {
        results.lolp = std::accumulate(lolp_samples.begin(), lolp_samples.end(), 0.0) / n;
        results.lole_hours_per_year = std::accumulate(lole_samples.begin(), lole_samples.end(), 0.0) / n;
        results.eens_mwh_per_year = std::accumulate(eens_samples.begin(), eens_samples.end(), 0.0) / n;

        // Desviaciones estandar
        if (n > 1) {
            double lolp_var = 0.0, eens_var = 0.0;
            for (int i = 0; i < n; ++i) {
                lolp_var += (lolp_samples[i] - results.lolp) * (lolp_samples[i] - results.lolp);
                eens_var += (eens_samples[i] - results.eens_mwh_per_year) * (eens_samples[i] - results.eens_mwh_per_year);
            }
            lolp_var /= (n - 1);
            eens_var /= (n - 1);
            results.lolp_std = std::sqrt(lolp_var / n);
            results.eens_std = std::sqrt(eens_var / n);
        }

        // Intervalos de confianza 95%
        results.lolp_ci_low = computeConfidenceIntervalLow(lolp_samples);
        results.lolp_ci_high = computeConfidenceIntervalHigh(lolp_samples);
        results.lole_ci_low = computeConfidenceIntervalLow(lole_samples);
        results.lole_ci_high = computeConfidenceIntervalHigh(lole_samples);
        results.eens_ci_low = computeConfidenceIntervalLow(eens_samples);
        results.eens_ci_high = computeConfidenceIntervalHigh(eens_samples);

        // Coeficiente de variacion
        if (results.lolp > 0.0) {
            results.convergence_beta = results.lolp_std / results.lolp;
        }

        // Indices derivados
        results.edlc_hours_per_year = results.lole_hours_per_year;
        if (results.lole_hours_per_year > 0.0) {
            results.eflc_events_per_year = static_cast<double>(results.load_loss_histogram.size()) / n;
        }
        results.edns_mw = results.eens_mwh_per_year / static_cast<double>(params_.hours_per_year);
        if (params_.peak_load_mw > 0.0) {
            results.siip = results.eens_mwh_per_year * 60.0 / params_.peak_load_mw;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    results.simulation_wall_time_seconds = std::chrono::duration<double>(end_time - start_time).count();

    return results;
}

MonteCarloResults MonteCarloReliability::runNonSequentialSimulation(int n_iterations)
{
    int iters = (n_iterations > 0) ? n_iterations : params_.max_iterations;
    MonteCarloResults results;
    results.total_iterations = iters;

    auto start_time = std::chrono::steady_clock::now();

    std::vector<double> lolp_samples;
    std::vector<double> eens_samples;
    lolp_samples.reserve(iters);
    eens_samples.reserve(iters);

    for (int iter = 0; iter < iters; ++iter) {
        // Muestrear estado de cada componente (UP/DOWN)
        std::vector<bool> comp_up(components_.size(), true);
        for (size_t i = 0; i < components_.size(); ++i) {
            if (sampleUniform() < components_[i].forced_outage_rate) {
                comp_up[i] = false;
            }
        }

        // Calcular capacidad disponible
        double available_cap = 0.0;
        double total_cap = 0.0;
        for (size_t i = 0; i < components_.size(); ++i) {
            const auto& comp = components_[i];
            total_cap += comp.capacity_mw;
            if (comp_up[i]) {
                available_cap += comp.capacity_mw * comp.derating_factor;
            }
        }

        // Para cada nivel de carga en la curva LDC
        double iter_eens = 0.0;
        double iter_lole = 0.0;

        if (!load_curve_.load_mw.empty() && !load_curve_.duration_frac.empty()) {
            for (size_t i = 0; i < load_curve_.load_mw.size(); ++i) {
                double load = load_curve_.load_mw[i];
                double frac = (i + 1 < load_curve_.duration_frac.size())
                                ? load_curve_.duration_frac[i] - load_curve_.duration_frac[i + 1]
                                : load_curve_.duration_frac[i];
                if (frac < 0) frac = 0;

                if (available_cap < load) {
                    double deficit = load - available_cap;
                    iter_eens += deficit * frac * params_.hours_per_year;
                    iter_lole += frac * params_.hours_per_year;
                }
            }
        } else {
            // Sin LDC, usar carga pico constante
            if (available_cap < params_.peak_load_mw) {
                iter_eens = (params_.peak_load_mw - available_cap) * params_.hours_per_year;
                iter_lole = params_.hours_per_year;
            }
        }

        lolp_samples.push_back(available_cap < params_.peak_load_mw ? 1.0 : 0.0);
        eens_samples.push_back(iter_eens);

        if (iter > params_.min_iterations && iter % 100 == 0) {
            if (checkConvergence(lolp_samples, eens_samples)) {
                results.iterations_to_converge = iter;
                results.converged = true;
                break;
            }
        }
    }

    int n = static_cast<int>(lolp_samples.size());
    if (n > 0) {
        results.lolp = std::accumulate(lolp_samples.begin(), lolp_samples.end(), 0.0) / n;
        results.eens_mwh_per_year = std::accumulate(eens_samples.begin(), eens_samples.end(), 0.0) / n;
        results.lole_hours_per_year = results.lolp * params_.hours_per_year;

        if (n > 1) {
            double lolp_var = 0.0, eens_var = 0.0;
            for (int i = 0; i < n; ++i) {
                lolp_var += (lolp_samples[i] - results.lolp) * (lolp_samples[i] - results.lolp);
                eens_var += (eens_samples[i] - results.eens_mwh_per_year) * (eens_samples[i] - results.eens_mwh_per_year);
            }
            lolp_var /= (n - 1);
            eens_var /= (n - 1);
            results.lolp_std = std::sqrt(lolp_var / n);
            results.eens_std = std::sqrt(eens_var / n);
        }

        results.lolp_ci_low = computeConfidenceIntervalLow(lolp_samples);
        results.lolp_ci_high = computeConfidenceIntervalHigh(lolp_samples);
        results.eens_ci_low = computeConfidenceIntervalLow(eens_samples);
        results.eens_ci_high = computeConfidenceIntervalHigh(eens_samples);
        results.lole_ci_low = results.lolp_ci_low * params_.hours_per_year;
        results.lole_ci_high = results.lolp_ci_high * params_.hours_per_year;

        if (results.lolp > 0.0) {
            results.convergence_beta = results.lolp_std / results.lolp;
        }

        results.edlc_hours_per_year = results.lole_hours_per_year;
        results.edns_mw = results.eens_mwh_per_year / params_.hours_per_year;
        if (params_.peak_load_mw > 0.0) {
            results.siip = results.eens_mwh_per_year * 60.0 / params_.peak_load_mw;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    results.simulation_wall_time_seconds = std::chrono::duration<double>(end_time - start_time).count();

    return results;
}

MonteCarloResults MonteCarloReliability::runSimulation(int n_iterations)
{
    if (params_.sequential_simulation) {
        return runSequentialSimulation(n_iterations);
    } else {
        return runNonSequentialSimulation(n_iterations);
    }
}

std::vector<StateTransition> MonteCarloReliability::simulateOutages()
{
    return generateAnnualTrajectory();
}

double MonteCarloReliability::calculateLOLP(const MonteCarloResults& results) const
{
    return results.lolp;
}

double MonteCarloReliability::calculateLOLE(const MonteCarloResults& results) const
{
    return results.lole_hours_per_year;
}

double MonteCarloReliability::calculateEENS(const MonteCarloResults& results) const
{
    return results.eens_mwh_per_year;
}

double MonteCarloReliability::calculateEDLC(const MonteCarloResults& results) const
{
    return results.edlc_hours_per_year;
}

double MonteCarloReliability::calculateEFLC(const MonteCarloResults& results) const
{
    return results.eflc_events_per_year;
}

double MonteCarloReliability::calculateEDNS(const MonteCarloResults& results) const
{
    return results.edns_mw;
}

double MonteCarloReliability::calculateSIIP(const MonteCarloResults& results) const
{
    return results.siip;
}

bool MonteCarloReliability::checkConvergence(
    const std::vector<double>& lolp_history,
    const std::vector<double>& eens_history) const
{
    if (static_cast<int>(lolp_history.size()) < params_.min_iterations) return false;

    int n = static_cast<int>(lolp_history.size());
    int window = std::min(100, n / 2);
    if (window < 10) return false;

    double recent_mean = 0.0;
    for (int i = n - window; i < n; ++i) {
        recent_mean += lolp_history[i];
    }
    recent_mean /= window;

    double recent_var = 0.0;
    for (int i = n - window; i < n; ++i) {
        recent_var += (lolp_history[i] - recent_mean) * (lolp_history[i] - recent_mean);
    }
    recent_var /= window;
    double recent_std = std::sqrt(recent_var);

    double cv = (recent_mean > 0.0) ? (recent_std / std::sqrt(window)) / recent_mean : 0.0;
    return cv < params_.convergence_threshold;
}

double MonteCarloReliability::computeConfidenceIntervalLow(
    const std::vector<double>& samples) const
{
    if (samples.empty()) return 0.0;
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(sorted.size() * 0.025);
    if (idx >= sorted.size()) idx = 0;
    return sorted[idx];
}

double MonteCarloReliability::computeConfidenceIntervalHigh(
    const std::vector<double>& samples) const
{
    if (samples.empty()) return 0.0;
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(sorted.size() * 0.975);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

LoadDurationCurve MonteCarloReliability::generateLoadDurationCurve(
    const std::vector<double>& hourly_loads) const
{
    LoadDurationCurve ldc;
    if (hourly_loads.empty()) return ldc;

    ldc.load_mw = hourly_loads;
    std::sort(ldc.load_mw.begin(), ldc.load_mw.end(), std::greater<double>());

    ldc.peak_load_mw = ldc.load_mw.front();
    ldc.min_load_mw = ldc.load_mw.back();
    ldc.average_load_mw = std::accumulate(ldc.load_mw.begin(), ldc.load_mw.end(), 0.0)
                           / ldc.load_mw.size();
    ldc.total_energy_mwh = ldc.average_load_mw * ldc.load_mw.size();

    size_t n = ldc.load_mw.size();
    ldc.duration_frac.resize(n);
    for (size_t i = 0; i < n; ++i) {
        ldc.duration_frac[i] = static_cast<double>(i) / static_cast<double>(n);
    }

    return ldc;
}

std::vector<MonteCarloReliability::SensitivityResult>
MonteCarloReliability::runSensitivityAnalysis(
    const std::string& component_id,
    const std::vector<double>& lambda_multipliers)
{
    std::vector<SensitivityResult> results;

    // Guardar componente original
    PowerComponent original_comp;
    bool found = false;
    for (const auto& comp : components_) {
        if (comp.id == component_id) {
            original_comp = comp;
            found = true;
            break;
        }
    }
    if (!found) return results;

    for (double mult : lambda_multipliers) {
        // Modificar tasa de falla
        for (auto& comp : components_) {
            if (comp.id == component_id) {
                comp.lambda_per_year = original_comp.lambda_per_year * mult;
                comp.mtbf_hours = 8760.0 / comp.lambda_per_year;
                double lambda_h = comp.lambda_per_year / 8760.0;
                comp.forced_outage_rate = (lambda_h * comp.mttr_hours)
                                            / (1.0 + lambda_h * comp.mttr_hours);
                break;
            }
        }

        auto mc_results = runSimulation(5000);

        SensitivityResult sr;
        sr.component_id = component_id;
        sr.lambda_multiplier = mult;
        sr.lolp = mc_results.lolp;
        sr.lole = mc_results.lole_hours_per_year;
        sr.eens = mc_results.eens_mwh_per_year;
        results.push_back(sr);
    }

    // Restaurar componente original
    for (auto& comp : components_) {
        if (comp.id == component_id) {
            comp = original_comp;
            break;
        }
    }

    return results;
}

bool MonteCarloReliability::validateModel() const
{
    if (components_.empty()) return false;
    if (params_.peak_load_mw <= 0.0) return false;

    double total_capacity = 0.0;
    for (const auto& comp : components_) {
        if (comp.lambda_per_year < 0.0) return false;
        if (comp.mttr_hours < 0.0) return false;
        if (comp.capacity_mw < 0.0) return false;
        total_capacity += comp.capacity_mw;
    }

    // El sistema debe tener capacidad > carga pico
    return total_capacity > params_.peak_load_mw * 1.05;
}

std::string MonteCarloReliability::generateReport(const MonteCarloResults& results) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    oss << "==================================================================\n";
    oss << "  POWSYS365 - Monte Carlo Reliability Simulation Report\n";
    oss << "  Method: " << (params_.sequential_simulation ? "Sequential" : "Non-Sequential") << "\n";
    oss << "  Iterations: " << results.total_iterations << " (converged: "
        << (results.converged ? "YES" : "NO") << ")\n";
    oss << "  Simulation time: " << results.simulation_wall_time_seconds << " seconds\n";
    oss << "==================================================================\n\n";

    oss << "--- Core Reliability Indices ---\n";
    oss << "LOLP  : " << results.lolp  << "  [" << results.lolp_ci_low << ", "
        << results.lolp_ci_high << "] 95% CI\n";
    oss << "LOLE  : " << results.lole_hours_per_year << " h/year  ["
        << results.lole_ci_low << ", " << results.lole_ci_high << "] 95% CI\n";
    oss << "EENS  : " << results.eens_mwh_per_year << " MWh/year  ["
        << results.eens_ci_low << ", " << results.eens_ci_high << "] 95% CI\n";
    oss << "\n";

    oss << "--- Derived Indices ---\n";
    oss << "EDLC  : " << results.edlc_hours_per_year << " h/year\n";
    oss << "EFLC  : " << results.eflc_events_per_year << " events/year\n";
    oss << "EDNS  : " << results.edns_mw << " MW\n";
    oss << "SIIP  : " << results.siip << " system-minutes\n";
    oss << "\n";

    oss << "--- Convergence ---\n";
    oss << "Beta (CV): " << results.convergence_beta << "\n";
    oss << "Iterations to converge: " << results.iterations_to_converge << "\n";
    oss << "\n";

    // Histograma de perdida de carga
    oss << "--- Load Loss Histogram (MW -> count) ---\n";
    for (const auto& [load, count] : results.load_loss_histogram) {
        oss << "  " << load << " MW : " << count << "\n";
    }

    return oss.str();
}

} // namespace powsys365
