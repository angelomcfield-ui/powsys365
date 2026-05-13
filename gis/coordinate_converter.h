#pragma once

#include <cmath>
#include <string>
#include <stdexcept>

namespace powsys365::gis {

// ============================================================================
// Estructuras de Datos
// ============================================================================

struct GeographicCoord {
    double latitude;   // grados decimales
    double longitude;  // grados decimales
    double elevation;  // metros sobre el nivel del mar
};

struct UTMCoord {
    double easting;    // metros
    double northing;   // metros
    int    zone;       // zona UTM (1-60)
    bool   isNorth;    // true = hemisferio norte
};

struct DatumShiftParams {
    double dx;  // metros
    double dy;  // metros
    double dz;  // metros
    double rx;  // segundos de arco (rotacion X)
    double ry;  // segundos de arco (rotacion Y)
    double rz;  // segundos de arco (rotacion Z)
    double ds;  // ppm (factor de escala)
};

// ============================================================================
// CoordinateConverter
// ============================================================================

class CoordinateConverter {
public:
    CoordinateConverter();

    // --- WGS84 <-> UTM ---
    UTMCoord        wgs84ToUtm(const GeographicCoord& geo) const;
    GeographicCoord utmToWgs84(const UTMCoord& utm) const;

    // --- WGS84 <-> NAD27 (Clarke 1866) ---
    GeographicCoord wgs84ToNad27(const GeographicCoord& wgs84) const;
    GeographicCoord nad27ToWgs84(const GeographicCoord& nad27) const;

    // --- WGS84 <-> NAD83 (GRS80, transformacion casi nula) ---
    GeographicCoord wgs84ToNad83(const GeographicCoord& wgs84) const;
    GeographicCoord nad83ToWgs84(const GeographicCoord& nad83) const;

    // --- WGS84 <-> ETRS89 (ITRF, transformacion moderna) ---
    GeographicCoord wgs84ToEtrs89(const GeographicCoord& wgs84) const;
    GeographicCoord etrs89ToWgs84(const GeographicCoord& etrs89) const;

    // --- Utilidades ---
    static int      computeUtmZone(double longitude);
    static bool     isNorthernHemisphere(double latitude);

private:
    // --- Constantes elipsoidales WGS84 ---
    static constexpr double WGS84_A = 6378137.0;                    // semieje mayor (m)
    static constexpr double WGS84_F = 1.0 / 298.257223563;          // aplanamiento
    static constexpr double WGS84_E2 = 2.0 * WGS84_F - WGS84_F * WGS84_F; // primera excentricidad al cuadrado
    static constexpr double K0 = 0.9996;                            // factor de escala UTM

    // --- Constantes elipsoidales Clarke 1866 (NAD27) ---
    static constexpr double CLARKE_A = 6378206.4;
    static constexpr double CLARKE_F = 1.0 / 294.978698214;

    // --- Constantes elipsoidales GRS80 (NAD83/ETRS89) ---
    static constexpr double GRS80_A = 6378137.0;
    static constexpr double GRS80_F = 1.0 / 298.257222101;

    // --- Parametros de transformacion Helmert ---
    static constexpr DatumShiftParams WGS84_TO_NAD27{}; // Identidad - se usa gridshift en produccion
    static constexpr DatumShiftParams WGS84_TO_NAD83{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    static constexpr DatumShiftParams WGS84_TO_ETRS89{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    // Parametros aproximados para NAD27 (WGS84 -> NAD27)
    static constexpr DatumShiftParams WGS84_TO_NAD27_APPROX{-8.0, 160.0, 176.0, 0.0, 0.0, 0.0, 0.0};

    // --- Funciones internas de conversion ---
    static double   deg2rad(double deg);
    static double   rad2deg(double rad);

    // Conversion geodesica <-> geocentrica
    void            geoToCartesian(double lat, double lon, double h,
                                 double a, double f,
                                 double& x, double& y, double& z) const;
    void            cartesianToGeo(double x, double y, double z,
                                 double a, double f,
                                 double& lat, double& lon, double& h) const;

    // Transformacion Helmert 7-parametros
    void            helmertTransform(double x, double y, double z,
                                   const DatumShiftParams& p,
                                   double& xo, double& yo, double& zo) const;
    void            helmertInverse(double x, double y, double z,
                                 const DatumShiftParams& p,
                                 double& xo, double& yo, double& zo) const;

    // Conversion directa/inversa de Transversal de Mercator (UTM)
    void            tmDirect(double lat, double lon, double lon0,
                            double a, double f, double k0,
                            double& easting, double& northing) const;
    void            tmInverse(double easting, double northing, double lon0,
                             double a, double f, double k0,
                             double& lat, double& lon) const;
};

} // namespace powsys365::gis
