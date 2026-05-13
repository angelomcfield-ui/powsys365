#pragma once

#include "coordinate_converter.h"
#include "map_renderer.h"
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <iomanip>

namespace powsys365::gis {

// ============================================================================
// KmlExporter - Exportacion de redes electricas a KML
// ============================================================================

class KmlExporter {
public:
    KmlExporter();

    // --- Exportacion completa ---
    std::string exportToString(const PowerSystem& sys) const;
    bool        exportToFile(const PowerSystem& sys, const std::string& filename) const;

    // --- Estilos configurables ---
    void setBusIconUrl(const std::string& url);
    void setLineWidth(double width);
    void setBusColor(const std::string& hexColor);   // formato "FF0000FF"
    void setLineColor(const std::string& hexColor);  // formato "FF00FF00"

    // --- Opciones de exportacion ---
    void setIncludeElevations(bool include);
    void setUseAbsoluteAltitude(bool absolute);
    void setDocumentName(const std::string& name);

private:
    std::string busIconUrl_;
    double      lineWidth_;
    std::string busColor_;
    std::string lineColor_;
    bool        includeElevations_;
    bool        useAbsoluteAltitude_;
    std::string documentName_;

    // Generar KML parciales
    std::string generateHeader() const;
    std::string generateStyles() const;
    std::string generateBusPlacemark(const PowerBus& bus) const;
    std::string generateLinePlacemark(const PowerLine& line, const PowerSystem& sys) const;
    std::string generateFolderBuses(const PowerSystem& sys) const;
    std::string generateFolderLines(const PowerSystem& sys) const;
    std::string generateFooter() const;

    // Escapar XML
    static std::string escapeXml(const std::string& text);

    // Color por voltaje para lineas
    static std::string voltageToColor(double voltageKv);

    // Altitud segun configuracion
    std::string altitudeMode() const;
};

} // namespace powsys365::gis
