#include "glossary_manager.h"
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#include <QMutexLocker>
#include <QRegularExpression>
#include <algorithm>

namespace powsys365::help {

// Simple Levenshtein distance for fuzzy search
static int levenshteinDistance(const QString& s1, const QString& s2) {
    int m = s1.length();
    int n = s2.length();

    if (m == 0) return n;
    if (n == 0) return m;

    QVector<int> previous(n + 1);
    QVector<int> current(n + 1);

    for (int j = 0; j <= n; ++j) previous[j] = j;

    for (int i = 1; i <= m; ++i) {
        current[0] = i;
        QChar c1 = s1[i - 1];
        for (int j = 1; j <= n; ++j) {
            int cost = (c1 == s2[j - 1]) ? 0 : 1;
            current[j] = std::min({
                previous[j] + 1,      // deletion
                current[j - 1] + 1,  // insertion
                previous[j - 1] + cost // substitution
            });
        }
        previous.swap(current);
    }

    return previous[n];
}

class GlossaryManager::Impl {
public:
    GlossaryManager* q;
    QMap<QString, GlossaryEntry> terms; // lowercase term -> entry
    QMap<QString, QString> abbreviations; // abbreviation -> full term
    mutable QMutex mutex;

    explicit Impl(GlossaryManager* parent) : q(parent) {}
};

GlossaryManager::GlossaryManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this))
{
}

GlossaryManager::~GlossaryManager() = default;

bool GlossaryManager::initialize() {
    loadDefaultGlossary();
    return true;
}

void GlossaryManager::addTerm(const GlossaryEntry& entry) {
    QMutexLocker lock(&d->mutex);
    d->terms[entry.term.toLower()] = entry;
    if (!entry.abbreviation.isEmpty()) {
        d->abbreviations[entry.abbreviation.toLower()] = entry.term;
    }
    Q_EMIT termAdded(entry.term);
}

void GlossaryManager::addTerm(const QString& term, const QString& definition,
                               const QString& category) {
    GlossaryEntry entry;
    entry.term = term;
    entry.definition = definition;
    entry.category = category;
    addTerm(entry);
}

bool GlossaryManager::removeTerm(const QString& term) {
    QMutexLocker lock(&d->mutex);

    auto it = d->terms.find(term.toLower());
    if (it == d->terms.end()) return false;

    if (!it.value().abbreviation.isEmpty()) {
        d->abbreviations.remove(it.value().abbreviation.toLower());
    }

    d->terms.erase(it);
    Q_EMIT termRemoved(term);
    return true;
}

bool GlossaryManager::updateTerm(const GlossaryEntry& entry) {
    QMutexLocker lock(&d->mutex);

    if (!d->terms.contains(entry.term.toLower())) return false;

    // Remove old abbreviation mapping
    auto old = d->terms.value(entry.term.toLower());
    if (!old.abbreviation.isEmpty()) {
        d->abbreviations.remove(old.abbreviation.toLower());
    }

    d->terms[entry.term.toLower()] = entry;
    if (!entry.abbreviation.isEmpty()) {
        d->abbreviations[entry.abbreviation.toLower()] = entry.term;
    }

    Q_EMIT termUpdated(entry.term);
    return true;
}

GlossaryEntry GlossaryManager::getTerm(const QString& term) const {
    QMutexLocker lock(&d->mutex);
    return d->terms.value(term.toLower());
}

bool GlossaryManager::hasTerm(const QString& term) const {
    QMutexLocker lock(&d->mutex);
    return d->terms.contains(term.toLower());
}

QList<GlossarySearchResult> GlossaryManager::search(const QString& query) const {
    QMutexLocker lock(&d->mutex);
    QList<GlossarySearchResult> results;

    if (query.isEmpty()) return results;

    QString lowerQuery = query.toLower();

    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        const GlossaryEntry& entry = it.value();
        GlossarySearchResult result;
        result.entry = entry;

        if (entry.term.toLower() == lowerQuery) {
            result.relevance = 100.0f;
            result.matchType = "exact";
            results.append(result);
        } else if (entry.term.toLower().startsWith(lowerQuery)) {
            result.relevance = 80.0f;
            result.matchType = "prefix";
            results.append(result);
        } else if (entry.term.toLower().contains(lowerQuery)) {
            result.relevance = 60.0f;
            result.matchType = "substring";
            results.append(result);
        } else if (entry.abbreviation.toLower() == lowerQuery) {
            result.relevance = 90.0f;
            result.matchType = "exact";
            results.append(result);
        } else if (!entry.abbreviation.isEmpty() &&
                   entry.abbreviation.toLower().startsWith(lowerQuery)) {
            result.relevance = 70.0f;
            result.matchType = "prefix";
            results.append(result);
        } else if (entry.definition.toLower().contains(lowerQuery)) {
            // Count occurrences
            int count = entry.definition.toLower().count(lowerQuery);
            result.relevance = 30.0f + count * 5.0f;
            result.relevance = std::min(result.relevance, 55.0f);
            result.matchType = "definition";
            results.append(result);
        } else if (entry.fullForm.toLower().contains(lowerQuery)) {
            result.relevance = 50.0f;
            result.matchType = "substring";
            results.append(result);
        } else {
            // Check related terms
            for (const QString& related : entry.relatedTerms) {
                if (related.toLower().contains(lowerQuery)) {
                    result.relevance = 25.0f;
                    result.matchType = "related";
                    results.append(result);
                    break;
                }
            }
        }
    }

    // Sort by relevance
    std::sort(results.begin(), results.end(),
        [](const GlossarySearchResult& a, const GlossarySearchResult& b) {
            return a.relevance > b.relevance;
        });

    return results;
}

QList<GlossarySearchResult> GlossaryManager::searchByCategory(const QString& category) const {
    QMutexLocker lock(&d->mutex);
    QList<GlossarySearchResult> results;

    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        if (it.value().category == category) {
            GlossarySearchResult result;
            result.entry = it.value();
            result.relevance = static_cast<float>(it.value().importance) * 10.0f;
            result.matchType = "category";
            results.append(result);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const GlossarySearchResult& a, const GlossarySearchResult& b) {
            return a.relevance > b.relevance;
        });

    return results;
}

QList<GlossarySearchResult> GlossaryManager::searchExact(const QString& term) const {
    QMutexLocker lock(&d->mutex);
    QList<GlossarySearchResult> results;

    auto it = d->terms.find(term.toLower());
    if (it != d->terms.end()) {
        GlossarySearchResult result;
        result.entry = it.value();
        result.relevance = 100.0f;
        result.matchType = "exact";
        results.append(result);
    }

    return results;
}

QList<GlossarySearchResult> GlossaryManager::fuzzySearch(const QString& query,
                                                          int maxDistance) const {
    QMutexLocker lock(&d->mutex);
    QList<GlossarySearchResult> results;

    if (query.isEmpty()) return results;

    QString lowerQuery = query.toLower();

    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        const GlossaryEntry& entry = it.value();

        int dist = levenshteinDistance(lowerQuery, entry.term.toLower());
        if (dist <= maxDistance) {
            GlossarySearchResult result;
            result.entry = entry;
            result.relevance = 100.0f - dist * 20.0f;
            result.matchType = "fuzzy";
            results.append(result);
        }

        // Also check abbreviation
        if (!entry.abbreviation.isEmpty()) {
            int abbrDist = levenshteinDistance(lowerQuery, entry.abbreviation.toLower());
            if (abbrDist <= maxDistance) {
                GlossarySearchResult result;
                result.entry = entry;
                result.relevance = 90.0f - abbrDist * 20.0f;
                result.matchType = "fuzzy_abbreviation";
                if (!results.contains(result)) {
                    results.append(result);
                }
            }
        }
    }

    std::sort(results.begin(), results.end(),
        [](const GlossarySearchResult& a, const GlossarySearchResult& b) {
            return a.relevance > b.relevance;
        });

    return results;
}

QStringList GlossaryManager::getRelatedTerms(const QString& term) const {
    QMutexLocker lock(&d->mutex);

    auto it = d->terms.find(term.toLower());
    if (it != d->terms.end()) {
        return it.value().relatedTerms;
    }
    return QStringList();
}

QStringList GlossaryManager::autocomplete(const QString& prefix, int maxResults) const {
    QMutexLocker lock(&d->mutex);
    QStringList results;

    if (prefix.isEmpty()) return results;

    QString lowerPrefix = prefix.toLower();

    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        const QString& term = it.value().term;
        if (term.toLower().startsWith(lowerPrefix)) {
            results.append(term);
        }
        if (!it.value().abbreviation.isEmpty() &&
            it.value().abbreviation.toLower().startsWith(lowerPrefix)) {
            results.append(it.value().abbreviation);
        }
        if (results.size() >= maxResults) break;
    }

    results.sort();
    return results;
}

QStringList GlossaryManager::categories() const {
    QMutexLocker lock(&d->mutex);
    QSet<QString> cats;
    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        cats.insert(it.value().category);
    }
    QStringList result = cats.values();
    result.sort();
    return result;
}

int GlossaryManager::termCount() const {
    QMutexLocker lock(&d->mutex);
    return d->terms.size();
}

int GlossaryManager::termCountByCategory(const QString& category) const {
    QMutexLocker lock(&d->mutex);
    int count = 0;
    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        if (it.value().category == category) count++;
    }
    return count;
}

QJsonObject GlossaryManager::toJson() const {
    QMutexLocker lock(&d->mutex);
    QJsonObject root;
    QJsonArray termsArray;

    for (auto it = d->terms.begin(); it != d->terms.end(); ++it) {
        termsArray.append(it.value().toJson());
    }

    root["terms"] = termsArray;
    root["version"] = "1.0";
    root["count"] = d->terms.size();

    // Add categories summary
    QJsonArray cats;
    for (const QString& cat : categories()) {
        QJsonObject catObj;
        catObj["name"] = cat;
        catObj["count"] = termCountByCategory(cat);
        cats.append(catObj);
    }
    root["categories"] = cats;

    return root;
}

bool GlossaryManager::fromJson(const QJsonObject& obj) {
    QMutexLocker lock(&d->mutex);

    d->terms.clear();
    d->abbreviations.clear();

    QJsonArray termsArray = obj.value("terms").toArray();
    for (const auto& val : termsArray) {
        GlossaryEntry entry = GlossaryEntry::fromJson(val.toObject());
        if (!entry.term.isEmpty()) {
            d->terms[entry.term.toLower()] = entry;
            if (!entry.abbreviation.isEmpty()) {
                d->abbreviations[entry.abbreviation.toLower()] = entry.term;
            }
        }
    }

    Q_EMIT glossaryLoaded();
    return true;
}

bool GlossaryManager::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT error(QString("Cannot open glossary file: %1").arg(filePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        Q_EMIT error(QString("JSON parse error: %1").arg(error.errorString()));
        return false;
    }

    return fromJson(doc.object());
}

bool GlossaryManager::saveToFile(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Q_EMIT error(QString("Cannot write glossary file: %1").arg(filePath));
        return false;
    }

    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void GlossaryManager::loadDefaultGlossary() {
    addElectricalTerms();
    addPowerSystemTerms();
    addProtectionTerms();
    addTransmissionTerms();
    addDistributionTerms();
    addTransformersTerms();
    addMotorsTerms();
    addMeasurementTerms();
    addSoftwareTerms();
    Q_EMIT glossaryLoaded();
}

// ============================================================================
// Default Glossary Terms - Electrical Engineering
// ============================================================================

void GlossaryManager::addElectricalTerms() {
    // Basic Electrical Terms
    addTerm({"Voltage", "The electrical potential difference between two points in a circuit, measured in volts (V). It represents the force that drives electric current.",
             "electrical", {"Current", "Resistance", "Ohm's Law"}, "V", "Voltage", "V = IR", "V", 10});

    addTerm({"Current", "The flow rate of electric charge past a point in a circuit, measured in amperes (A). It is the movement of electrons through a conductor.",
             "electrical", {"Voltage", "Resistance", "Power"}, "I", "Current", "I = V/R", "A", 10});

    addTerm({"Resistance", "The opposition to the flow of electric current in a material, measured in ohms (Omega). It depends on the material's properties, length, and cross-sectional area.",
             "electrical", {"Voltage", "Current", "Ohm's Law", "Conductance"}, "R", "Resistance", "R = V/I", "Omega", 10});

    addTerm({"Ohm's Law", "A fundamental law of electrical circuits stating that the current through a conductor is directly proportional to the voltage across it and inversely proportional to its resistance: V = IR.",
             "electrical", {"Voltage", "Current", "Resistance"}, "", "", "V = IR", "", 10});

    addTerm({"Power", "The rate at which electrical energy is transferred by a circuit, measured in watts (W). In DC circuits: P = VI = I^2R = V^2/R.",
             "electrical", {"Voltage", "Current", "Energy", "Apparent Power"}, "P", "Power", "P = VI", "W", 10});

    addTerm({"Energy", "The capacity to do work, measured in joules (J) or kilowatt-hours (kWh). Electrical energy is power multiplied by time: E = Pt.",
             "electrical", {"Power", "Work"}, "E", "Energy", "E = Pt", "J", 9});

    addTerm({"Conductance", "The ability of a material to conduct electric current, measured in siemens (S). It is the reciprocal of resistance: G = 1/R.",
             "electrical", {"Resistance", "Conductivity"}, "G", "Conductance", "G = 1/R", "S", 7});

    addTerm({"Conductivity", "A material property that quantifies how well it conducts electric current, measured in siemens per meter (S/m). It is the reciprocal of resistivity.",
             "electrical", {"Conductance", "Resistivity"}, "sigma", "Conductivity", "sigma = 1/rho", "S/m", 7});

    addTerm({"Resistivity", "An intrinsic property of a material that measures how strongly it opposes the flow of electric current, measured in ohm-meters (Omega.m).",
             "electrical", {"Conductivity", "Resistance"}, "rho", "Resistivity", "rho = R*A/L", "Omega.m", 8});

    addTerm({"Capacitance", "The ability of a component or circuit to store electrical energy in an electric field, measured in farads (F). C = Q/V.",
             "electrical", {"Capacitor", "Reactance"}, "C", "Capacitance", "C = Q/V", "F", 9});

    addTerm({"Capacitor", "A passive electrical component consisting of two conductive plates separated by a dielectric, used to store electrical energy in an electric field.",
             "electrical", {"Capacitance", "Inductor", "Filter"}, "", "", "", "", 9});

    addTerm({"Inductance", "The property of an electrical conductor by which a change in current induces an electromotive force (EMF), measured in henries (H).",
             "electrical", {"Inductor", "Reactance", "Magnetic Field"}, "L", "Inductance", "L = N*Phi/I", "H", 9});

    addTerm({"Inductor", "A passive electrical component, typically a coil of wire, that stores energy in a magnetic field when electric current flows through it.",
             "electrical", {"Inductance", "Capacitor", "Transformer"}, "", "", "", "", 9});

    addTerm({"Reactance", "The opposition to the flow of alternating current caused by capacitance or inductance, measured in ohms (Omega). Xc = 1/(2*pi*f*C), Xl = 2*pi*f*L.",
             "electrical", {"Impedance", "Capacitance", "Inductance"}, "X", "Reactance", "X = Xl - Xc", "Omega", 9});

    addTerm({"Impedance", "The total opposition to current flow in an AC circuit, combining resistance and reactance, measured in ohms (Omega). Z = R + jX.",
             "electrical", {"Resistance", "Reactance", "Admittance"}, "Z", "Impedance", "Z = sqrt(R^2 + X^2)", "Omega", 10});

    addTerm({"Admittance", "The reciprocal of impedance, representing how easily a circuit allows current to flow, measured in siemens (S). Y = 1/Z = G + jB.",
             "electrical", {"Impedance", "Conductance"}, "Y", "Admittance", "Y = 1/Z", "S", 8});

    addTerm({"Frequency", "The number of cycles per second of an alternating current or voltage waveform, measured in hertz (Hz). f = 1/T.",
             "electrical", {"Period", "Angular Frequency", "Wavelength"}, "f", "Frequency", "f = 1/T", "Hz", 9});

    addTerm({"Period", "The time duration of one complete cycle of a periodic waveform, measured in seconds (s). T = 1/f.",
             "electrical", {"Frequency"}, "T", "Period", "T = 1/f", "s", 8});

    addTerm({"Phase", "The position of a point in time on a waveform cycle, measured in degrees or radians. In AC circuits, it describes the timing relationship between voltage and current.",
             "electrical", {"Phase Angle", "Power Factor"}, "phi", "Phase", "", "deg/rad", 8});

    addTerm({"Phase Angle", "The angle by which the voltage leads or lags the current in an AC circuit, measured in degrees. phi = arctan(X/R).",
             "electrical", {"Phase", "Power Factor", "Reactance"}, "phi", "Phase Angle", "phi = arctan(X/R)", "deg", 8});

    addTerm({"Power Factor", "The ratio of real power to apparent power in an AC circuit, dimensionless. PF = cos(phi) = P/S. A value closer to 1 indicates more efficient power usage.",
             "electrical", {"Real Power", "Apparent Power", "Reactive Power", "Phase Angle"}, "PF", "Power Factor", "PF = cos(phi)", "", 10});

    addTerm({"Real Power", "The actual power consumed by a circuit to do work, measured in watts (W). Also called active power or true power. P = VI*cos(phi).",
             "electrical", {"Reactive Power", "Apparent Power", "Power Factor"}, "P", "Real Power", "P = VI*cos(phi)", "W", 10});

    addTerm({"Reactive Power", "The power that oscillates between the source and reactive components (inductors and capacitors) without doing work, measured in volt-amperes reactive (VAR). Q = VI*sin(phi).",
             "electrical", {"Real Power", "Apparent Power", "Power Factor"}, "Q", "Reactive Power", "Q = VI*sin(phi)", "VAR", 9});

    addTerm({"Apparent Power", "The combination of real and reactive power in an AC circuit, measured in volt-amperes (VA). S = VI = sqrt(P^2 + Q^2).",
             "electrical", {"Real Power", "Reactive Power", "Power Factor"}, "S", "Apparent Power", "S = sqrt(P^2 + Q^2)", "VA", 10});

    addTerm({"Electromotive Force", "The energy provided by a source per unit of charge, measured in volts (V). It represents the potential difference created by a source such as a battery or generator.",
             "electrical", {"Voltage", "Battery", "Generator"}, "EMF", "Electromotive Force", "", "V", 8});

    addTerm({"Electrical Field", "A vector field surrounding electrically charged particles that exerts force on other charged objects, measured in volts per meter (V/m) or newtons per coulomb (N/C).",
             "electrical", {"Voltage", "Charge", "Force"}, "E", "Electrical Field", "E = F/Q", "V/m", 7});

    addTerm({"Magnetic Field", "A vector field produced by moving electric charges or magnetic dipoles, measured in teslas (T). It exerts force on moving charges and magnetic materials.",
             "electrical", {"Inductance", "Transformer", "Motor", "Flux"}, "B", "Magnetic Field", "", "T", 8});

    addTerm({"Electric Flux", "The measure of the electric field passing through a given area, measured in volt-meters (V.m) or newton-meters squared per coulomb (N.m^2/C).",
             "electrical", {"Electric Field", "Gauss's Law"}, "Phi_E", "Electric Flux", "Phi = E * A", "V.m", 7});

    addTerm({"Kirchhoff's Voltage Law", "KVL states that the sum of all voltages around any closed loop in a circuit equals zero. The algebraic sum of voltage rises equals the algebraic sum of voltage drops.",
             "electrical", {"Kirchhoff's Current Law", "Circuit Analysis", "Mesh Analysis"}, "KVL", "Kirchhoff's Voltage Law", "Sum(V) = 0", "", 10});

    addTerm({"Kirchhoff's Current Law", "KCL states that the sum of currents entering a junction equals the sum of currents leaving that junction. The algebraic sum of currents at any node is zero.",
             "electrical", {"Kirchhoff's Voltage Law", "Circuit Analysis", "Node Analysis"}, "KCL", "Kirchhoff's Current Law", "Sum(I) = 0", "", 10});

    addTerm({"Thevenin's Theorem", "Any linear electrical network with voltage and current sources and resistances can be replaced by an equivalent circuit consisting of a single voltage source in series with a single resistance.",
             "electrical", {"Norton's Theorem", "Circuit Analysis", "Equivalent Circuit"}, "", "", "", "", 9});

    addTerm({"Norton's Theorem", "Any linear electrical network can be replaced by an equivalent circuit consisting of a single current source in parallel with a single resistance.",
             "electrical", {"Thevenin's Theorem", "Circuit Analysis", "Equivalent Circuit"}, "", "", "", "", 9});

    addTerm({"Superposition Theorem", "In a linear circuit with multiple sources, the voltage or current at any point is the algebraic sum of the voltages or currents produced by each source acting independently.",
             "electrical", {"Circuit Analysis", "Linear Systems"}, "", "", "", "", 8});
}

void GlossaryManager::addPowerSystemTerms() {
    addTerm({"Load Flow Analysis", "A numerical analysis method used to determine the steady-state operating conditions of a power system, including voltages, currents, and power flows at each bus.",
             "power_systems", {"Newton-Raphson", "Gauss-Seidel", "Power Flow", "Bus"}, "LF", "Load Flow", "", "", 10});

    addTerm({"Newton-Raphson Method", "An iterative numerical method for solving nonlinear equations, widely used in load flow analysis due to its quadratic convergence rate.",
             "power_systems", {"Load Flow", "Gauss-Seidel", "Convergence"}, "", "", "", "", 9});

    addTerm({"Gauss-Seidel Method", "An iterative method for solving systems of linear equations, used for load flow analysis. Simpler than Newton-Raphson but converges more slowly.",
             "power_systems", {"Load Flow", "Newton-Raphson", "Iteration"}, "", "", "", "", 8});

    addTerm({"Bus", "A node in a power system where power is injected, extracted, or transferred. Types include slack bus, PV bus, and PQ bus.",
             "power_systems", {"Load Flow", "Bus Admittance Matrix", "Node"}, "", "", "", "", 9});

    addTerm({"Slack Bus", "A reference bus in load flow analysis with specified voltage magnitude and angle, used to balance generation and load in the system.",
             "power_systems", {"Bus", "Load Flow", "Reference"}, "", "", "", "", 9});

    addTerm({"PV Bus", "A generator bus in load flow where the real power (P) and voltage magnitude (V) are specified, and reactive power (Q) and voltage angle are calculated.",
             "power_systems", {"Bus", "Slack Bus", "PQ Bus", "Load Flow"}, "", "", "", "", 9});

    addTerm({"PQ Bus", "A load bus in load flow where real power (P) and reactive power (Q) are specified, and voltage magnitude and angle are calculated.",
             "power_systems", {"Bus", "Slack Bus", "PV Bus", "Load Flow"}, "", "", "", "", 9});

    addTerm({"Short Circuit", "An abnormal condition where a low-resistance path allows excessive current to flow, potentially causing equipment damage and system instability.",
             "power_systems", {"Fault Analysis", "Protection", "Current"}, "", "", "", "", 10});

    addTerm({"Fault Analysis", "The study of abnormal conditions in power systems, including short circuits and open circuits, to determine fault currents for protective device coordination.",
             "power_systems", {"Short Circuit", "Protection", "Sequence Components"}, "", "", "", "", 10});

    addTerm({"Symmetrical Components", "A method for analyzing unbalanced three-phase systems by decomposing them into three balanced sets of phasors: positive, negative, and zero sequence.",
             "power_systems", {"Sequence Networks", "Fault Analysis", "Unbalance"}, "", "", "", "", 9});

    addTerm({"Positive Sequence", "A set of three phasors of equal magnitude displaced by 120 degrees in the normal (ABC) phase sequence. Represents balanced normal operation.",
             "power_systems", {"Symmetrical Components", "Negative Sequence", "Zero Sequence"}, "", "", "", "", 8});

    addTerm({"Negative Sequence", "A set of three phasors of equal magnitude displaced by 120 degrees in the reverse (ACB) phase sequence. Indicates unbalanced conditions or faults.",
             "power_systems", {"Symmetrical Components", "Positive Sequence", "Zero Sequence"}, "", "", "", "", 8});

    addTerm({"Zero Sequence", "A set of three phasors of equal magnitude with zero phase displacement. Flows during ground faults and indicates the presence of a ground return path.",
             "power_systems", {"Symmetrical Components", "Positive Sequence", "Negative Sequence", "Ground Fault"}, "", "", "", "", 8});

    addTerm({"Power System Stability", "The ability of a power system to maintain synchronous operation and return to an acceptable steady state after being subjected to a disturbance.",
             "power_systems", {"Transient Stability", "Voltage Stability", "Frequency Stability"}, "", "", "", "", 10});

    addTerm({"Transient Stability", "The ability of a power system to maintain synchronism when subjected to large disturbances such as faults or sudden load changes.",
             "power_systems", {"Stability", "Swing Equation", "Equal Area Criterion"}, "", "", "", "", 10});

    addTerm({"Voltage Stability", "The ability of a power system to maintain acceptable voltages at all buses under normal conditions and after being subjected to a disturbance.",
             "power_systems", {"Stability", "PV Curve", "Collapse"}, "", "", "", "", 10});

    addTerm({"Frequency Stability", "The ability of a power system to maintain steady frequency following a severe imbalance between generation and load.",
             "power_systems", {"Stability", "Load Shedding", "Governor"}, "", "", "", "", 9});

    addTerm({"Swing Equation", "The differential equation describing the rotor dynamics of a synchronous machine: M*d^2(delta)/dt^2 = Pm - Pe - D*d(delta)/dt.",
             "power_systems", {"Transient Stability", "Rotor Angle", "Inertia"}, "", "", "M*d2d/dt2 = Pm - Pe", "", 9});

    addTerm({"Equal Area Criterion", "A graphical method for assessing transient stability by comparing the accelerating and decelerating areas on a power-angle curve.",
             "power_systems", {"Transient Stability", "Swing Equation", "Stability"}, "", "", "", "", 8});

    addTerm({"Per Unit System", "A method of expressing electrical quantities as normalized dimensionless values relative to a chosen base value. Simplifies calculations and comparisons.",
             "power_systems", {"Base Values", "Normalization", "Impedance"}, "pu", "Per Unit", "", "", 9});

    addTerm({"Three-Phase System", "An electrical power system with three voltage sources of equal magnitude with 120-degree phase displacement. The standard for power generation, transmission, and distribution.",
             "power_systems", {"Phase", "Balanced System", "Line Voltage"}, "3-phase", "Three-Phase System", "", "", 10});

    addTerm({"Line Voltage", "The voltage between any two phases in a three-phase system. For balanced systems: V_line = sqrt(3) * V_phase.",
             "power_systems", {"Phase Voltage", "Three-Phase", "Delta", "Wye"}, "V_L", "Line Voltage", "V_L = sqrt(3)*V_ph", "V", 9});

    addTerm({"Phase Voltage", "The voltage between any phase and neutral in a three-phase system. For balanced systems: V_phase = V_line / sqrt(3).",
             "power_systems", {"Line Voltage", "Three-Phase", "Neutral"}, "V_ph", "Phase Voltage", "V_ph = V_L/sqrt(3)", "V", 9});

    addTerm({"Wye Connection", "A three-phase connection where one end of each phase winding is connected to a common neutral point, forming a Y shape.",
             "power_systems", {"Delta Connection", "Three-Phase", "Neutral", "Star"}, "Y", "Wye Connection", "", "", 8});

    addTerm({"Delta Connection", "A three-phase connection where the end of each phase winding is connected to the start of the next, forming a triangle (delta) shape.",
             "power_systems", {"Wye Connection", "Three-Phase", "Line Current"}, "Delta", "Delta Connection", "", "", 8});

    addTerm({"Power Factor Correction", "The process of improving the power factor of an electrical system by adding capacitors or synchronous condensers to compensate for inductive loads.",
             "power_systems", {"Power Factor", "Capacitor", "Reactive Power"}, "PFC", "Power Factor Correction", "", "", 10});

    addTerm({"Load Shedding", "The deliberate disconnection of electrical load from the power system to maintain system stability when generation is insufficient.",
             "power_systems", {"Frequency Stability", "Emergency", "Underfrequency"}, "", "", "", "", 9});

    addTerm({"Economic Dispatch", "The optimization process of allocating generation among available power plants to meet load demand at minimum cost while satisfying operational constraints.",
             "power_systems", {"Generation", "Optimization", "Lambda Iteration"}, "ED", "Economic Dispatch", "", "", 9});
}

void GlossaryManager::addProtectionTerms() {
    addTerm({"Relay", "A protective device that detects abnormal conditions in an electrical system and initiates circuit breaker operation to isolate the faulted section.",
             "protection", {"Circuit Breaker", "Protection Coordination", "Fault"}, "", "", "", "", 10});

    addTerm({"Circuit Breaker", "A switching device capable of making, carrying, and breaking currents under normal and fault conditions. Types include air, oil, SF6, and vacuum circuit breakers.",
             "protection", {"Relay", "Fault Current", "Switching"}, "CB", "Circuit Breaker", "", "", 10});

    addTerm({"Protection Coordination", "The systematic selection and setting of protective devices to ensure selective fault clearing, where only the device closest to the fault operates.",
             "protection", {"Relay", "Circuit Breaker", "Selectivity", "Time-Current Characteristic"}, "", "", "", "", 10});

    addTerm({"Selectivity", "The property of a protection system where only the protective device closest to a fault operates, minimizing the outage area.",
             "protection", {"Protection Coordination", "Relay", "Zone"}, "", "", "", "", 9});

    addTerm({"Overcurrent Protection", "Protection against excessive current caused by short circuits or overloads, using relays or fuses that operate when current exceeds a set threshold.",
             "protection", {"Relay", "Fuse", "Fault Current", "Inverse Time"}, "", "", "", "", 10});

    addTerm({"Differential Protection", "A protection scheme that compares currents entering and leaving a protected zone. Any difference indicates an internal fault.",
             "protection", {"Relay", "Transformer Protection", "Bus Protection"}, "", "", "", "", 10});

    addTerm({"Distance Protection", "A protection method that measures the impedance between the relay location and the fault point. The relay operates when the measured impedance falls within a set zone.",
             "protection", {"Relay", "Impedance", "Zone", "Transmission Line"}, "", "", "Z = V_fault/I_fault", "", 9});

    addTerm({"Zone of Protection", "A defined section of the power system that a particular protective device is responsible for protecting. Zones are designed to overlap to ensure complete coverage.",
             "protection", {"Selectivity", "Relay", "Coordination"}, "", "", "", "", 9});

    addTerm({"Time-Current Characteristic", "A curve showing the relationship between fault current magnitude and the operating time of a protective device. Types include definite time, inverse, and extremely inverse.",
             "protection", {"Overcurrent", "Relay", "Coordination", "TCC"}, "TCC", "Time-Current Characteristic", "", "", 9});

    addTerm({"Recloser", "A self-contained protective device that automatically opens and recloses a circuit a predetermined number of times to clear temporary faults on distribution lines.",
             "protection", {"Distribution", "Temporary Fault", "Auto-reclose"}, "", "", "", "", 8});

    addTerm({"Fuse", "A protective device containing a metal strip that melts when current exceeds a rated value, permanently opening the circuit. Provides simple, low-cost overcurrent protection.",
             "protection", {"Overcurrent", "Protection", "Melting"}, "", "", "", "", 8});

    addTerm({"Ground Fault", "An unintentional electrical path between an energized conductor and ground, which can cause equipment damage, fires, and electric shock hazards.",
             "protection", {"Fault", "Protection", "Residual Current", "Earth"}, "GF", "Ground Fault", "", "", 10});

    addTerm({"Arc Flash", "A dangerous electrical explosion caused by a low-impedance connection through air to ground or another voltage phase. Temperatures can exceed 35,000 degrees Fahrenheit.",
             "protection", {"Safety", "Fault", "Hazard", "PPE"}, "", "", "", "", 10});

    addTerm({"Islanding", "A condition where a portion of the power system becomes electrically isolated from the main grid but continues to be energized by local generation.",
             "protection", {"Distributed Generation", "Grid", "Anti-islanding"}, "", "", "", "", 8});
}

void GlossaryManager::addTransmissionTerms() {
    addTerm({"Transmission Line", "High-voltage lines that carry bulk electric power over long distances from generating stations to distribution substations. Typically operated at 69 kV and above.",
             "transmission", {"Distribution", "Voltage", "Conductor", "Tower"}, "", "", "", "", 10});

    addTerm({"Surge Impedance", "The ratio of voltage to current for a wave propagating on a lossless transmission line, measured in ohms. Zc = sqrt(L/C).",
             "transmission", {"Transmission Line", "Surge Impedance Loading", "Characteristic Impedance"}, "Zc", "Surge Impedance", "Zc = sqrt(L/C)", "Omega", 8});

    addTerm({"Surge Impedance Loading", "The power delivered by a transmission line when terminated by its surge impedance, resulting in a flat voltage profile. SIL = V^2 / Zc.",
             "transmission", {"Surge Impedance", "Transmission Line", "Loading"}, "SIL", "Surge Impedance Loading", "SIL = V^2/Zc", "MW", 8});

    addTerm({"ABCD Parameters", "Transmission line parameters that relate sending-end and receiving-end voltages and currents. Also called generalized circuit constants.",
             "transmission", {"Transmission Line", "Two-Port Network", "Cascade"}, "", "", "", "", 7});

    addTerm({"Shunt Compensation", "The use of shunt-connected capacitors or reactors to improve voltage regulation and power factor on transmission lines.",
             "transmission", {"Series Compensation", "FACTS", "Voltage Control", "Reactive Power"}, "", "", "", "", 8});

    addTerm({"Series Compensation", "The insertion of series capacitors in transmission lines to reduce the effective line reactance, increasing power transfer capability.",
             "transmission", {"Shunt Compensation", "FACTS", "Power Transfer"}, "", "", "", "", 8});

    addTerm({"Corona", "A luminous electrical discharge that occurs when the electric field at the surface of a conductor exceeds the dielectric strength of air, causing power loss and audible noise.",
             "transmission", {"Transmission Line", "Electric Field", "Loss"}, "", "", "", "", 7});

    addTerm({"Skin Effect", "The tendency of alternating current to concentrate near the surface of a conductor at high frequencies, effectively increasing the AC resistance.",
             "transmission", {"Resistance", "AC", "Conductor"}, "", "", "", "", 7});

    addTerm({"Bundled Conductor", "A group of two or more conductors per phase used in high-voltage transmission lines to reduce corona loss and improve power transfer capability.",
             "transmission", {"Conductor", "Corona", "Transmission Line"}, "", "", "", "", 7});

    addTerm({"FACTS", "Flexible AC Transmission Systems - power electronic devices used to enhance the controllability and increase the power transfer capability of AC transmission networks.",
             "transmission", {"SVC", "STATCOM", "UPFC", "Power Electronics"}, "FACTS", "Flexible AC Transmission Systems", "", "", 9});

    addTerm({"SVC", "Static VAR Compensator - a FACTS device that uses thyristor-controlled reactors and capacitors to provide dynamic reactive power compensation for voltage control.",
             "transmission", {"FACTS", "Reactive Power", "Shunt Compensation", "Thyristor"}, "SVC", "Static VAR Compensator", "", "MVAR", 8});

    addTerm({"STATCOM", "Static Synchronous Compensator - a voltage-source converter-based FACTS device that provides dynamic reactive power compensation with faster response than SVC.",
             "transmission", {"FACTS", "Reactive Power", "VSC", "Shunt Compensation"}, "STATCOM", "Static Synchronous Compensator", "", "MVAR", 8});
}

void GlossaryManager::addDistributionTerms() {
    addTerm({"Distribution System", "The portion of the power system that delivers electricity from transmission substations to end consumers, typically at voltages below 69 kV.",
             "distribution", {"Transmission", "Substation", "Feeder", "Consumer"}, "", "", "", "", 10});

    addTerm({"Feeder", "A distribution circuit that carries power from a substation to the load area. Radial feeders are the simplest, while loop and network feeders offer higher reliability.",
             "distribution", {"Distribution", "Substation", "Radial", "Loop"}, "", "", "", "", 9});

    addTerm({"Radial Distribution", "A distribution system topology where each load is connected to a single power source through a single path. Simple and economical but less reliable.",
             "distribution", {"Feeder", "Loop", "Network", "Reliability"}, "", "", "", "", 8});

    addTerm({"Substation", "A facility where voltage is transformed from high to low or vice versa, and where switching, protection, and control equipment is located.",
             "distribution", {"Transformer", "Circuit Breaker", "Bus", "Distribution"}, "", "", "", "", 10});

    addTerm({"Smart Grid", "An electrical grid that uses information and communication technology to gather and act on information about the behavior of suppliers and consumers to improve efficiency and reliability.",
             "distribution", {"AMI", "Smart Meter", "Demand Response", "Automation"}, "", "", "", "", 9});

    addTerm({"Smart Meter", "An electronic device that records energy consumption at regular intervals and communicates the data to the utility for monitoring and billing.",
             "distribution", {"Smart Grid", "AMI", "Metering"}, "", "", "", "", 8});

    addTerm({"Demand Response", "Programs that incentivize consumers to reduce or shift electricity usage during peak demand periods to improve system reliability and reduce costs.",
             "distribution", {"Smart Grid", "Load Management", "Peak Shaving"}, "DR", "Demand Response", "", "", 8});

    addTerm({"Distributed Generation", "Small-scale electricity generation connected to the distribution network, typically near the point of consumption. Includes solar PV, wind, and CHP systems.",
             "distribution", {"DG", "Renewable", "Solar", "Wind"}, "DG", "Distributed Generation", "", "", 9});

    addTerm({"Power Quality", "The concept of powering and grounding sensitive electronic equipment in a manner suitable for the equipment. Includes voltage sags, swells, harmonics, and flicker.",
             "distribution", {"Harmonics", "THD", "Sag", "Swell", "Flicker"}, "PQ", "Power Quality", "", "", 9});

    addTerm({"Harmonics", "Sinusoidal voltage or current components at integer multiples of the fundamental frequency, caused by nonlinear loads. Measured as Total Harmonic Distortion (THD).",
             "distribution", {"Power Quality", "THD", "Nonlinear Load", "Fourier"}, "", "", "", "", 9});

    addTerm({"Total Harmonic Distortion", "A measure of the harmonic content in a waveform, defined as the ratio of the RMS sum of all harmonic components to the RMS value of the fundamental component.",
             "distribution", {"Harmonics", "Power Quality", "Distortion"}, "THD", "Total Harmonic Distortion", "THD = sqrt(sum(H_n^2))/H_1", "%", 9});

    addTerm({"Voltage Sag", "A short-duration reduction in RMS voltage, typically lasting from half a cycle to a few seconds, caused by faults or large load switching.",
             "distribution", {"Power Quality", "Voltage Swell", "Dip"}, "", "", "", "", 8});

    addTerm({"Voltage Swell", "A short-duration increase in RMS voltage, typically caused by sudden load rejection or energization of large capacitor banks.",
             "distribution", {"Power Quality", "Voltage Sag", "Overvoltage"}, "", "", "", "", 7});

    addTerm({"Flicker", "Rapid, repeated variations in luminance caused by voltage fluctuations, perceptible to the human eye and potentially annoying or harmful.",
             "distribution", {"Power Quality", "Voltage Fluctuation", "Pst"}, "", "", "", "", 7});

    addTerm({"Load Balancing", "The practice of distributing electrical loads evenly across the three phases of a distribution system to minimize neutral currents and improve efficiency.",
             "distribution", {"Three-Phase", "Neutral", "Efficiency"}, "", "", "", "", 8});
}

void GlossaryManager::addTransformersTerms() {
    addTerm({"Transformer", "A static electrical device that transfers electrical energy between two or more circuits through electromagnetic induction, changing voltage and current levels.",
             "transformers", {"Induction", "Turns Ratio", "Core", "Winding"}, "", "", "", "", 10});

    addTerm({"Turns Ratio", "The ratio of the number of turns in the primary winding to the number of turns in the secondary winding. Determines the voltage transformation ratio.",
             "transformers", {"Transformer", "Voltage Ratio", "Winding"}, "a", "Turns Ratio", "a = N1/N2 = V1/V2", "", 9});

    addTerm({"Autotransformer", "A transformer with a single winding that serves as both primary and secondary, with a portion of the winding common to both circuits. More economical for small voltage ratios.",
             "transformers", {"Transformer", "Tap Changer", "Voltage Regulation"}, "", "", "", "", 8});

    addTerm({"Tap Changer", "A device on a transformer that allows the turns ratio to be adjusted by changing the effective number of turns in a winding, providing voltage regulation.",
             "transformers", {"Transformer", "Voltage Regulation", "OLTC"}, "", "", "", "", 8});

    addTerm({"OLTC", "On-Load Tap Changer - a tap changer that can operate while the transformer is energized and carrying load, providing continuous voltage regulation.",
             "transformers", {"Tap Changer", "Transformer", "Voltage Regulation"}, "OLTC", "On-Load Tap Changer", "", "", 8});

    addTerm({"Buchholz Relay", "A gas-actuated relay installed on oil-filled transformers that detects internal faults by sensing gas accumulation or oil flow, triggering an alarm or trip.",
             "transformers", {"Transformer", "Protection", "Oil", "Gas"}, "", "", "", "", 8});

    addTerm({"Transformer Inrush Current", "A transient high current that flows when a transformer is first energized due to core saturation, typically 8-30 times the rated current for a few cycles.",
             "transformers", {"Transformer", "Saturation", "Transient", "Harmonics"}, "", "", "", "", 7});

    addTerm({"Core Loss", "Power loss in a transformer core due to hysteresis and eddy currents in the magnetic material, also called iron loss or no-load loss.",
             "transformers", {"Transformer", "Hysteresis", "Eddy Current", "Efficiency"}, "", "", "", "W", 8});

    addTerm({"Copper Loss", "Power loss in transformer windings due to the resistance of the conductor material, proportional to the square of the load current. Also called load loss.",
             "transformers", {"Transformer", "Resistance", "I2R", "Efficiency"}, "", "", "P_cu = I^2*R", "W", 8});

    addTerm({"Efficiency", "The ratio of output power to input power of a transformer, typically expressed as a percentage. eta = P_out / P_in * 100%.",
             "transformers", {"Transformer", "Loss", "Core Loss", "Copper Loss"}, "eta", "Efficiency", "eta = P_out/P_in * 100%", "%", 9});

    addTerm({"Vector Group", "A notation system describing the phase displacement between primary and secondary winding voltages of a three-phase transformer.",
             "transformers", {"Transformer", "Three-Phase", "Phase Shift"}, "", "", "", "", 7});
}

void GlossaryManager::addMotorsTerms() {
    addTerm({"Induction Motor", "An AC motor where torque is produced by electromagnetic induction from the magnetic field of the stator winding. The most common type of industrial motor.",
             "motors", {"Motor", "AC", "Slip", "Rotor", "Stator"}, "IM", "Induction Motor", "", "", 10});

    addTerm({"Synchronous Motor", "An AC motor that operates at a constant speed synchronized with the supply frequency. The rotor speed equals the synchronous speed: Ns = 120*f/P.",
             "motors", {"Motor", "AC", "Synchronous Speed", "Excitation"}, "SM", "Synchronous Motor", "Ns = 120f/P", "rpm", 9});

    addTerm({"Synchronous Speed", "The speed at which the rotating magnetic field of an AC motor revolves, determined by the supply frequency and number of poles. Ns = 120*f/P.",
             "motors", {"Induction Motor", "Slip", "Frequency", "Poles"}, "Ns", "Synchronous Speed", "Ns = 120*f/P", "rpm", 9});

    addTerm({"Slip", "The difference between synchronous speed and actual rotor speed in an induction motor, expressed as a percentage of synchronous speed. s = (Ns - N) / Ns.",
             "motors", {"Induction Motor", "Synchronous Speed", "Rotor"}, "s", "Slip", "s = (Ns - N)/Ns", "%", 9});

    addTerm({"Starting Torque", "The torque produced by a motor at the instant of starting (zero speed). It must exceed the load torque for the motor to accelerate.",
             "motors", {"Motor", "Torque", "Inrush", "Acceleration"}, "", "", "", "N.m", 8});

    addTerm({"Locked Rotor Current", "The current drawn by a motor when the rotor is stationary and rated voltage is applied, typically 5-8 times the full-load current.",
             "motors", {"Motor", "Starting", "Current", "Inrush"}, "LRC", "Locked Rotor Current", "", "A", 8});

    addTerm({"Torque-Slip Characteristic", "A curve showing the relationship between motor torque and slip, used to analyze motor performance and stability.",
             "motors", {"Induction Motor", "Slip", "Torque", "Performance"}, "", "", "", "", 7});

    addTerm({"Variable Frequency Drive", "A motor controller that varies the frequency and voltage supplied to an AC motor to control its speed and torque.",
             "motors", {"VFD", "Speed Control", "Inverter", "Motor"}, "VFD", "Variable Frequency Drive", "", "", 9});

    addTerm({"DC Motor", "A motor that converts direct current electrical energy into mechanical energy. Types include series, shunt, and compound wound motors.",
             "motors", {"Motor", "DC", "Commutator", "Armature"}, "DCM", "DC Motor", "", "", 8});

    addTerm({"Servo Motor", "A rotary actuator that allows precise control of angular position, velocity, and acceleration. Used in automation, robotics, and CNC applications.",
             "motors", {"Motor", "Control", "Automation", "Position"}, "", "", "", "", 7});

    addTerm({"Stepper Motor", "A brushless DC electric motor that divides a full rotation into equal steps, allowing precise position control without feedback (open loop).",
             "motors", {"Motor", "Step", "Position Control", "Open Loop"}, "", "", "", "", 7});

    addTerm({"Power Factor of Motor", "The ratio of real power to apparent power consumed by a motor. Induction motors typically operate at a lagging power factor of 0.7-0.9 at full load.",
             "motors", {"Motor", "Power Factor", "Reactive Power", "Efficiency"}, "", "", "", "", 8});

    addTerm({"Motor Efficiency Class", "International efficiency classification for motors: IE1 (Standard), IE2 (High), IE3 (Premium), IE4 (Super Premium), and IE5 (Ultra Premium).",
             "motors", {"Motor", "Efficiency", "IEC", "Classification"}, "IE", "International Efficiency", "", "", 8});
}

void GlossaryManager::addMeasurementTerms() {
    addTerm({"Multimeter", "An electronic measuring instrument that combines several measurement functions in one unit, typically voltage, current, and resistance.",
             "measurement", {"Measurement", "Voltage", "Current", "Resistance"}, "", "", "", "", 7});

    addTerm({"Oscilloscope", "An electronic test instrument that graphically displays varying signal voltages as a two-dimensional plot of one or more signals as a function of time.",
             "measurement", {"Measurement", "Waveform", "Signal", "Time Domain"}, "", "", "", "", 7});

    addTerm({"Clamp Meter", "An electrical tester that combines a current sensor with a basic multimeter, allowing current measurement without breaking the circuit.",
             "measurement", {"Measurement", "Current", "Non-invasive"}, "", "", "", "", 7});

    addTerm({"Power Analyzer", "An instrument for measuring electrical power parameters including voltage, current, power factor, harmonics, and energy consumption.",
             "measurement", {"Measurement", "Power", "Harmonics", "Energy"}, "", "", "", "", 8});

    addTerm({"Phasor Measurement Unit", "A device that measures electrical waves on an electricity grid using a common time source for synchronization, enabling synchronized comparison across the grid.",
             "measurement", {"PMU", "Synchrophasor", "WAMS", "GPS"}, "PMU", "Phasor Measurement Unit", "", "", 9});

    addTerm({"WAMS", "Wide Area Measurement System - a network of PMUs and communication infrastructure that provides real-time monitoring and control of large-scale power systems.",
             "measurement", {"PMU", "Monitoring", "Control", "Grid"}, "WAMS", "Wide Area Measurement System", "", "", 8});

    addTerm({"Energy Meter", "A device that measures the amount of electrical energy consumed by a residence, business, or electrically-powered device. Modern meters are electronic and support smart grid functions.",
             "measurement", {"Measurement", "Energy", "kWh", "Smart Meter"}, "", "", "", "", 8});

    addTerm({"CT", "Current Transformer - an instrument transformer used to step down high currents to measurable levels, providing isolation and standard outputs (typically 1A or 5A).",
             "measurement", {"Transformer", "Current", "Measurement", "Protection"}, "CT", "Current Transformer", "", "", 9});

    addTerm({"PT", "Potential Transformer (or Voltage Transformer) - an instrument transformer used to step down high voltages to standard low voltages (typically 110V or 120V) for measurement and protection.",
             "measurement", {"Transformer", "Voltage", "Measurement", "Protection"}, "PT", "Potential Transformer", "", "", 9});
}

void GlossaryManager::addSoftwareTerms() {
    addTerm({"LSP", "Language Server Protocol - an open protocol for providing programming language smartness features (completion, hover, definition) between editors and language servers.",
             "software", {"Protocol", "IDE", "Editor", "Completion"}, "LSP", "Language Server Protocol", "", "", 10});

    addTerm({"DAP", "Debug Adapter Protocol - a standardized protocol for how a development tool communicates with a debugger, enabling IDE-agnostic debugging support.",
             "software", {"Protocol", "Debugger", "IDE", "Breakpoint"}, "DAP", "Debug Adapter Protocol", "", "", 10});

    addTerm({"JSON-RPC", "A remote procedure call protocol encoded in JSON. Used by LSP for communication between the IDE and language servers.",
             "software", {"Protocol", "JSON", "RPC", "LSP"}, "", "", "", "", 9});

    addTerm({"IDE", "Integrated Development Environment - a software application that provides comprehensive facilities to programmers for software development, including editor, debugger, and build tools.",
             "software", {"Editor", "Debugger", "Development", "Tools"}, "IDE", "Integrated Development Environment", "", "", 9});

    addTerm({"Git", "A distributed version control system that tracks changes in source code during software development, supporting collaboration and history management.",
             "software", {"VCS", "Version Control", "Repository", "Branch"}, "", "", "", "", 9});

    addTerm({"OpenAL", "Open Audio Library - a cross-platform 3D audio API designed for gaming and multimedia applications, providing spatial audio capabilities.",
             "software", {"Audio", "3D", "Sound", "API"}, "", "", "", "", 8});

    addTerm({"CMake", "A cross-platform build system generator that uses configuration files (CMakeLists.txt) to generate native build scripts for various platforms.",
             "software", {"Build", "Compilation", "Make", "Generator"}, "", "", "", "", 8});

    addTerm({"Qt", "A cross-platform application framework for developing GUI applications and non-GUI programs using C++ with bindings for various languages.",
             "software", {"Framework", "GUI", "C++", "Widgets"}, "", "", "", "", 8});

    addTerm({"FTS5", "Full-Text Search version 5 - an SQLite extension module that provides advanced full-text search capabilities, used for indexing and searching documentation.",
             "software", {"Search", "SQLite", "Index", "Text"}, "", "", "", "", 8});

    addTerm({"MeiliSearch", "An open-source, lightning-fast search engine that provides relevant search results with typo tolerance, faceting, and filtering capabilities.",
             "software", {"Search", "Engine", "Full-Text", "Indexing"}, "", "", "", "", 8});

    addTerm({"Breakpoint", "A marker set by a programmer in source code that causes the debugger to pause execution at that point, allowing inspection of program state.",
             "software", {"Debugger", "DAP", "Debug", "IDE"}, "", "", "", "", 8});

    addTerm({"Code Completion", "An IDE feature that suggests possible completions for partially typed code, based on context, language semantics, and available symbols.",
             "software", {"LSP", "IDE", "Editor", "IntelliSense"}, "", "", "", "", 8});

    addTerm({"Refactoring", "The process of restructuring existing code without changing its external behavior, to improve readability, reduce complexity, or improve maintainability.",
             "software", {"Code", "Restructuring", "IDE", "Maintenance"}, "", "", "", "", 7});
}

} // namespace powsys365::help
