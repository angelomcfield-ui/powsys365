/**
 * @file frequency_scan.h
 * @brief Frequency scan para analisis de resonancia en POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Realiza barrido de frecuencias para identificar frecuencias de resonancia
 * calculando |Z(f)| para un rango de frecuencias en cada barra.
 */

#pragma once

#include "harmonic_load_flow.h"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <complex>
#include <map>
#include <utility>

namespace powsys365 {

/**
 * @brief Resultado de un punto del frequency scan.
 */
struct FrequencyScanPoint {
    double frequency = 0.0;       ///< Frecuencia [Hz]
    std::complex<double> impedance; ///< Impedancia compleja Z(f) [pu]
    double magnitude = 0.0;       ///< |Z(f)| [pu]
    double angle = 0.0;           ///< angulo(Z(f)) [rad]
};

/**
 * @brief Punto de resonancia detectado.
 */
struct ResonancePoint {
    double frequency = 0.0;       ///< Frecuencia de resonancia [Hz]
    int harmonicOrder = 0;        ///< Orden armonico aproximado
    double impedanceMagnitude = 0.0; ///< |Z_res| en resonancia [pu]
    double qFactor = 0.0;         ///< Factor de calidad Q
    enum Type {
        PARALLEL,                 ///< Resonancia paralelo (|Z| maximo)
        SERIES                    ///< Resonancia serie (|Z| minimo)
    };
    Type type = PARALLEL;         ///< Tipo de resonancia
};

/**
 * @brief Motor de frequency scan para deteccion de resonancias.
 *
 * Calcula la impedancia de Thevenin Z_th(f) vista desde cada barra
 * para un rango de frecuencias, identificando picos (resonancia paralelo)
 y valles (resonancia serie).
 */
class FrequencyScanner {
public:
    using Complex = std::complex<double>;
    using SparseMatrixXcd = Eigen::SparseMatrix<Complex>;

    /**
     * @brief Constructor con referencia al flujo de carga armonico.
     */
    explicit FrequencyScanner(const HarmonicLoadFlow& hlf);

    ~FrequencyScanner();

    /**
     * @brief Ejecuta el frequency scan para una barra especifica.
     *
     * Calcula |Z(f)| para f = f_min .. f_max con 'steps' puntos.
     * La impedancia de Thevenin se calcula como Z_th = 1 / Y_ii(f).
     *
     * @param busId Barra de analisis.
     * @param f_min Frecuencia minima [Hz].
     * @param f_max Frecuencia maxima [Hz].
     * @param steps Numero de puntos del barrido.
     * @return Vector de puntos (frecuencia, impedancia) ordenados por frecuencia.
     */
    std::vector<FrequencyScanPoint> scan(int busId,
                                          double f_min = 50.0,
                                          double f_max = 2500.0,
                                          int steps = 500);

    /**
     * @brief Ejecuta frequency scan para todas las barras.
     * @return Mapa de busId a vector de puntos del scan.
     */
    std::map<int, std::vector<FrequencyScanPoint>> scanAllBuses(
        double f_min = 50.0,
        double f_max = 2500.0,
        int steps = 500);

    /**
     * @brief Detecta puntos de resonancia a partir de un frequency scan.
     *
     * Resonancia paralelo: pico local maximo en |Z(f)|.
     * Resonancia serie: valle local minimo en |Z(f)|.
     *
     * @param scanResult Resultado del frequency scan.
     * @return Vector de puntos de resonancia detectados.
     */
    std::vector<ResonancePoint> detectResonance(
        const std::vector<FrequencyScanPoint>& scanResult) const;

    /**
     * @brief Detecta resonancias para todas las barras escaneadas.
     * @return Mapa de busId a puntos de resonancia.
     */
    std::map<int, std::vector<ResonancePoint>> detectAllResonances() const;

    /**
     * @brief Obtiene el ultimo resultado de scan para una barra.
     */
    const std::vector<FrequencyScanPoint>& getScanResult(int busId) const;

    /**
     * @brief Verifica si hay resultados de scan disponibles.
     */
    bool hasScanResults() const;

    /**
     * @brief Calcula la impedancia de Thevenin en una frecuencia dada.
     *
     * Z_th(f) = 1 / Y_ii(f), donde Y_ii es el elemento diagonal
     * de Ybus correspondiente a la barra i.
     *
     * @param busId Barra de analisis.
     * @param frequency Frecuencia [Hz].
     * @return Impedancia de Thevenin compleja.
     */
    Complex calculateTheveninImpedance(int busId, double frequency) const;

    /**
     * @brief Genera datos para plot de impedancia vs frecuencia.
     *
     * Formato: pares (frecuencia_Hz, |Z|_pu) listos para graficar.
     *
     * @param busId Barra de analisis.
     * @return Vector de pares (frecuencia, |Z|).
     */
    std::vector<std::pair<double, double>> plotImpedance(int busId) const;

    /**
     * @brief Calcula el factor de calidad Q de una resonancia.
     *
     * Q = |Z_res| / R_equivalente, donde R_equivalente se estima
     * a partir del ancho de banda a -3dB.
     *
     * @param scanResult Resultado del scan completo.
     * @param resonanceIdx Indice del punto de resonancia en el scan.
     * @return Factor de calidad Q.
     */
    double calculateQFactor(const std::vector<FrequencyScanPoint>& scanResult,
                            size_t resonanceIdx) const;

private:
    const HarmonicLoadFlow& m_hlf;
    double m_fundamentalFreq = 50.0;

    std::map<int, std::vector<FrequencyScanPoint>> m_scanResults;
    std::map<int, std::vector<ResonancePoint>> m_resonancePoints;

    /**
     * @brief Construye Ybus para una frecuencia arbitraria (no necesariamente armonica).
     */
    SparseMatrixXcd buildYbusAtFrequency(double frequency) const;

    /**
     * @brief Calcula la impedancia diagonal Z_ii desde Ybus.
     */
    Complex getDiagonalImpedance(const SparseMatrixXcd& ybus, int busId) const;

    /**
     * @brief Estima el ancho de banda -3dB alrededor de un pico de resonancia.
     * @return Par de frecuencias (f_lower, f_upper) a -3dB.
     */
    std::pair<double, double> estimateBandwidth(
        const std::vector<FrequencyScanPoint>& scanResult,
        size_t peakIdx) const;
};

} // namespace powsys365
