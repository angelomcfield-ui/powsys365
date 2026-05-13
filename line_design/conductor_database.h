#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>

namespace powsys365::linedesign {

/**
 * @brief Comprehensive database of 100+ overhead conductors.
 *
 * Includes ACSR, AAC, AAAC, ACSS, ACAR types with full electrical
 * and mechanical properties. Lookup by code name or manufacturer.
 */
class ConductorDatabase {
public:
    /**
     * @brief Conductor type enumeration.
     */
    enum class Type {
        ACSR,   /**< Aluminium Conductor Steel Reinforced     */
        AAC,    /**< All Aluminium Conductor                  */
        AAAC,   /**< All Aluminium Alloy Conductor            */
        ACSS,   /**< Aluminium Conductor Steel Supported      */
        ACAR,   /**< Aluminium Conductor Alloy Reinforced     */
        ACSR_AW,/**< ACSR Aluminium Clad Steel               */
        AACSR,  /**< Alloy ACSR                               */
        HARDDrawn /**< Hard Drawn Copper / Bronze             */
    };

    static std::string typeToString(Type t);

    /**
     * @brief Full conductor record.
     */
    struct Record {
        std::string code;           /**< e.g. "Condor"                    */
        std::string name;           /**< Manufacturer name                */
        Type        type;           /**< Conductor family                 */
        double      diameter_mm;    /**< Outer diameter [mm]              */
        double      area_mm2;       /**< Total cross-section [mm²]        */
        double      aluminium_mm2;  /**< Aluminium area [mm²]             */
        double      steel_mm2;      /**< Steel area [mm²] (0 for AAC)     */
        double      weight_kg_m;    /**< Unit mass [kg/m]                 */
        double      ratedStrength_N;/**< Rated breaking load [N]          */
        double      R_dc_20;        /**< DC resistance at 20°C [Ω/km]     */
        double      R_ac_25;        /**< AC resistance at 25°C [Ω/km]     */
        double      R_ac_75;        /**< AC resistance at 75°C [Ω/km]     */
        double      alpha_R;        /**< Resistance temp coeff [1/°C]     */
        double      ampacity_A;     /**< Base ampacity at 75°C [A]        */
        double      C_heat;         /**< Specific heat [J/(kg·°C)]        */
        double      E_elastic_GPa;  /**< Young's modulus [GPa]            */
        double      alpha_exp;      /**< Thermal expansion coeff [1/°C]   */
        double      CTE;            /**< Coefficient of thermal expansion */
        int         stranding_al;   /**< Al strands                       */
        int         stranding_st;   /**< Steel strands (0 for AAC/AAAC)   */
    };

    /**
     * @brief Load the built-in database (100+ conductors).
     */
    ConductorDatabase();

    /**
     * @brief Look up a conductor by its code name (case-insensitive).
     *
     * @param code  e.g. "Condor", "Bluebird", "Dove".
     * @return Record if found, empty optional otherwise.
     */
    std::optional<Record> lookupByName(const std::string& code) const;

    /**
     * @brief Look up by manufacturer part number.
     *
     * @param partNumber Manufacturer part number.
     * @return Record if found.
     */
    std::optional<Record> lookupByCode(const std::string& partNumber) const;

    /**
     * @brief Search by type.
     *
     * @param t Conductor type.
     * @return All matching records.
     */
    std::vector<Record> lookupByType(Type t) const;

    /**
     * @brief Find closest match by area.
     *
     * @param targetArea_mm2 Desired cross-section [mm²].
     * @return Closest record.
     */
    Record lookupByArea(double targetArea_mm2) const;

    /**
     * @brief Find closest match by diameter.
     */
    Record lookupByDiameter(double targetDiameter_mm) const;

    /**
     * @brief Find closest match by rated strength.
     */
    Record lookupByStrength(double targetStrength_N) const;

    /**
     * @brief Full list of all records.
     */
    std::vector<Record> allRecords() const;

    /**
     * @brief Number of records in database.
     */
    size_t size() const;

private:
    std::unordered_map<std::string, Record> byCode_;
    std::unordered_map<std::string, Record> byName_;
    std::vector<Record>                     all_;

    /**
     * @brief Populate the 100+ built-in entries.
     */
    void populateACSR();
    void populateAAC();
    void populateAAAC();
    void populateACSS();
    void populateACAR();
    void populateOthers();

    /**
     * @brief Case-insensitive string compare for lookup.
     */
    static std::string toLower(const std::string& s);
};

} // namespace powsys365::linedesign
