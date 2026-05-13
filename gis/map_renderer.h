#pragma once

#include "coordinate_converter.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <future>
#include <chrono>

namespace powsys365::gis {

// ============================================================================
// Tipos de capa base
// ============================================================================

enum class BaseLayerType {
    OSM,        // OpenStreetMap
    SATELLITE,  // Imagen satelital
    TERRAIN     // Mapa de terreno
};

// ============================================================================
// Estructuras de la red electrica
// ============================================================================

struct PowerBus {
    int    busId;
    std::string name;
    double voltageKv;    // kV
    GeographicCoord location;
    bool   isSubstation; // true = subestacion, false = bus simple
    double capacityMw;   // Capacidad en MW
};

struct PowerLine {
    int    lineId;
    std::string name;
    int    fromBus;
    int    toBus;
    double voltageKv;
    double currentA;
    double powerMw;
    std::vector<GeographicCoord> waypoints; // puntos intermedios
};

struct PowerSystem {
    std::string                name;
    std::vector<PowerBus>      buses;
    std::vector<PowerLine>     lines;
};

// ============================================================================
// Tile y cache
// ============================================================================

struct MapTile {
    int    zoom;
    int    x;
    int    y;
    std::vector<uint8_t> imageData; // datos raw de la imagen (PNG/JPEG)
    std::chrono::steady_clock::time_point timestamp;
    bool   isValid = false;
};

struct Viewport {
    double centerLat;
    double centerLon;
    int    zoom;
    int    pixelWidth;
    int    pixelHeight;
};

// ============================================================================
// MapRenderer
// ============================================================================

class MapRenderer {
public:
    MapRenderer();
    ~MapRenderer();

    // --- Carga de tiles ---
    MapTile loadTile(int zoom, int x, int y);

    // --- Prefetch asincrono ---
    std::future<MapTile> loadTileAsync(int zoom, int x, int y);

    // --- Capa base ---
    void setBaseLayer(BaseLayerType type);
    BaseLayerType getBaseLayer() const;

    // --- Renderizado de la red ---
    std::vector<uint8_t> renderNetwork(const PowerSystem& sys, const Viewport& vp) const;

    // --- Renderizado completo (capa base + overlay) ---
    std::vector<uint8_t> renderMapWithOverlay(const Viewport& vp, const PowerSystem& sys) const;

    // --- Cache ---
    void clearTileCache();
    size_t getCacheSize() const;

    // --- Configuracion ---
    void setTileCacheLimit(size_t maxTiles);
    void setDownloadTimeoutMs(int ms);
    void setTileUrlTemplate(const std::string& urlTemplate);

private:
    BaseLayerType baseLayer_;
    mutable std::mutex cacheMutex_;
    std::map<std::tuple<int, int, int>, MapTile> tileCache_; // key = (zoom, x, y)
    size_t cacheLimit_ = 500;
    int    timeoutMs_ = 10000;

    // Templates de URL para cada tipo de capa
    std::map<BaseLayerType, std::string> urlTemplates_;

    // Convierte coordenadas lat/lon a tile XYZ
    static void latLonToTile(double lat, double lon, int zoom, int& tx, int& ty);
    static void tileToLatLon(int tx, int ty, int zoom, double& lat, double& lon);

    // Descarga un tile via HTTP
    std::vector<uint8_t> downloadTile(int zoom, int x, int y);

    // Genera la URL del tile
    std::string buildTileUrl(int zoom, int x, int y) const;

    // Renderiza la capa de overlay sobre el tile
    std::vector<uint8_t> composeOverlay(const std::vector<uint8_t>& baseTile,
                                       const PowerSystem& sys,
                                       const Viewport& vp) const;

    // Proyecta coordenadas geograficas a pixeles en el viewport
    void geoToPixel(double lat, double lon, const Viewport& vp,
                    int& px, int& py) const;

    // Dibuja un bus en el buffer de imagen
    void drawBus(std::vector<uint8_t>& buffer, int width, int height,
                 const PowerBus& bus, const Viewport& vp) const;

    // Dibuja una linea en el buffer de imagen
    void drawLine(std::vector<uint8_t>& buffer, int width, int height,
                  const PowerLine& line, const PowerSystem& sys,
                  const Viewport& vp) const;

    // Utilidades de dibujo
    void drawCircle(std::vector<uint8_t>& buffer, int w, int h,
                    int cx, int cy, int radius,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;
    void drawLineSegment(std::vector<uint8_t>& buffer, int w, int h,
                         int x0, int y0, int x1, int y1,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                         int thickness) const;
    void setPixel(std::vector<uint8_t>& buffer, int w, int h,
                  int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;
    void fillRect(std::vector<uint8_t>& buffer, int w, int h,
                  int x0, int y0, int x1, int y1,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;
};

} // namespace powsys365::gis
