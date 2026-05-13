/**
 * @file settlement_calculator.cpp
 * @brief Implementacion del calculador de settlements para POWSYS365
 * @author POWSYS365 Team
 * @copyright 2025 XNOX L.L.C.
 * @version 1.0.0
 */

#include "settlement_calculator.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace powsys365 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SettlementCalculator::SettlementCalculator() = default;
SettlementCalculator::~SettlementCalculator() = default;

// ============================================================================
// Configuracion
// ============================================================================

void SettlementCalculator::setLMPData(const std::vector<LMPResult>& lmpResults) {
    m_lmpResults = lmpResults;
}

void SettlementCalculator::setPeriod(const std::string& period) {
    m_period = period;
}

// ============================================================================
// Buscar LMP de una barra
// ============================================================================

double SettlementCalculator::getLMPForBus(int busId) const {
    for (const auto& lmp : m_lmpResults) {
        if (lmp.busId == busId) {
            return lmp.lmp;
        }
    }
    return 0.0;
}

const LMPResult* SettlementCalculator::getLMPResultForBus(int busId) const {
    for (const auto& lmp : m_lmpResults) {
        if (lmp.busId == busId) {
            return &lmp;
        }
    }
    return nullptr;
}

// ============================================================================
// Calcular impuestos
// ============================================================================

double SettlementCalculator::calculateTax(double subtotal) const {
    // Tasa de impuesto tipica sobre settlements de energia
    const double TAX_RATE = 0.16;  // 16% IVA (ejemplo)
    return subtotal * TAX_RATE;
}

// ============================================================================
// Settlement energetico basico
// ============================================================================

double SettlementCalculator::calculateEnergySettlement(int genId,
    double mWh, double lmp) const {

    (void)genId;  // Usado para reportes en version extendida
    return mWh * lmp;  // [MWh] * [USD/MWh] = [USD]
}

// ============================================================================
// Settlement energetico detallado
// ============================================================================

SettlementInvoice SettlementCalculator::calculateEnergySettlementDetailed(
    int genId, double mWh, const LMPResult& lmpResult) const {

    SettlementInvoice invoice;
    invoice.entityId = genId;
    invoice.entityType = "GENERADOR";
    invoice.period = m_period;
    invoice.isNodal = true;

    // Item: Energia base
    SettlementItem energyItem;
    energyItem.description = "Energia - Componente de Energia";
    energyItem.quantity = mWh;
    energyItem.unitPrice = lmpResult.energyComponent;
    energyItem.amount = mWh * lmpResult.energyComponent;
    energyItem.period = m_period;
    invoice.items.push_back(energyItem);
    invoice.totalEnergy += mWh;

    // Item: Congestion
    if (std::abs(lmpResult.congestionComponent) > 1e-6) {
        SettlementItem congestionItem;
        congestionItem.description = "Energia - Componente de Congestion";
        congestionItem.quantity = mWh;
        congestionItem.unitPrice = lmpResult.congestionComponent;
        congestionItem.amount = mWh * lmpResult.congestionComponent;
        congestionItem.period = m_period;
        invoice.items.push_back(congestionItem);
        invoice.totalCongestion += congestionItem.amount;
    }

    // Item: Perdidas
    if (std::abs(lmpResult.lossesComponent) > 1e-6) {
        SettlementItem lossesItem;
        lossesItem.description = "Energia - Componente de Perdidas";
        lossesItem.quantity = mWh;
        lossesItem.unitPrice = lmpResult.lossesComponent;
        lossesItem.amount = mWh * lmpResult.lossesComponent;
        lossesItem.period = m_period;
        invoice.items.push_back(lossesItem);
        invoice.totalLosses += lossesItem.amount;
    }

    // Subtotal
    invoice.subtotal = energyItem.amount + invoice.totalCongestion + invoice.totalLosses;
    invoice.tax = calculateTax(invoice.subtotal);
    invoice.total = invoice.subtotal + invoice.tax;

    return invoice;
}

// ============================================================================
// Settlement de carga
// ============================================================================

double SettlementCalculator::calculateLoadSettlement(int loadId,
    double mWh, double lmp) const {

    (void)loadId;
    return mWh * lmp;
}

// ============================================================================
// Clearing de servicios auxiliares (merit order)
// ============================================================================

AncillaryClearingResult SettlementCalculator::calculateAncillaryServices(
    const std::vector<AncillaryServiceOffer>& offers,
    const std::map<AncillaryServiceType, double>& requirements) {

    AncillaryClearingResult result;

    // Para cada tipo de servicio requerido
    for (const auto& [reqType, required] : requirements) {
        // Filtrar ofertas del tipo requerido
        std::vector<AncillaryServiceOffer*> typeOffers;
        std::vector<AncillaryServiceOffer> mutableOffers = offers;

        for (auto& offer : mutableOffers) {
            if (offer.type == reqType) {
                typeOffers.push_back(&offer);
            }
        }

        if (typeOffers.empty()) continue;

        // Ordenar por precio (merit order: menor a mayor)
        std::sort(typeOffers.begin(), typeOffers.end(),
                  [](const AncillaryServiceOffer* a, const AncillaryServiceOffer* b) {
                      return a->price < b->price;
                  });

        // Asignar por orden de precio hasta cubrir requerimiento
        double remaining = required;
        double clearingPrice = 0.0;

        for (auto* offer : typeOffers) {
            if (remaining <= 1e-6) break;

            double assigned = std::min(offer->capacity, remaining);
            offer->clearedAmount = assigned;
            clearingPrice = offer->price;
            remaining -= assigned;
        }

        // Asignar precio de clearing a todas las ofertas aceptadas
        double totalClearedType = 0.0;
        for (auto* offer : typeOffers) {
            if (offer->clearedAmount > 1e-6) {
                offer->clearingPrice = clearingPrice;
                offer->accepted = true;

                result.clearedServices.push_back(*offer);
                totalClearedType += offer->clearedAmount;
                result.totalCost += offer->clearedAmount * clearingPrice;
            }
        }

        result.clearingPrices[reqType] = clearingPrice;
        result.totalCleared[reqType] = totalClearedType;
    }

    return result;
}

// ============================================================================
// Generar invoice de generador
// ============================================================================

SettlementInvoice SettlementCalculator::generateGeneratorInvoice(
    int genId, const std::string& genName,
    const std::map<std::string, double>& mWhByPeriod,
    bool isNodal) {

    SettlementInvoice invoice;
    invoice.entityId = genId;
    invoice.entityName = genName;
    invoice.entityType = "GENERADOR";
    invoice.period = m_period;
    invoice.isNodal = isNodal;

    double lmp = getLMPForBus(genId);
    const LMPResult* lmpResult = getLMPResultForBus(genId);

    for (const auto& [period, mWh] : mWhByPeriod) {
        if (mWh <= 0.0) continue;

        SettlementItem item;
        item.period = period;

        if (isNodal && lmpResult != nullptr) {
            // Precio nodal: descomponer en componentes
            item.description = "Energia (Nodal) - Bus " + std::to_string(genId);
            item.quantity = mWh;
            item.unitPrice = lmp;
            item.amount = mWh * lmp;

            invoice.totalEnergy += mWh;
            invoice.subtotal += item.amount;
        } else {
            // Precio zonal
            item.description = "Energia (Zonal) - Bus " + std::to_string(genId);
            item.quantity = mWh;
            item.unitPrice = lmp;
            item.amount = mWh * lmp;

            invoice.totalEnergy += mWh;
            invoice.subtotal += item.amount;
        }

        invoice.items.push_back(item);
    }

    // Totales
    invoice.tax = calculateTax(invoice.subtotal);
    invoice.total = invoice.subtotal + invoice.tax;

    m_invoices.push_back(invoice);
    return invoice;
}

// ============================================================================
// Generar invoice de carga
// ============================================================================

SettlementInvoice SettlementCalculator::generateLoadInvoice(
    int loadId, const std::string& loadName,
    const std::map<std::string, double>& mWhByPeriod,
    bool isNodal) {

    SettlementInvoice invoice;
    invoice.entityId = loadId;
    invoice.entityName = loadName;
    invoice.entityType = "CARGA";
    invoice.period = m_period;
    invoice.isNodal = isNodal;

    double lmp = getLMPForBus(loadId);

    for (const auto& [period, mWh] : mWhByPeriod) {
        if (mWh <= 0.0) continue;

        SettlementItem item;
        item.period = period;
        item.quantity = mWh;
        item.unitPrice = lmp;
        item.amount = mWh * lmp;

        if (isNodal) {
            item.description = "Energia Consumida (Nodal) - Bus " +
                               std::to_string(loadId);
        } else {
            item.description = "Energia Consumida (Zonal) - Bus " +
                               std::to_string(loadId);
        }

        invoice.items.push_back(item);
        invoice.totalEnergy += mWh;
        invoice.subtotal += item.amount;
    }

    invoice.tax = calculateTax(invoice.subtotal);
    invoice.total = invoice.subtotal + invoice.tax;

    m_invoices.push_back(invoice);
    return invoice;
}

// ============================================================================
// Generar invoice combinado (energia + auxiliares)
// ============================================================================

SettlementInvoice SettlementCalculator::generateCombinedInvoice(
    int entityId, const std::string& entityName,
    const SettlementInvoice& energyInvoice,
    const std::vector<AncillaryServiceOffer>& ancillaryServices) {

    SettlementInvoice invoice = energyInvoice;
    invoice.entityId = entityId;
    invoice.entityName = entityName;

    // Agregar items de servicios auxiliares
    for (const auto& service : ancillaryServices) {
        if (service.clearedAmount <= 1e-6) continue;

        SettlementItem item;
        item.description = "Servicio Auxiliar - " +
                           serviceTypeToString(service.type);
        item.quantity = service.clearedAmount;
        item.unitPrice = service.clearingPrice;
        item.amount = service.clearedAmount * service.clearingPrice;
        item.period = m_period;

        invoice.items.push_back(item);
        invoice.totalAncillary += item.amount;
    }

    // Recalcular totales
    invoice.subtotal = invoice.subtotal + invoice.totalAncillary;
    invoice.tax = calculateTax(invoice.subtotal);
    invoice.total = invoice.subtotal + invoice.tax;

    m_invoices.push_back(invoice);
    return invoice;
}

// ============================================================================
// Invoice a string
// ============================================================================

std::string SettlementCalculator::invoiceToString(
    const SettlementInvoice& invoice) const {

    std::stringstream report;

    report << "============================================================\n";
    report << "                    INVOICE DE SETTLEMENT\n";
    report << "============================================================\n";
    report << "Entidad: " << invoice.entityName
           << " (ID: " << invoice.entityId << ")\n";
    report << "Tipo:    " << invoice.entityType << "\n";
    report << "Periodo: " << invoice.period << "\n";
    report << "Precio:  " << (invoice.isNodal ? "NODAL" : "ZONAL");
    if (!invoice.pricingZone.empty()) {
        report << " (Zona: " << invoice.pricingZone << ")";
    }
    report << "\n";
    report << "------------------------------------------------------------\n";
    report << "  Descripcion                    | Cant.  |  Precio  |  Monto\n";
    report << "------------------------------------------------------------\n";

    for (const auto& item : invoice.items) {
        report << "  " << std::left << std::setw(30)
               << item.description.substr(0, 30) << " | "
               << std::right << std::setw(6) << std::fixed << std::setprecision(1)
               << item.quantity << " | "
               << std::setw(8) << std::setprecision(2) << item.unitPrice << " | "
               << std::setw(10) << std::setprecision(2) << item.amount << "\n";
    }

    report << "------------------------------------------------------------\n";
    report << std::right;
    report << "  Subtotal:                           "
           << std::setw(25) << std::setprecision(2) << invoice.subtotal << "\n";
    report << "  Impuestos:                          "
           << std::setw(25) << invoice.tax << "\n";
    report << "  TOTAL:                              "
           << std::setw(25) << invoice.total << "\n";
    report << "============================================================\n";

    return report.str();
}

// ============================================================================
// Tipo de servicio a string
// ============================================================================

std::string SettlementCalculator::serviceTypeToString(AncillaryServiceType type) {
    switch (type) {
        case AncillaryServiceType::SPINNING_RESERVE:
            return "Reserva Girante";
        case AncillaryServiceType::NON_SPINNING_RESERVE:
            return "Reserva No Girante";
        case AncillaryServiceType::REGULATION_UP:
            return "Regulacion (Subida)";
        case AncillaryServiceType::REGULATION_DOWN:
            return "Regulacion (Bajada)";
        case AncillaryServiceType::REACTIVE_SUPPORT:
            return "Soporte Reactivo";
        case AncillaryServiceType::BLACK_START:
            return "Arranque en Negro";
        case AncillaryServiceType::VOLTAGE_SUPPORT:
            return "Soporte de Voltaje";
        default:
            return "Desconocido";
    }
}

// ============================================================================
// Getters
// ============================================================================

const std::vector<SettlementInvoice>& SettlementCalculator::getAllInvoices() const {
    return m_invoices;
}

double SettlementCalculator::getPeriodTotal() const {
    double total = 0.0;
    for (const auto& invoice : m_invoices) {
        total += invoice.total;
    }
    return total;
}

} // namespace powsys365
