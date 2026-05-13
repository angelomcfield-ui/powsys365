/**
 * @file harmonic_load_flow.h
 * @brief Flujo de carga armonico para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Resuelve flujo de carga para cada armonico h=2..50 mediante
 * I_h = Ybus_h * V_h donde I_h son inyecciones de fuentes armonicas.
 * Soporta multiples fuentes armonicas simultaneas.
 */

#pragma once

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <vector>
#include <complex>
#include <map>
#include <memory>

namespace powsys365 {

/**
 * @brief Motor de flujo de carga armonico.
 *
 * Para cada orden armonico h, resuelve:
 *   V(h) = Ybus(h)^(-1) * I_inj(h)
 *
 * donde:
 * - V(h) = vector de tensiones armonicas complejas en cada barra
 * - Ybus(h) = matriz de admitancia nodal para el orden armonico h
 * - I_inj(h) = vector de inyecciones de corriente armonica
 */
class HarmonicLoadFlow {
public:
    using Complex = std::complex<double>;
    using VectorXcd = Eigen::VectorXcd;
    using MatrixXcd = Eigen::MatrixXcd;
    using SparseMatrixXcd = Eigen::SparseMatrix<Complex>;

    /**
     * @brief Tipos de fuentes armonicas.
     */
    enum class SourceType {
        SIX_PULSE_CONVERTER,          ///< Convertidor 6-pulsos
        TWELVE_PULSE_CONVERTER,       ///< Convertidor 12-pulsos
        EIGHTEEN_PULSE_CONVERTER,     ///< Convertidor 18-pulsos
        PWM_INVERTER,                 ///< Inversor PWM
        VARIABLE_SPEED_DRIVE,         ///< Variador de velocidad
        ARC_FURNACE,                  ///< Horno de arco
        FLUORESCENT_LOAD,             ///< Carga fluorescente
        CUSTOM_SPECTRUM               ///< Espectro personalizado
    };

    /**
     * @brief Fuente armonica inyectada en una barra.
     */
    struct HarmonicSource {
        int busId = 0;                ///< Barra de conexion (indice 0-based)
        int order = 0;                ///< Orden armonico h
        double magnitude = 0.0;       ///< Magnitud de corriente armonica [A]
        double angle = 0.0;           ///< Angulo de fase [rad]
        SourceType type = SourceType::CUSTOM_SPECTRUM;  ///< Tipo de fuente
    };

    /**
     * @brief Modelo armonico de una linea de transmision.
     */
    struct HarmonicLineModel {
        int fromBus = 0;              ///< Barra origen
        int toBus = 0;                ///< Barra destino
        double r1 = 0.0;              ///< Resistencia fundamental [pu]
        double x1 = 0.0;              ///< Reactancia fundamental [pu]
        double b1 = 0.0;              ///< Susceptancia shunt fundamental [pu]
        double length = 0.0;          ///< Longitud [km]
        double skinEffectExponent = 0.5; ///< Exponente efecto skin (0.5 tipico)
    };

    /**
     * @brief Modelo armonico de un transformador.
     */
    struct HarmonicTransformerModel {
        int bus1 = 0;                 ///< Barra primario
        int bus2 = 0;                 ///< Barra secundario
        double ratedPower = 0.0;      ///< Potencia nominal [MVA]
        double uk = 0.0;              ///< Impedancia de cortocircuito [%]
        double pcu = 0.0;             ///< Perdidas en cobre [kW]
        double xRratio = 0.0;         ///< Relacion X/R
    };

    /**
     * @brief Modelo armonico de una carga.
     */
    struct HarmonicLoadModel {
        int busId = 0;                ///< Barra de conexion
        double pFundamental = 0.0;    ///< Potencia activa fundamental [MW]
        double qFundamental = 0.0;    ///< Potencia reactiva fundamental [MVAr]
        std::vector<double> harmonicCurrentSpectrum; ///< Espectro Ih/I1 (indice=orden)
    };

    /**
     * @brief Modelo armonico de un capacitor banco.
     */
    struct HarmonicCapacitorModel {
        int busId = 0;                ///< Barra de conexion
        double qNominal = 0.0;        ///< Potencia reactiva nominal [MVAr]
        double vNominal = 0.0;        ///< Tension nominal [kV]
        double xC1 = 0.0;             ///< Reactancia capacitiva fundamental [pu]
    };

    /**
     * @brief Resultados del flujo de carga armonico.
     */
    struct HarmonicLFResults {
        std::map<int, VectorXcd> voltagesByHarmonic;   ///< Tensiones por orden armonico
        std::map<int, VectorXcd> currentsByHarmonic;   ///< Corrientes inyectadas por orden
        VectorXcd fundamentalVoltage;                  ///< Tension fundamental (h=1)
        bool converged = false;                        ///< Estado de convergencia
        int iterations = 0;                            ///< Iteraciones realizadas
        std::map<int, SparseMatrixXcd> ybusByHarmonic; ///< Matrices Ybus por orden
    };

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------

    HarmonicLoadFlow();
    ~HarmonicLoadFlow();

    // -------------------------------------------------------------------------
    // Configuracion del sistema
    // -------------------------------------------------------------------------

    /**
     * @brief Establece el numero de barras del sistema.
     */
    void setNumBuses(int numBuses);

    /**
     * @brief Establece la frecuencia fundamental del sistema [Hz].
     */
    void setFundamentalFrequency(double freq);

    /**
     * @brief Agrega una fuente armonica.
     */
    void addHarmonicSource(const HarmonicSource& source);

    /**
     * @brief Agrega el modelo armonico de una linea.
     */
    void addLineModel(const HarmonicLineModel& line);

    /**
     * @brief Agrega el modelo armonico de un transformador.
     */
    void addTransformerModel(const HarmonicTransformerModel& tx);

    /**
     * @brief Agrega el modelo armonico de una carga.
     */
    void addLoadModel(const HarmonicLoadModel& load);

    /**
     * @brief Agrega el modelo armonico de un capacitor.
     */
    void addCapacitorModel(const HarmonicCapacitorModel& cap);

    /**
     * @brief Elimina todas las fuentes armonicas.
     */
    void clearHarmonicSources();

    /**
     * @brief Elimina todos los modelos del sistema.
     */
    void clearAllModels();

    // -------------------------------------------------------------------------
    // Construccion de Ybus armonica
    // -------------------------------------------------------------------------

    /**
     * @brief Construye la matriz de admitancia nodal Ybus para el orden armonico h.
     * @param h Orden armonico.
     * @return Matriz de admitancia armonica Ybus(h).
     */
    SparseMatrixXcd buildYbusHarmonic(int h) const;

    // -------------------------------------------------------------------------
    // Solucion del flujo armonico
    // -------------------------------------------------------------------------

    /**
     * @brief Resuelve el flujo de carga armonico completo.
     *
     * Itera sobre h = 2 .. maxHarmonic, construye Ybus_h para cada orden,
     * ensambla el vector de inyecciones I_h, y resuelve V_h = Ybus_h^(-1) * I_h.
     *
     * @param maxHarmonic Orden armonico maximo (default: 50).
     * @return Resultados del flujo de carga armonico.
     */
    HarmonicLFResults solve(int maxHarmonic = 50);

    /**
     * @brief Calcula las tensiones armonicas para un orden especifico.
     * @param h Orden armonico.
     * @param harmonicCurrents Vector de corrientes armonicas inyectadas.
     * @return Vector de tensiones armonicas complejas.
     */
    VectorXcd calculateHarmonicVoltages(int h, const VectorXcd& harmonicCurrents) const;

    /**
     * @brief Obtiene los voltajes armonicos por barra y por orden.
     * @return Mapa de orden armonico a vector de voltajes complejos.
     */
    const std::map<int, VectorXcd>& getHarmonicVoltages() const;

    /**
     * @brief Obtiene los resultados completos del ultimo solve().
     */
    const HarmonicLFResults& getResults() const;

    // -------------------------------------------------------------------------
    // Utilidades de modelado armonico
    // -------------------------------------------------------------------------

    /**
     * @brief Calcula la impedancia de linea para orden armonico h.
     *
     * R(h) = R1 * sqrt(h) (efecto skin)
     * X(h) = h * X1
     */
    static Complex lineImpedance(const HarmonicLineModel& line, int h);

    /**
     * @brief Calcula la impedancia de transformador para orden armonico h.
     */
    static Complex transformerImpedance(const HarmonicTransformerModel& tx, int h);

    /**
     * @brief Calcula la admitancia de carga para orden armonico h.
     *
     * Modelo exponencial: Y(h) = G1 - j*Q1/h
     */
    static Complex loadAdmittance(const HarmonicLoadModel& load, int h);

    /**
     * @brief Calcula la reactancia capacitiva para orden armonico h.
     *
     * Xc(h) = Xc1 / h
     */
    static Complex capacitorImpedance(const HarmonicCapacitorModel& cap, int h);

    /**
     * @brief Genera el espectro armonico caracteristico de un convertidor.
     * @param numPulses Numero de pulsos (6, 12, 18, 24).
     * @param fundamentalCurrent Corriente fundamental [A].
     * @param maxOrder Orden armonico maximo.
     * @return Mapa de orden a magnitud de corriente armonica.
     */
    static std::map<int, double> getConverterHarmonicSpectrum(
        int numPulses, double fundamentalCurrent, int maxOrder = 50);

private:
    int m_numBuses = 0;
    double m_fundamentalFreq = 50.0;  ///< Hz

    std::vector<HarmonicSource> m_sources;
    std::vector<HarmonicLineModel> m_lines;
    std::vector<HarmonicTransformerModel> m_transformers;
    std::vector<HarmonicLoadModel> m_loads;
    std::vector<HarmonicCapacitorModel> m_capacitors;

    HarmonicLFResults m_results;

    /**
     * @brief Ensambla el vector de inyecciones de corriente para el orden h.
     */
    VectorXcd assembleInjectionVector(int h) const;

    /**
     * @brief Aplica correccion por efecto skin en resistencia.
     */
    static double skinEffectResistance(double r1, int h, double exponent);
};

} // namespace powsys365
