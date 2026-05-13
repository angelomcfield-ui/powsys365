/**
 * @file settlement_calculator.h
 * @brief Calculador de settlements para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 *
 * Calcula settlements energeticos, servicios auxiliares,
 * genera invoices y soporta precios nodales vs zonales.
 *
 * Settlement energetico:
 *   $ = MWh * LMP [USD/MWh]
 *
 * Servicios auxiliares:
 *   $ = MW_asignado * Precio_clearing [USD/MW]
 *
 * Congestion:
 *   $ = MWh * (LMP_local - LMP_referencia)
 */

#pragma once

#include "lmp_calculator.h"
#include <vector>
#include <map>
#include <string>
#include <chrono>

namespace powsys365 {

/**
 * @brief Tipo de servicio auxiliar.
 */
enum class AncillaryServiceType {
    SPINNING_RESERVE,           ///< Reserva girante
    NON_SPINNING_RESERVE,       ///< Reserva no girante
    REGULATION_UP,              ///< Regulacion de frecuencia (subida)
    REGULATION_DOWN,            ///< Regulacion de frecuencia (bajada)
    REACTIVE_SUPPORT,           ///< Soporte de reactivos
    BLACK_START,                ///< Arranque en negro
    VOLTAGE_SUPPORT             ///< Soporte de voltaje
};

/**
 * @brief Oferta de servicio auxiliar.
 */
struct AncillaryServiceOffer {
    AncillaryServiceType type;    ///< Tipo de servicio
    int providerId = 0;           ///< Generador proveedor (busId)
    double capacity = 0.0;        ///< Capacidad ofertada [MW]
    double price = 0.0;           ///< Precio de oferta [USD/MW]
    double clearedAmount = 0.0;   ///< Cantidad asignada [MW]
    double clearingPrice = 0.0;   ///< Precio de clearing [USD/MW]
    bool accepted = false;        ///< Fue aceptada en el mercado
};

/**
 * @brief Item de settlement.
 */
struct SettlementItem {
    std::string description;      ///< Descripcion del item
    double quantity = 0.0;        ///< Cantidad [MWh o MW]
    double unitPrice = 0.0;       ///< Precio unitario [USD]
    double amount = 0.0;          ///< Monto total [USD]
    std::string period;           ///< Periodo de settlement
};

/**
 * @brief Invoice completo de settlement.
 */
struct SettlementInvoice {
    int entityId = 0;             ///< ID de la entidad (generador/carga)
    std::string entityName;       ///< Nombre de la entidad
    std::string entityType;       ///< Tipo: GENERADOR, CARGA, COMERCIALIZADOR
    std::string period;           ///< Periodo de facturacion
    std::vector<SettlementItem> items;  ///< Items de la factura
    double totalEnergy = 0.0;     ///< Total energia [MWh]
    double totalAncillary = 0.0;  ///< Total servicios auxiliares [USD]
    double totalCongestion = 0.0; ///< Total congestion [USD]
    double totalLosses = 0.0;     ///< Total perdidas [USD]
    double subtotal = 0.0;        ///< Subtotal [USD]
    double tax = 0.0;             ///< Impuestos [USD]
    double total = 0.0;           ///< Total [USD]
    bool isNodal = true;          ///< Precio nodal (true) o zonal (false)
    std::string pricingZone;      ///< Zona de precio (si aplica)
};

/**
 * @brief Resultado del clearing de servicios auxiliares.
 */
struct AncillaryClearingResult {
    std::vector<AncillaryServiceOffer> clearedServices;  ///< Servicios asignados
    std::map<AncillaryServiceType, double> clearingPrices;  ///< Precio por tipo
    std::map<AncillaryServiceType, double> totalCleared;    ///< Total asignado por tipo
    double totalCost = 0.0;      ///< Costo total [USD]
};

/**
 * @brief Calculador de settlements para mercados electricos.
 *
 * Implementa:
 * - Settlement energetico: MWh * LMP
 * - Clearing de servicios auxiliares (merit order)
 * - Generacion de invoices
 * - Soporte para precios nodales y zonales
 */
class SettlementCalculator {
public:
    /**
     * @brief Constructor.
     */
    SettlementCalculator();
    ~SettlementCalculator();

    /**
     * @brief Establece los datos LMP para settlement.
     */
    void setLMPData(const std::vector<LMPResult>& lmpResults);

    /**
     * @brief Establece el periodo de settlement.
     */
    void setPeriod(const std::string& period);

    /**
     * @brief Calcula el settlement energetico.
     *
     * $_settlement = MWh_generados * LMP_bus [USD]
     *
     * @param genId ID del generador (busId).
     * @param mWh Energia generada [MWh].
     * @param lmp Precio LMP en la barra [USD/MWh].
     * @return Monto del settlement [USD].
     */
    double calculateEnergySettlement(int genId, double mWh, double lmp) const;

    /**
     * @brief Calcula settlement energetico con componentes desglosados.
     *
     * Descompone el settlement en:
     * - Energia: MWh * lambda
     * - Congestion: MWh * mu
     * - Perdidas: MWh * gamma
     *
     * @param genId ID del generador.
     * @param mWh Energia [MWh].
     * @param lmpResult Resultado LMP completo con descomposicion.
     * @return Invoice con items desglosados.
     */
    SettlementInvoice calculateEnergySettlementDetailed(
        int genId, double mWh, const LMPResult& lmpResult) const;

    /**
     * @brief Calcula settlement para una carga.
     *
     * La carga PAGA el LMP de su barra por cada MWh consumido.
     *
     * @param loadId ID de la carga.
     * @param mWh Energia consumida [MWh].
     * @param lmp Precio LMP [USD/MWh].
     * @return Monto a pagar [USD].
     */
    double calculateLoadSettlement(int loadId, double mWh, double lmp) const;

    /**
     * @brief Realiza clearing de servicios auxiliares.
     *
     * Algoritmo de merit order:
     * 1. Ordenar ofertas por precio (menor a mayor)
     * 2. Asignar por orden hasta cubrir requerimiento
     * 3. Precio de clearing = precio de ultima oferta aceptada
     *
     * min sum(c_i * r_i) sujeto a:
     *   sum(r_i) >= R_required
     *   r_i <= R_i_max
     *   r_i >= 0
     *
     * @param offers Lista de ofertas de servicios auxiliares.
     * @param requirements Requerimientos por tipo [MW].
     * @return Resultado del clearing.
     */
    AncillaryClearingResult calculateAncillaryServices(
        const std::vector<AncillaryServiceOffer>& offers,
        const std::map<AncillaryServiceType, double>& requirements);

    /**
     * @brief Genera invoice para un generador.
     *
     * @param genId ID del generador.
     * @param genName Nombre del generador.
     * @param mWhByPeriod Energia por periodo [MWh].
     * @param isNodal true=nodal, false=zonal.
     * @return Invoice completo.
     */
    SettlementInvoice generateGeneratorInvoice(
        int genId,
        const std::string& genName,
        const std::map<std::string, double>& mWhByPeriod,
        bool isNodal = true);

    /**
     * @brief Genera invoice para una carga.
     *
     * @param loadId ID de la carga.
     * @param loadName Nombre de la carga.
     * @param mWhByPeriod Energia por periodo [MWh].
     * @param isNodal true=nodal, false=zonal.
     * @return Invoice completo.
     */
    SettlementInvoice generateLoadInvoice(
        int loadId,
        const std::string& loadName,
        const std::map<std::string, double>& mWhByPeriod,
        bool isNodal = true);

    /**
     * @brief Genera invoice con servicios auxiliares.
     *
     * @param entityId ID de la entidad.
     * @param entityName Nombre.
     * @param energyInvoice Invoice de energia.
     * @param ancillaryServices Servicios auxiliares asignados.
     * @return Invoice combinado.
     */
    SettlementInvoice generateCombinedInvoice(
        int entityId,
        const std::string& entityName,
        const SettlementInvoice& energyInvoice,
        const std::vector<AncillaryServiceOffer>& ancillaryServices);

    /**
     * @brief Convierte invoice a formato string.
     */
    std::string invoiceToString(const SettlementInvoice& invoice) const;

    /**
     * @brief Convierte tipo de servicio auxiliar a string.
     */
    static std::string serviceTypeToString(AncillaryServiceType type);

    /**
     * @brief Obtiene todos los invoices generados.
     */
    const std::vector<SettlementInvoice>& getAllInvoices() const;

    /**
     * @brief Calcula el total de settlements del periodo.
     */
    double getPeriodTotal() const;

private:
    std::vector<LMPResult> m_lmpResults;
    std::string m_period;
    std::vector<SettlementInvoice> m_invoices;

    /**
     * @brief Busca el LMP de una barra.
     */
    double getLMPForBus(int busId) const;

    /**
     * @brief Busca el resultado LMP completo de una barra.
     */
    const LMPResult* getLMPResultForBus(int busId) const;

    /**
     * @brief Calcula impuestos sobre el subtotal.
     */
    double calculateTax(double subtotal) const;
};

} // namespace powsys365
