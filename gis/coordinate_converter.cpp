#include "coordinate_converter.h"
#include <cmath>

namespace powsys365::gis {

// ============================================================================
// Utilidades de angulos
// ============================================================================

inline double CoordinateConverter::deg2rad(double deg) {
    return deg * M_PI / 180.0;
}

inline double CoordinateConverter::rad2deg(double rad) {
    return rad * 180.0 / M_PI;
}

// ============================================================================
// Constructor
// ============================================================================

CoordinateConverter::CoordinateConverter() {}

// ============================================================================
// Conversion Geodesica <-> Cartesianas Geocentricas
// ============================================================================

void CoordinateConverter::geoToCartesian(double lat, double lon, double h,
                                        double a, double f,
                                        double& x, double& y, double& z) const {
    const double e2 = 2.0 * f - f * f;
    const double sinLat = std::sin(lat);
    const double cosLat = std::cos(lat);
    const double sinLon = std::sin(lon);
    const double cosLon = std::cos(lon);
    const double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);

    x = (N + h) * cosLat * cosLon;
    y = (N + h) * cosLat * sinLon;
    z = (N * (1.0 - e2) + h) * sinLat;
}

void CoordinateConverter::cartesianToGeo(double x, double y, double z,
                                        double a, double f,
                                        double& lat, double& lon, double& h) const {
    const double e2 = 2.0 * f - f * f;
    lon = std::atan2(y, x);

    const double p = std::sqrt(x * x + y * y);
    double latIter = std::atan2(z, p * (1.0 - e2));
    double latPrev;
    double N;

    // Iteracion de Bowring (max 10 iteraciones)
    for (int i = 0; i < 10; ++i) {
        latPrev = latIter;
        N = a / std::sqrt(1.0 - e2 * std::sin(latIter) * std::sin(latIter));
        h = p / std::cos(latIter) - N;
        latIter = std::atan2(z, p * (1.0 - e2 * N / (N + h)));
        if (std::abs(latIter - latPrev) < 1e-12) break;
    }

    lat = latIter;
    N = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    h = p / std::cos(lat) - N;
}

// ============================================================================
// Transformacion Helmert 7-parametros
// ============================================================================

void CoordinateConverter::helmertTransform(double x, double y, double z,
                                          const DatumShiftParams& p,
                                          double& xo, double& yo, double& zo) const {
    // Convertir segundos de arco a radianes
    const double rxRad = deg2rad(p.rx / 3600.0);
    const double ryRad = deg2rad(p.ry / 3600.0);
    const double rzRad = deg2rad(p.rz / 3600.0);
    const double dsFactor = 1.0 + p.ds / 1e6;

    xo = p.dx + dsFactor * (x + rzRad * y - ryRad * z);
    yo = p.dy + dsFactor * (-rzRad * x + y + rxRad * z);
    zo = p.dz + dsFactor * (ryRad * x - rxRad * y + z);
}

void CoordinateConverter::helmertInverse(double x, double y, double z,
                                        const DatumShiftParams& p,
                                        double& xo, double& yo, double& zo) const {
    // Inversa: aplicar Helmert con parametros negados
    DatumShiftParams inv;
    inv.dx = -p.dx;
    inv.dy = -p.dy;
    inv.dz = -p.dz;
    inv.rx = -p.rx;
    inv.ry = -p.ry;
    inv.rz = -p.rz;
    inv.ds = -p.ds; // Aproximacion valida para ds << 1
    helmertTransform(x, y, z, inv, xo, yo, zo);
}

// ============================================================================
// Proyeccion Transversal de Mercator Directa/Inversa
// ============================================================================

void CoordinateConverter::tmDirect(double lat, double lon, double lon0,
                                  double a, double f, double k0,
                                  double& easting, double& northing) const {
    const double e2 = 2.0 * f - f * f;
    const double e4 = e2 * e2;
    const double e6 = e4 * e2;

    const double n = f / (2.0 - f);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n3 * n;

    const double A = a / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0);

    const double alpha1 = n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0;
    const double alpha2 = 13.0 * n2 / 48.0 - 3.0 * n3 / 5.0;
    const double alpha3 = 61.0 * n3 / 240.0;

    const double t = std::sinh(std::atanh(std::sin(lat)) - 2.0 * std::sqrt(n) / (1.0 + n) *
                               std::atanh(2.0 * std::sqrt(n) / (1.0 + n) * std::sin(lat)));
    const double xiPrime = std::atan(t / std::cos(lon - lon0));
    const double etaPrime = std::atanh(std::sin(lon - lon0) / std::sqrt(1.0 + t * t));

    const double sigma = 1.0 + 2.0 * std::sin(2.0 * xiPrime) * std::cosh(2.0 * etaPrime) * alpha1
                        + 4.0 * std::sin(4.0 * xiPrime) * std::cosh(4.0 * etaPrime) * alpha2
                        + 6.0 * std::sin(6.0 * xiPrime) * std::cosh(6.0 * etaPrime) * alpha3;
    const double tau = 2.0 * std::cos(2.0 * xiPrime) * std::sinh(2.0 * etaPrime) * alpha1
                     + 4.0 * std::cos(4.0 * xiPrime) * std::sinh(4.0 * etaPrime) * alpha2
                     + 6.0 * std::cos(6.0 * xiPrime) * std::sinh(6.0 * etaPrime) * alpha3;

    easting = 500000.0 + k0 * A * (etaPrime + std::cos(2.0 * xiPrime) * std::sinh(2.0 * etaPrime) * alpha1
                          + std::cos(4.0 * xiPrime) * std::sinh(4.0 * etaPrime) * alpha2
                          + std::cos(6.0 * xiPrime) * std::sinh(6.0 * etaPrime) * alpha3);
    northing = k0 * A * (xiPrime + std::sin(2.0 * xiPrime) * std::cosh(2.0 * etaPrime) * alpha1
                + std::sin(4.0 * xiPrime) * std::cosh(4.0 * etaPrime) * alpha2
                + std::sin(6.0 * xiPrime) * std::cosh(6.0 * etaPrime) * alpha3);
}

void CoordinateConverter::tmInverse(double easting, double northing, double lon0,
                                   double a, double f, double k0,
                                   double& lat, double& lon) const {
    const double e2 = 2.0 * f - f * f;
    const double n = f / (2.0 - f);
    const double n2 = n * n;
    const double n3 = n2 * n;
    const double n4 = n3 * n;

    const double A = a / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0);

    const double beta1 = n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0;
    const double beta2 = n2 / 48.0 + n3 / 15.0;
    const double beta3 = 17.0 * n3 / 480.0;

    const double delta1 = 2.0 * n - 2.0 * n2 / 3.0 - 2.0 * n3;
    const double delta2 = 7.0 * n2 / 3.0 - 8.0 * n3 / 5.0;
    const double delta3 = 56.0 * n3 / 15.0;

    const double xi = northing / (k0 * A);
    const double eta = (easting - 500000.0) / (k0 * A);

    const double xiPrime = xi - std::sin(2.0 * xi) * std::cosh(2.0 * eta) * beta1
                               - std::sin(4.0 * xi) * std::cosh(4.0 * eta) * beta2
                               - std::sin(6.0 * xi) * std::cosh(6.0 * eta) * beta3;
    const double etaPrime = eta - std::cos(2.0 * xi) * std::sinh(2.0 * eta) * beta1
                                - std::cos(4.0 * xi) * std::sinh(4.0 * eta) * beta2
                                - std::cos(6.0 * xi) * std::sinh(6.0 * eta) * beta3;

    const double chi = std::asin(std::sin(xiPrime) / std::cosh(etaPrime));

    lat = chi + std::sin(2.0 * chi) * delta1 + std::sin(4.0 * chi) * delta2 + std::sin(6.0 * chi) * delta3;
    lon = lon0 + std::atan(std::sinh(etaPrime) / std::cos(xiPrime));
}

// ============================================================================
// WGS84 <-> UTM (Directa e Inversa)
// ============================================================================

UTMCoord CoordinateConverter::wgs84ToUtm(const GeographicCoord& geo) const {
    if (geo.latitude < -80.0 || geo.latitude > 84.0) {
        throw std::invalid_argument("Latitud fuera de rango UTM valido (-80 a 84 grados)");
    }

    UTMCoord utm;
    utm.zone = computeUtmZone(geo.longitude);
    utm.isNorth = isNorthernHemisphere(geo.latitude);

    const double latRad = deg2rad(geo.latitude);
    const double lonRad = deg2rad(geo.longitude);
    const double lon0Rad = deg2rad((utm.zone - 1) * 6.0 - 180.0 + 3.0); // Meridiano central

    double e, n;
    tmDirect(latRad, lonRad, lon0Rad, WGS84_A, WGS84_F, K0, e, n);

    utm.easting = e;
    // Ajustar northing para hemisferio sur (constant 10,000,000 m offset)
    utm.northing = utm.isNorth ? n : n + 10000000.0;

    return utm;
}

GeographicCoord CoordinateConverter::utmToWgs84(const UTMCoord& utm) const {
    if (utm.zone < 1 || utm.zone > 60) {
        throw std::invalid_argument("Zona UTM fuera de rango (1-60)");
    }

    GeographicCoord geo;

    const double lon0Rad = deg2rad((utm.zone - 1) * 6.0 - 180.0 + 3.0);
    const double adjustedNorthing = utm.isNorth ? utm.northing : utm.northing - 10000000.0;

    double latRad, lonRad;
    tmInverse(utm.easting, adjustedNorthing, lon0Rad, WGS84_A, WGS84_F, K0, latRad, lonRad);

    geo.latitude = rad2deg(latRad);
    geo.longitude = rad2deg(lonRad);
    geo.elevation = 0.0; // UTM no almacena elevacion

    return geo;
}

// ============================================================================
// WGS84 <-> NAD27 (Helmert 7-parametros con Clarke 1866)
// ============================================================================

GeographicCoord CoordinateConverter::wgs84ToNad27(const GeographicCoord& wgs84) const {
    double x, y, z;
    geoToCartesian(deg2rad(wgs84.latitude), deg2rad(wgs84.longitude), wgs84.elevation,
                   WGS84_A, WGS84_F, x, y, z);

    double xn, yn, zn;
    helmertTransform(x, y, z, WGS84_TO_NAD27_APPROX, xn, yn, zn);

    GeographicCoord nad27;
    cartesianToGeo(xn, yn, zn, CLARKE_A, CLARKE_F,
                   nad27.latitude, nad27.longitude, nad27.elevation);
    nad27.latitude = rad2deg(nad27.latitude);
    nad27.longitude = rad2deg(nad27.longitude);

    return nad27;
}

GeographicCoord CoordinateConverter::nad27ToWgs84(const GeographicCoord& nad27) const {
    double x, y, z;
    geoToCartesian(deg2rad(nad27.latitude), deg2rad(nad27.longitude), nad27.elevation,
                   CLARKE_A, CLARKE_F, x, y, z);

    double xw, yw, zw;
    helmertInverse(x, y, z, WGS84_TO_NAD27_APPROX, xw, yw, zw);

    GeographicCoord wgs84;
    cartesianToGeo(xw, yw, zw, WGS84_A, WGS84_F,
                   wgs84.latitude, wgs84.longitude, wgs84.elevation);
    wgs84.latitude = rad2deg(wgs84.latitude);
    wgs84.longitude = rad2deg(wgs84.longitude);

    return wgs84;
}

// ============================================================================
// WGS84 <-> NAD83 (transformacion identidad / Helmert nulo)
// ============================================================================

GeographicCoord CoordinateConverter::wgs84ToNad83(const GeographicCoord& wgs84) const {
    // WGS84 y NAD83 difieren en < 2 metros. Para precision maxima
    // se usaria un grid de transformacion NTv2.
    double x, y, z;
    geoToCartesian(deg2rad(wgs84.latitude), deg2rad(wgs84.longitude), wgs84.elevation,
                   WGS84_A, WGS84_F, x, y, z);

    double xn, yn, zn;
    helmertTransform(x, y, z, WGS84_TO_NAD83, xn, yn, zn);

    GeographicCoord nad83;
    cartesianToGeo(xn, yn, zn, GRS80_A, GRS80_F,
                   nad83.latitude, nad83.longitude, nad83.elevation);
    nad83.latitude = rad2deg(nad83.latitude);
    nad83.longitude = rad2deg(nad83.longitude);

    return nad83;
}

GeographicCoord CoordinateConverter::nad83ToWgs84(const GeographicCoord& nad83) const {
    double x, y, z;
    geoToCartesian(deg2rad(nad83.latitude), deg2rad(nad83.longitude), nad83.elevation,
                   GRS80_A, GRS80_F, x, y, z);

    double xw, yw, zw;
    helmertInverse(x, y, z, WGS84_TO_NAD83, xw, yw, zw);

    GeographicCoord wgs84;
    cartesianToGeo(xw, yw, zw, WGS84_A, WGS84_F,
                   wgs84.latitude, wgs84.longitude, wgs84.elevation);
    wgs84.latitude = rad2deg(wgs84.latitude);
    wgs84.longitude = rad2deg(wgs84.longitude);

    return wgs84;
}

// ============================================================================
// WGS84 <-> ETRS89 (transformacion ITRF)
// ============================================================================

GeographicCoord CoordinateConverter::wgs84ToEtrs89(const GeographicCoord& wgs84) const {
    double x, y, z;
    geoToCartesian(deg2rad(wgs84.latitude), deg2rad(wgs84.longitude), wgs84.elevation,
                   WGS84_A, WGS84_F, x, y, z);

    double xe, ye, ze;
    helmertTransform(x, y, z, WGS84_TO_ETRS89, xe, ye, ze);

    GeographicCoord etrs89;
    cartesianToGeo(xe, ye, ze, GRS80_A, GRS80_F,
                   etrs89.latitude, etrs89.longitude, etrs89.elevation);
    etrs89.latitude = rad2deg(etrs89.latitude);
    etrs89.longitude = rad2deg(etrs89.longitude);

    return etrs89;
}

GeographicCoord CoordinateConverter::etrs89ToWgs84(const GeographicCoord& etrs89) const {
    double x, y, z;
    geoToCartesian(deg2rad(etrs89.latitude), deg2rad(etrs89.longitude), etrs89.elevation,
                   GRS80_A, GRS80_F, x, y, z);

    double xw, yw, zw;
    helmertInverse(x, y, z, WGS84_TO_ETRS89, xw, yw, zw);

    GeographicCoord wgs84;
    cartesianToGeo(xw, yw, zw, WGS84_A, WGS84_F,
                   wgs84.latitude, wgs84.longitude, wgs84.elevation);
    wgs84.latitude = rad2deg(wgs84.latitude);
    wgs84.longitude = rad2deg(wgs84.longitude);

    return wgs84;
}

// ============================================================================
// Utilidades
// ============================================================================

int CoordinateConverter::computeUtmZone(double longitude) {
    int zone = static_cast<int>((longitude + 180.0) / 6.0) + 1;
    if (zone < 1) zone = 1;
    if (zone > 60) zone = 60;
    return zone;
}

bool CoordinateConverter::isNorthernHemisphere(double latitude) {
    return latitude >= 0.0;
}

} // namespace powsys365::gis
