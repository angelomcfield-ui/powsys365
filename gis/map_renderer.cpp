#include "map_renderer.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iostream>
#include <curl/curl.h>

namespace powsys365::gis {

// ============================================================================
// Utilidades
// ============================================================================

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::vector<uint8_t>* data) {
    size_t totalSize = size * nmemb;
    data->insert(data->end(), static_cast<uint8_t*>(contents), static_cast<uint8_t*>(contents) + totalSize);
    return totalSize;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

MapRenderer::MapRenderer() : baseLayer_(BaseLayerType::OSM), cacheLimit_(500), timeoutMs_(10000) {
    urlTemplates_[BaseLayerType::OSM] = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    urlTemplates_[BaseLayerType::SATELLITE] = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}";
    urlTemplates_[BaseLayerType::TERRAIN] = "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png";
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

MapRenderer::~MapRenderer() {
    curl_global_cleanup();
}

// ============================================================================
// Configuracion
// ============================================================================

void MapRenderer::setBaseLayer(BaseLayerType type) {
    baseLayer_ = type;
}

BaseLayerType MapRenderer::getBaseLayer() const {
    return baseLayer_;
}

void MapRenderer::setTileCacheLimit(size_t maxTiles) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheLimit_ = maxTiles;
    // Limpiar cache si excede el nuevo limite
    while (tileCache_.size() > cacheLimit_) {
        tileCache_.erase(tileCache_.begin());
    }
}

void MapRenderer::setDownloadTimeoutMs(int ms) {
    timeoutMs_ = ms;
}

void MapRenderer::setTileUrlTemplate(const std::string& urlTemplate) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    urlTemplates_[baseLayer_] = urlTemplate;
}

// ============================================================================
// Carga de tiles
// ============================================================================

MapTile MapRenderer::loadTile(int zoom, int x, int y) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto key = std::make_tuple(zoom, x, y);
    auto it = tileCache_.find(key);
    if (it != tileCache_.end()) {
        return it->second;
    }

    MapTile tile;
    tile.zoom = zoom;
    tile.x = x;
    tile.y = y;
    tile.timestamp = std::chrono::steady_clock::now();

    try {
        tile.imageData = downloadTile(zoom, x, y);
        tile.isValid = !tile.imageData.empty();
    } catch (...) {
        tile.isValid = false;
    }

    // Almacenar en cache
    if (tileCache_.size() >= cacheLimit_) {
        tileCache_.erase(tileCache_.begin());
    }
    tileCache_[key] = tile;

    return tile;
}

std::future<MapTile> MapRenderer::loadTileAsync(int zoom, int x, int y) {
    return std::async(std::launch::async, [this, zoom, x, y]() {
        return this->loadTile(zoom, x, y);
    });
}

// ============================================================================
// Descarga de tiles via HTTP (libcurl)
// ============================================================================

std::string MapRenderer::buildTileUrl(int zoom, int x, int y) const {
    auto it = urlTemplates_.find(baseLayer_);
    if (it == urlTemplates_.end()) {
        return "";
    }
    std::string url = it->second;
    size_t pos;
    pos = url.find("{z}"); if (pos != std::string::npos) url.replace(pos, 3, std::to_string(zoom));
    pos = url.find("{x}"); if (pos != std::string::npos) url.replace(pos, 3, std::to_string(x));
    pos = url.find("{y}"); if (pos != std::string::npos) url.replace(pos, 3, std::to_string(y));
    pos = url.find("{s}"); if (pos != std::string::npos) url.replace(pos, 3, "a");
    return url;
}

std::vector<uint8_t> MapRenderer::downloadTile(int zoom, int x, int y) {
    std::vector<uint8_t> data;
    std::string url = buildTileUrl(zoom, x, y);
    if (url.empty()) return data;

    CURL* curl = curl_easy_init();
    if (!curl) return data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "POWSYS365-GIS/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) {
        data.clear();
    }

    return data;
}

// ============================================================================
// Cache
// ============================================================================

void MapRenderer::clearTileCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    tileCache_.clear();
}

size_t MapRenderer::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return tileCache_.size();
}

// ============================================================================
// Conversiones lat/lon <-> tile
// ============================================================================

void MapRenderer::latLonToTile(double lat, double lon, int zoom, int& tx, int& ty) {
    double latRad = lat * M_PI / 180.0;
    int n = 1 << zoom;
    tx = static_cast<int>((lon + 180.0) / 360.0 * n);
    ty = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);
}

void MapRenderer::tileToLatLon(int tx, int ty, int zoom, double& lat, double& lon) {
    int n = 1 << zoom;
    lon = static_cast<double>(tx) / n * 360.0 - 180.0;
    double latRad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * static_cast<double>(ty) / n)));
    lat = latRad * 180.0 / M_PI;
}

// ============================================================================
// Proyeccion geo -> pixel
// ============================================================================

void MapRenderer::geoToPixel(double lat, double lon, const Viewport& vp,
                             int& px, int& py) const {
    int n = 1 << vp.zoom;
    double latRad = lat * M_PI / 180.0;
    double xtile = (lon + 180.0) / 360.0 * n;
    double ytile = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n;

    double centerLatRad = vp.centerLat * M_PI / 180.0;
    double xCenter = (vp.centerLon + 180.0) / 360.0 * n;
    double yCenter = (1.0 - std::log(std::tan(centerLatRad) + 1.0 / std::cos(centerLatRad)) / M_PI) / 2.0 * n;

    px = static_cast<int>((xtile - xCenter) * 256.0) + vp.pixelWidth / 2;
    py = static_cast<int>((ytile - yCenter) * 256.0) + vp.pixelHeight / 2;
}

// ============================================================================
// Dibujo de primitivas graficas (RGBA8888)
// ============================================================================

void MapRenderer::setPixel(std::vector<uint8_t>& buffer, int w, int h,
                           int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int idx = (y * w + x) * 4;
    uint8_t oldA = buffer[idx + 3];
    uint8_t newA = a;
    uint8_t outA = std::min(255, oldA + newA * (255 - oldA) / 255);
    if (outA > 0) {
        buffer[idx]     = (buffer[idx]     * oldA * (255 - newA) / 255 + r * newA) / outA;
        buffer[idx + 1] = (buffer[idx + 1] * oldA * (255 - newA) / 255 + g * newA) / outA;
        buffer[idx + 2] = (buffer[idx + 2] * oldA * (255 - newA) / 255 + b * newA) / outA;
        buffer[idx + 3] = outA;
    }
}

void MapRenderer::drawCircle(std::vector<uint8_t>& buffer, int w, int h,
                             int cx, int cy, int radius,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                setPixel(buffer, w, h, cx + dx, cy + dy, r, g, b, a);
            }
        }
    }
}

void MapRenderer::drawLineSegment(std::vector<uint8_t>& buffer, int w, int h,
                                  int x0, int y0, int x1, int y1,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                  int thickness) const {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        // Dibujar con grosor
        for (int ty = -thickness / 2; ty <= thickness / 2; ++ty) {
            for (int tx = -thickness / 2; tx <= thickness / 2; ++tx) {
                setPixel(buffer, w, h, x0 + tx, y0 + ty, r, g, b, a);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void MapRenderer::fillRect(std::vector<uint8_t>& buffer, int w, int h,
                           int x0, int y0, int x1, int y1,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    int xStart = std::max(0, std::min(x0, x1));
    int xEnd   = std::min(w - 1, std::max(x0, x1));
    int yStart = std::max(0, std::min(y0, y1));
    int yEnd   = std::min(h - 1, std::max(y0, y1));
    for (int y = yStart; y <= yEnd; ++y) {
        for (int x = xStart; x <= xEnd; ++x) {
            setPixel(buffer, w, h, x, y, r, g, b, a);
        }
    }
}

// ============================================================================
// Dibujo de elementos de red electrica
// ============================================================================

void MapRenderer::drawBus(std::vector<uint8_t>& buffer, int width, int height,
                          const PowerBus& bus, const Viewport& vp) const {
    int px, py;
    geoToPixel(bus.location.latitude, bus.location.longitude, vp, px, py);

    // Color segun voltaje
    uint8_t r = 0, g = 255, b = 0;
    if (bus.voltageKv >= 400.0)      { r = 255; g = 0;   b = 0; }   // Extra alto
    else if (bus.voltageKv >= 220.0) { r = 255; g = 128; b = 0; }   // Alto
    else if (bus.voltageKv >= 132.0) { r = 255; g = 255; b = 0; }   // Medio
    else if (bus.voltageKv >= 66.0)  { r = 0;   g = 255; b = 0; }   // Bajo
    else                             { r = 0;   g = 128; b = 255; } // Distribucion

    int radius = bus.isSubstation ? 12 : 6;
    // Circulo exterior (borde negro)
    drawCircle(buffer, width, height, px, py, radius + 2, 0, 0, 0, 255);
    // Circulo interior (color)
    drawCircle(buffer, width, height, px, py, radius, r, g, b, 230);

    // Etiqueta del bus
    // (simplificado - en produccion se renderizaria texto)
    fillRect(buffer, width, height, px + radius + 2, py - 4, px + radius + 60, py + 4, 0, 0, 0, 180);
}

void MapRenderer::drawLine(std::vector<uint8_t>& buffer, int width, int height,
                           const PowerLine& line, const PowerSystem& sys,
                           const Viewport& vp) const {
    // Encontrar buses de origen y destino
    const PowerBus* from = nullptr;
    const PowerBus* to = nullptr;
    for (const auto& bus : sys.buses) {
        if (bus.busId == line.fromBus) from = &bus;
        if (bus.busId == line.toBus)   to = &bus;
    }
    if (!from || !to) return;

    // Color segun carga
    uint8_t r = 0, g = 200, b = 0;
    double loadRatio = std::abs(line.powerMw) / 100.0; // Asumiendo 100MW nominal
    if (loadRatio > 0.9)      { r = 255; g = 0;   b = 0; }
    else if (loadRatio > 0.7) { r = 255; g = 100; b = 0; }
    else if (loadRatio > 0.5) { r = 255; g = 255; b = 0; }

    int thickness = line.voltageKv >= 400.0 ? 4 :
                    line.voltageKv >= 220.0 ? 3 :
                    line.voltageKv >= 132.0 ? 2 : 1;

    int x0, y0, x1, y1;
    geoToPixel(from->location.latitude, from->location.longitude, vp, x0, y0);
    geoToPixel(to->location.latitude, to->location.longitude, vp, x1, y1);

    // Si hay waypoints, dibujar segmento a segmento
    if (!line.waypoints.empty()) {
        int prevX = x0, prevY = y0;
        for (const auto& wp : line.waypoints) {
            int wx, wy;
            geoToPixel(wp.latitude, wp.longitude, vp, wx, wy);
            drawLineSegment(buffer, width, height, prevX, prevY, wx, wy, r, g, b, 220, thickness);
            prevX = wx;
            prevY = wy;
        }
        drawLineSegment(buffer, width, height, prevX, prevY, x1, y1, r, g, b, 220, thickness);
    } else {
        drawLineSegment(buffer, width, height, x0, y0, x1, y1, r, g, b, 220, thickness);
    }
}

// ============================================================================
// Renderizado completo
// ============================================================================

std::vector<uint8_t> MapRenderer::renderNetwork(const PowerSystem& sys, const Viewport& vp) const {
    // Crear buffer RGBA transparente
    std::vector<uint8_t> buffer(vp.pixelWidth * vp.pixelHeight * 4, 0);

    // Dibujar lineas primero (detras de los buses)
    for (const auto& line : sys.lines) {
        drawLine(buffer, vp.pixelWidth, vp.pixelHeight, line, sys, vp);
    }

    // Dibujar buses encima
    for (const auto& bus : sys.buses) {
        drawBus(buffer, vp.pixelWidth, vp.pixelHeight, bus, vp);
    }

    return buffer;
}

std::vector<uint8_t> MapRenderer::renderMapWithOverlay(const Viewport& vp, const PowerSystem& sys) const {
    // Calcular tiles necesarios
    int centerTx, centerTy;
    latLonToTile(vp.centerLat, vp.centerLon, vp.zoom, centerTx, centerTy);

    int tilesX = (vp.pixelWidth / 256) + 2;
    int tilesY = (vp.pixelHeight / 256) + 2;

    // Buffer resultante
    std::vector<uint8_t> result(vp.pixelWidth * vp.pixelHeight * 4, 0);

    // Cargar y componer tiles base
    for (int dy = -tilesY / 2; dy <= tilesY / 2; ++dy) {
        for (int dx = -tilesX / 2; dx <= tilesX / 2; ++dx) {
            int tx = centerTx + dx;
            int ty = centerTy + dy;

            MapTile tile = const_cast<MapRenderer*>(this)->loadTile(vp.zoom, tx, ty);
            if (!tile.isValid || tile.imageData.empty()) continue;

            // Posicion del tile en pixeles del viewport
            double tileLat, tileLon;
            tileToLatLon(tx, ty, vp.zoom, tileLat, tileLon);
            int tilePx, tilePy;
            geoToPixel(tileLat, tileLon, vp, tilePx, tilePy);

            // Decodificar PNG y blit al buffer
            // Simplificado: en produccion se usaria libpng
            // Aqui usamos un placeholder gris para tiles no descargados
        }
    }

    // Si no hay tiles base, fondo gris oscuro
    for (size_t i = 0; i < result.size(); i += 4) {
        if (result[i + 3] == 0) {
            result[i]     = 40;
            result[i + 1] = 44;
            result[i + 2] = 52;
            result[i + 3] = 255;
        }
    }

    // Renderizar overlay de red
    std::vector<uint8_t> overlay = renderNetwork(sys, vp);

    // Componer overlay sobre el fondo
    for (size_t i = 0; i < overlay.size(); i += 4) {
        if (overlay[i + 3] > 0) {
            uint8_t oa = overlay[i + 3];
            uint8_t ba = result[i + 3];
            uint8_t outA = std::min(255, ba + oa * (255 - ba) / 255);
            if (outA > 0) {
                result[i]     = (result[i]     * ba * (255 - oa) / 255 + overlay[i]     * oa) / outA;
                result[i + 1] = (result[i + 1] * ba * (255 - oa) / 255 + overlay[i + 1] * oa) / outA;
                result[i + 2] = (result[i + 2] * ba * (255 - oa) / 255 + overlay[i + 2] * oa) / outA;
                result[i + 3] = outA;
            }
        }
    }

    return result;
}

std::vector<uint8_t> MapRenderer::composeOverlay(const std::vector<uint8_t>& baseTile,
                                                 const PowerSystem& sys,
                                                 const Viewport& vp) const {
    return renderNetwork(sys, vp);
}

} // namespace powsys365::gis
