#include "kml_exporter.h"
#include <iomanip>

namespace powsys365::gis {

// ============================================================================
// Constructor
// ============================================================================

KmlExporter::KmlExporter()
    : busIconUrl_("http://maps.google.com/mapfiles/kml/shapes/substation.png")
    , lineWidth_(3.0)
    , busColor_("FFFF0000")
    , lineColor_("FF00FF00")
    , includeElevations_(true)
    , useAbsoluteAltitude_(true)
    , documentName_("POWSYS365 Network") {}

// ============================================================================
// Configuracion
// ============================================================================

void KmlExporter::setBusIconUrl(const std::string& url) {
    busIconUrl_ = url;
}

void KmlExporter::setLineWidth(double width) {
    lineWidth_ = width;
}

void KmlExporter::setBusColor(const std::string& hexColor) {
    busColor_ = hexColor;
}

void KmlExporter::setLineColor(const std::string& hexColor) {
    lineColor_ = hexColor;
}

void KmlExporter::setIncludeElevations(bool include) {
    includeElevations_ = include;
}

void KmlExporter::setUseAbsoluteAltitude(bool absolute) {
    useAbsoluteAltitude_ = absolute;
}

void KmlExporter::setDocumentName(const std::string& name) {
    documentName_ = name;
}

// ============================================================================
// Escapar XML
// ============================================================================

std::string KmlExporter::escapeXml(const std::string& text) {
    std::string result;
    result.reserve(text.size() * 2);
    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c;         break;
        }
    }
    return result;
}

// ============================================================================
// Color por voltaje
// ============================================================================

std::string KmlExporter::voltageToColor(double voltageKv) {
    // KML usa aabbggrr (alpha, blue, green, red)
    if (voltageKv >= 400.0)      return "FF0000FF"; // Rojo (extra alto)
    if (voltageKv >= 220.0)      return "FF0066FF"; // Naranja (alto)
    if (voltageKv >= 132.0)      return "FF00AAFF"; // Amarillo-naranja
    if (voltageKv >= 66.0)       return "FF00FF00"; // Verde (medio)
    if (voltageKv >= 33.0)       return "FFFFFF00"; // Cyan
    if (voltageKv >= 11.0)       return "FFFF0000"; // Azul (baja tension)
    return "FFFFFFFF"; // Blanco (muy baja)
}

std::string KmlExporter::altitudeMode() const {
    if (useAbsoluteAltitude_) return "absolute";
    return "relativeToGround";
}

// ============================================================================
// Generadores KML
// ============================================================================

std::string KmlExporter::generateHeader() const {
    std::ostringstream kml;
    kml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    kml << "<kml xmlns=\"http://www.opengis.net/kml/2.2\"\n";
    kml << "     xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n";
    kml << "<Document>\n";
    kml << "  <name>" << escapeXml(documentName_) << "</name>\n";
    kml << "  <description>Red electrica generada por POWSYS365 GIS</description>\n";
    kml << "  <open>1</open>\n";
    return kml.str();
}

std::string KmlExporter::generateStyles() const {
    std::ostringstream kml;
    kml << "  <Style id=\"busStyle\">\n";
    kml << "    <IconStyle>\n";
    kml << "      <color>" << busColor_ << "</color>\n";
    kml << "      <scale>1.2</scale>\n";
    kml << "      <Icon>\n";
    kml << "        <href>" << escapeXml(busIconUrl_) << "</href>\n";
    kml << "      </Icon>\n";
    kml << "    </IconStyle>\n";
    kml << "    <LabelStyle>\n";
    kml << "      <scale>0.9</scale>\n";
    kml << "    </LabelStyle>\n";
    kml << "    <BalloonStyle>\n";
    kml << "      <text>\n";
    kml << "        <![CDATA[\n";
    kml << "          <h3>$[name]</h3>\n";
    kml << "          <p><b>ID:</b> $[id]<br/>\n";
    kml << "          <b>Voltaje:</b> $[voltage] kV<br/>\n";
    kml << "          <b>Capacidad:</b> $[capacity] MW<br/>\n";
    kml << "          <b>Tipo:</b> $[type]</p>\n";
    kml << "        ]]>\n";
    kml << "      </text>\n";
    kml << "    </BalloonStyle>\n";
    kml << "  </Style>\n";

    // Estilos por voltaje para lineas
    std::vector<std::pair<std::string, std::string>> lineStyles = {
        {"line400",  "FF0000FF"},
        {"line220",  "FF0066FF"},
        {"line132",  "FF00AAFF"},
        {"line66",   "FF00FF00"},
        {"line33",   "FFFFFF00"},
        {"line11",   "FFFF0000"},
        {"lineLow",  "FFFFFFFF"}
    };

    for (const auto& style : lineStyles) {
        kml << "  <Style id=\"" << style.first << "\">\n";
        kml << "    <LineStyle>\n";
        kml << "      <color>" << style.second << "</color>\n";
        kml << "      <width>" << std::fixed << std::setprecision(1) << lineWidth_ << "</width>\n";
        kml << "    </LineStyle>\n";
        kml << "    <BalloonStyle>\n";
        kml << "      <text>\n";
        kml << "        <![CDATA[\n";
        kml << "          <h3>$[name]</h3>\n";
        kml << "          <p><b>ID:</b> $[id]<br/>\n";
        kml << "          <b>Voltaje:</b> $[voltage] kV<br/>\n";
        kml << "          <b>Potencia:</b> $[power] MW<br/>\n";
        kml << "          <b>Desde:</b> $[fromBus] &rarr; <b>Hasta:</b> $[toBus]</p>\n";
        kml << "        ]]>\n";
        kml << "      </text>\n";
        kml << "    </BalloonStyle>\n";
        kml << "  </Style>\n";
    }

    return kml.str();
}

std::string KmlExporter::generateBusPlacemark(const PowerBus& bus) const {
    std::ostringstream kml;
    kml << "    <Placemark>\n";
    kml << "      <name>" << escapeXml(bus.name) << "</name>\n";
    kml << "      <styleUrl>#busStyle</styleUrl>\n";

    // ExtendedData para el balloon
    kml << "      <ExtendedData>\n";
    kml << "        <Data name=\"id\"><value>" << bus.busId << "</value></Data>\n";
    kml << "        <Data name=\"voltage\"><value>" << bus.voltageKv << "</value></Data>\n";
    kml << "        <Data name=\"capacity\"><value>" << bus.capacityMw << "</value></Data>\n";
    kml << "        <Data name=\"type\"><value>" << (bus.isSubstation ? "Subestacion" : "Bus") << "</value></Data>\n";
    kml << "      </ExtendedData>\n";

    kml << "      <Point>\n";
    kml << "        <altitudeMode>" << altitudeMode() << "</altitudeMode>\n";
    kml << "        <coordinates>";
    kml << std::fixed << std::setprecision(6) << bus.location.longitude << ","
        << bus.location.latitude;
    if (includeElevations_) {
        kml << "," << bus.location.elevation;
    } else {
        kml << ",0";
    }
    kml << "</coordinates>\n";
    kml << "      </Point>\n";
    kml << "    </Placemark>\n";
    return kml.str();
}

std::string KmlExporter::generateLinePlacemark(const PowerLine& line, const PowerSystem& sys) const {
    std::ostringstream kml;

    // Encontrar nombres de los buses
    std::string fromName = "Bus " + std::to_string(line.fromBus);
    std::string toName   = "Bus " + std::to_string(line.toBus);
    for (const auto& bus : sys.buses) {
        if (bus.busId == line.fromBus) fromName = bus.name;
        if (bus.busId == line.toBus)   toName   = bus.name;
    }

    // Determinar estilo
    std::string styleId;
    if (line.voltageKv >= 400.0)       styleId = "line400";
    else if (line.voltageKv >= 220.0)  styleId = "line220";
    else if (line.voltageKv >= 132.0)  styleId = "line132";
    else if (line.voltageKv >= 66.0)   styleId = "line66";
    else if (line.voltageKv >= 33.0)   styleId = "line33";
    else if (line.voltageKv >= 11.0)   styleId = "line11";
    else                               styleId = "lineLow";

    kml << "    <Placemark>\n";
    kml << "      <name>" << escapeXml(line.name) << "</name>\n";
    kml << "      <styleUrl>#" << styleId << "</styleUrl>\n";

    // ExtendedData
    kml << "      <ExtendedData>\n";
    kml << "        <Data name=\"id\"><value>" << line.lineId << "</value></Data>\n";
    kml << "        <Data name=\"voltage\"><value>" << line.voltageKv << "</value></Data>\n";
    kml << "        <Data name=\"power\"><value>" << line.powerMw << "</value></Data>\n";
    kml << "        <Data name=\"fromBus\"><value>" << escapeXml(fromName) << "</value></Data>\n";
    kml << "        <Data name=\"toBus\"><value>" << escapeXml(toName) << "</value></Data>\n";
    kml << "      </ExtendedData>\n";

    kml << "      <LineString>\n";
    kml << "        <altitudeMode>" << altitudeMode() << "</altitudeMode>\n";
    kml << "        <tessellate>1</tessellate>\n";
    kml << "        <coordinates>\n";

    // Coordenadas del bus origen
    const PowerBus* fromBus = nullptr;
    const PowerBus* toBus = nullptr;
    for (const auto& bus : sys.buses) {
        if (bus.busId == line.fromBus) fromBus = &bus;
        if (bus.busId == line.toBus)   toBus   = &bus;
    }

    if (fromBus) {
        kml << "          " << std::fixed << std::setprecision(6)
            << fromBus->location.longitude << ","
            << fromBus->location.latitude << ",";
        if (includeElevations_) kml << fromBus->location.elevation;
        else kml << "0";
        kml << "\n";
    }

    // Waypoints intermedios
    for (const auto& wp : line.waypoints) {
        kml << "          " << wp.longitude << "," << wp.latitude << ",";
        if (includeElevations_) kml << wp.elevation;
        else kml << "0";
        kml << "\n";
    }

    if (toBus) {
        kml << "          " << toBus->location.longitude << ","
            << toBus->location.latitude << ",";
        if (includeElevations_) kml << toBus->location.elevation;
        else kml << "0";
        kml << "\n";
    }

    kml << "        </coordinates>\n";
    kml << "      </LineString>\n";
    kml << "    </Placemark>\n";
    return kml.str();
}

std::string KmlExporter::generateFolderBuses(const PowerSystem& sys) const {
    std::ostringstream kml;
    kml << "  <Folder>\n";
    kml << "    <name>Buses y Subestaciones</name>\n";
    kml << "    <visibility>1</visibility>\n";
    kml << "    <open>1</open>\n";

    // Separar substaciones y buses simples
    std::vector<const PowerBus*> substations;
    std::vector<const PowerBus*> simpleBuses;
    for (const auto& bus : sys.buses) {
        if (bus.isSubstation) substations.push_back(&bus);
        else simpleBuses.push_back(&bus);
    }

    // Subestaciones
    if (!substations.empty()) {
        kml << "    <Folder>\n";
        kml << "      <name>Subestaciones</name>\n";
        for (const auto* bus : substations) {
            kml << generateBusPlacemark(*bus);
        }
        kml << "    </Folder>\n";
    }

    // Buses simples
    if (!simpleBuses.empty()) {
        kml << "    <Folder>\n";
        kml << "      <name>Buses</name>\n";
        for (const auto* bus : simpleBuses) {
            kml << generateBusPlacemark(*bus);
        }
        kml << "    </Folder>\n";
    }

    kml << "  </Folder>\n";
    return kml.str();
}

std::string KmlExporter::generateFolderLines(const PowerSystem& sys) const {
    std::ostringstream kml;
    kml << "  <Folder>\n";
    kml << "    <name>Lineas de Transmision</name>\n";
    kml << "    <visibility>1</visibility>\n";

    // Agrupar por voltaje
    std::map<double, std::vector<const PowerLine*>> grouped;
    for (const auto& line : sys.lines) {
        grouped[line.voltageKv].push_back(&line);
    }

    for (auto it = grouped.rbegin(); it != grouped.rend(); ++it) {
        kml << "    <Folder>\n";
        kml << "      <name>" << it->first << " kV</name>\n";
        for (const auto* line : it->second) {
            kml << generateLinePlacemark(*line, sys);
        }
        kml << "    </Folder>\n";
    }

    kml << "  </Folder>\n";
    return kml.str();
}

std::string KmlExporter::generateFooter() const {
    return "</Document>\n</kml>\n";
}

// ============================================================================
// Exportacion publica
// ============================================================================

std::string KmlExporter::exportToString(const PowerSystem& sys) const {
    std::ostringstream kml;
    kml << generateHeader();
    kml << generateStyles();
    kml << generateFolderBuses(sys);
    kml << generateFolderLines(sys);
    kml << generateFooter();
    return kml.str();
}

bool KmlExporter::exportToFile(const PowerSystem& sys, const std::string& filename) const {
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;
    file << exportToString(sys);
    file.close();
    return !file.fail();
}

} // namespace powsys365::gis
