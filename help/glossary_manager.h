#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <memory>

namespace powsys365::help {

/**
 * @brief Glossary entry with definition and metadata
 */
struct GlossaryEntry {
    QString term;
    QString definition;
    QString category;     // electrical, mechanical, software, general
    QStringList relatedTerms;
    QString abbreviation;
    QString fullForm;
    QString formula;      // For mathematical formulas
    QString unit;         // SI unit
    int importance = 0;   // 1-10 ranking for search priority

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["term"] = term;
        obj["definition"] = definition;
        obj["category"] = category;
        obj["relatedTerms"] = QJsonArray::fromStringList(relatedTerms);
        obj["abbreviation"] = abbreviation;
        obj["fullForm"] = fullForm;
        obj["formula"] = formula;
        obj["unit"] = unit;
        obj["importance"] = importance;
        return obj;
    }

    static GlossaryEntry fromJson(const QJsonObject& obj) {
        GlossaryEntry entry;
        entry.term = obj.value("term").toString();
        entry.definition = obj.value("definition").toString();
        entry.category = obj.value("category").toString();
        QJsonArray relArr = obj.value("relatedTerms").toArray();
        for (const auto& v : relArr) entry.relatedTerms.append(v.toString());
        entry.abbreviation = obj.value("abbreviation").toString();
        entry.fullForm = obj.value("fullForm").toString();
        entry.formula = obj.value("formula").toString();
        entry.unit = obj.value("unit").toString();
        entry.importance = obj.value("importance").toInt(0);
        return entry;
    }
};

/**
 * @brief Search result for glossary queries
 */
struct GlossarySearchResult {
    GlossaryEntry entry;
    float relevance = 0.0f;
    QString matchType; // "exact", "prefix", "substring", "related", "definition"
};

/**
 * @brief Glossary manager with complete electrical engineering terminology
 *
 * Provides a comprehensive glossary of electrical engineering terms
 * with definitions, formulas, units, and cross-references.
 */
class GlossaryManager : public QObject {
    Q_OBJECT

public:
    explicit GlossaryManager(QObject* parent = nullptr);
    ~GlossaryManager();

    // === Management ===
    bool initialize();
    void addTerm(const GlossaryEntry& entry);
    void addTerm(const QString& term, const QString& definition,
                 const QString& category = "general");
    bool removeTerm(const QString& term);
    bool updateTerm(const GlossaryEntry& entry);
    GlossaryEntry getTerm(const QString& term) const;
    bool hasTerm(const QString& term) const;

    // === Search ===
    QList<GlossarySearchResult> search(const QString& query) const;
    QList<GlossarySearchResult> searchByCategory(const QString& category) const;
    QList<GlossarySearchResult> searchExact(const QString& term) const;
    QList<GlossarySearchResult> fuzzySearch(const QString& query, int maxDistance = 2) const;
    QStringList getRelatedTerms(const QString& term) const;
    QStringList autocomplete(const QString& prefix, int maxResults = 10) const;

    // === Categories ===
    QStringList categories() const;
    int termCount() const;
    int termCountByCategory(const QString& category) const;

    // === Import/Export ===
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);
    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath) const;

    // === Default Glossary ===
    void loadDefaultGlossary();

Q_SIGNALS:
    void termAdded(const QString& term);
    void termRemoved(const QString& term);
    void termUpdated(const QString& term);
    void glossaryLoaded();
    void error(const QString& message);

private:
    class Impl;
    std::unique_ptr<Impl> d;

    void addElectricalTerms();
    void addPowerSystemTerms();
    void addProtectionTerms();
    void addTransmissionTerms();
    void addDistributionTerms();
    void addTransformersTerms();
    void addMotorsTerms();
    void addMeasurementTerms();
    void addSoftwareTerms();
};

} // namespace powsys365::help
