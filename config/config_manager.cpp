#include "config_manager.h"
#include "panels/ai_config.h"
#include "panels/ui_config.h"
#include "panels/payment_config.h"
#include "panels/ide_config.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace powsys365::config {

namespace fs = std::filesystem;

/* ================================================================
   PanelId helpers
   ================================================================ */

std::string
ConfigManager::panelIdToString(PanelId id)
{
    switch (id) {
        case PanelId::AI:      return "ai";
        case PanelId::UI:      return "ui";
        case PanelId::Payment: return "payment";
        case PanelId::IDE:     return "ide";
        case PanelId::iTalk:   return "italk";
        case PanelId::Sounds:  return "sounds";
        case PanelId::OSS:     return "oss";
    }
    return "unknown";
}

ConfigManager::PanelId
ConfigManager::stringToPanelId(const std::string& s)
{
    if (s == "ai")       return PanelId::AI;
    if (s == "ui")       return PanelId::UI;
    if (s == "payment")  return PanelId::Payment;
    if (s == "ide")      return PanelId::IDE;
    if (s == "italk")    return PanelId::iTalk;
    if (s == "sounds")   return PanelId::Sounds;
    if (s == "oss")      return PanelId::OSS;
    throw std::invalid_argument("unknown panel id: " + s);
}

/* ================================================================
   JSON helpers (lightweight – no external dependency)
   ================================================================ */

namespace {

// Simple JSON escaping
std::string jsonEscape(const std::string& s)
{
    std::string r;
    r.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b";  break;
            case '\f': r += "\\f";  break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;      break;
        }
    }
    return r;
}

// Simple JSON key-value builder
void jsonBeginObject(std::ostringstream& oss) { oss << "{"; }
void jsonEndObject(std::ostringstream& oss)   { oss << "}"; }
void jsonKey(std::ostringstream& oss, const std::string& key) {
    oss << "\\\"" << jsonEscape(key) << "\\\":";
}
void jsonString(std::ostringstream& oss, const std::string& val) {
    oss << "\\\"" << jsonEscape(val) << "\\\"";
}
void jsonNumber(std::ostringstream& oss, double val) {
    oss << std::fixed << std::setprecision(6) << val;
}
void jsonInt(std::ostringstream& oss, int val) { oss << val; }
void jsonBool(std::ostringstream& oss, bool val) { oss << (val ? "true" : "false"); }
void jsonComma(std::ostringstream& oss) { oss << ","; }

// Simple JSON parser helpers
struct JSONParser {
    const char* data;
    size_t len;
    size_t pos = 0;

    explicit JSONParser(const std::string& s) : data(s.c_str()), len(s.size()) {}

    void skipWhitespace() {
        while (pos < len && (data[pos] == ' ' || data[pos] == '\t'
                          || data[pos] == '\n' || data[pos] == '\r')) ++pos;
    }

    bool match(char c) {
        skipWhitespace();
        if (pos < len && data[pos] == c) { ++pos; return true; }
        return false;
    }

    std::optional<std::string> parseString() {
        skipWhitespace();
        if (pos >= len || data[pos] != '"') return std::nullopt;
        ++pos; // skip opening quote
        std::string result;
        while (pos < len && data[pos] != '"') {
            if (data[pos] == '\\' && pos + 1 < len) {
                ++pos;
                switch (data[pos]) {
                    case '"': case '\\': case '/': result += data[pos]; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += data[pos]; break;
                }
            } else {
                result += data[pos];
            }
            ++pos;
        }
        if (pos < len) ++pos; // skip closing quote
        return result;
    }

    std::optional<std::string> parseValue() {
        skipWhitespace();
        if (pos >= len) return std::nullopt;
        if (data[pos] == '"') return parseString();
        // Parse number, bool, null
        size_t start = pos;
        if (data[pos] == 't' || data[pos] == 'f' || data[pos] == 'n') {
            while (pos < len && data[pos] != ',' && data[pos] != '}' && data[pos] != ']') ++pos;
            return std::string(data + start, pos - start);
        }
        while (pos < len && (data[pos] == '-' || data[pos] == '+' || data[pos] == '.'
               || (data[pos] >= '0' && data[pos] <= '9')
               || data[pos] == 'e' || data[pos] == 'E')) ++pos;
        return std::string(data + start, pos - start);
    }

    // Extract a specific field value as string
    std::string extractField(const std::string& fieldName) {
        pos = 0;
        skipWhitespace();
        if (!match('{')) return "";
        while (pos < len) {
            auto key = parseString();
            if (!key) break;
            skipWhitespace();
            if (!match(':')) break;
            if (key == fieldName) {
                auto val = parseValue();
                return val.value_or("");
            }
            // Skip value
            auto val = parseValue();
            (void)val;
            skipWhitespace();
            if (!match(',')) break;
        }
        return "";
    }
};

} // anonymous namespace

/* ================================================================
   PIMPL implementation
   ================================================================ */

class ConfigManager::Impl {
public:
    std::string configPath_;
    bool autoSave_ = false;
    ChangeCallback changeCb_;

    std::unique_ptr<AIConfigPanel>      aiPanel_;
    std::unique_ptr<UIConfigPanel>      uiPanel_;
    std::unique_ptr<PaymentConfigPanel> paymentPanel_;
    std::unique_ptr<IDEConfigPanel>     idePanel_;

    // Simple in-memory storage for panels not yet with dedicated classes
    std::unordered_map<std::string, std::string> rawPanels_;

    Impl(const std::string& path)
        : configPath_(path)
        , aiPanel_(std::make_unique<AIConfigPanel>())
        , uiPanel_(std::make_unique<UIConfigPanel>())
        , paymentPanel_(std::make_unique<PaymentConfigPanel>())
        , idePanel_(std::make_unique<IDEConfigPanel>())
    {
        // Ensure directory exists
        if (!fs::exists(configPath_)) {
            fs::create_directories(configPath_);
        }
    }
};

/* ================================================================
   Construction / destruction
   ================================================================ */

ConfigManager::ConfigManager(const std::string& configPath)
    : pImpl(std::make_unique<Impl>(configPath)) {}

ConfigManager::~ConfigManager() = default;

ConfigManager::ConfigManager(ConfigManager&&) noexcept = default;
ConfigManager& ConfigManager::operator=(ConfigManager&&) noexcept = default;

/* ================================================================
   Panel access
   ================================================================ */

AIConfigPanel&
ConfigManager::aiPanel()
{
    return *pImpl->aiPanel_;
}

UIConfigPanel&
ConfigManager::uiPanel()
{
    return *pImpl->uiPanel_;
}

PaymentConfigPanel&
ConfigManager::paymentPanel()
{
    return *pImpl->paymentPanel_;
}

IDEConfigPanel&
ConfigManager::idePanel()
{
    return *pImpl->idePanel_;
}

std::string
ConfigManager::getPanel(PanelId id) const
{
    switch (id) {
        case PanelId::AI:      return pImpl->aiPanel_->toJSON();
        case PanelId::UI:      return pImpl->uiPanel_->toJSON();
        case PanelId::Payment: return pImpl->paymentPanel_->toJSON();
        case PanelId::IDE:     return pImpl->idePanel_->toJSON();
        case PanelId::iTalk:
        case PanelId::Sounds:
        case PanelId::OSS: {
            auto it = pImpl->rawPanels_.find(panelIdToString(id));
            if (it != pImpl->rawPanels_.end()) return it->second;
            return "{}";
        }
    }
    return "{}";
}

void
ConfigManager::savePanel(PanelId id, const std::string& jsonSettings)
{
    switch (id) {
        case PanelId::AI:      pImpl->aiPanel_->fromJSON(jsonSettings); break;
        case PanelId::UI:      pImpl->uiPanel_->fromJSON(jsonSettings); break;
        case PanelId::Payment: pImpl->paymentPanel_->fromJSON(jsonSettings); break;
        case PanelId::IDE:     pImpl->idePanel_->fromJSON(jsonSettings); break;
        case PanelId::iTalk:
        case PanelId::Sounds:
        case PanelId::OSS:
            pImpl->rawPanels_[panelIdToString(id)] = jsonSettings;
            break;
    }
    notifyChanged(id, "all");
    if (pImpl->autoSave_) {
        // Save just this panel
        std::string filename = pImpl->configPath_ + "/" + panelIdToString(id) + ".json";
        std::ofstream ofs(filename);
        if (ofs) ofs << jsonSettings;
    }
}

bool
ConfigManager::hasUnsavedChanges(PanelId id) const
{
    switch (id) {
        case PanelId::AI:      return pImpl->aiPanel_->hasChanges();
        case PanelId::UI:      return pImpl->uiPanel_->hasChanges();
        case PanelId::Payment: return pImpl->paymentPanel_->hasChanges();
        case PanelId::IDE:     return pImpl->idePanel_->hasChanges();
        default: return false;
    }
}

void
ConfigManager::resetPanel(PanelId id)
{
    switch (id) {
        case PanelId::AI:      pImpl->aiPanel_->resetToDefaults(); break;
        case PanelId::UI:      pImpl->uiPanel_->resetToDefaults(); break;
        case PanelId::Payment: pImpl->paymentPanel_->resetToDefaults(); break;
        case PanelId::IDE:     pImpl->idePanel_->resetToDefaults(); break;
        case PanelId::iTalk:
        case PanelId::Sounds:
        case PanelId::OSS:
            pImpl->rawPanels_[panelIdToString(id)] = "{}";
            break;
    }
}

void
ConfigManager::resetAllPanels()
{
    pImpl->aiPanel_->resetToDefaults();
    pImpl->uiPanel_->resetToDefaults();
    pImpl->paymentPanel_->resetToDefaults();
    pImpl->idePanel_->resetToDefaults();
    pImpl->rawPanels_.clear();
}

/* ================================================================
   Persistence
   ================================================================ */

void
ConfigManager::loadAll()
{
    // Load each panel from its JSON file
    for (auto id : availablePanels()) {
        std::string filename = pImpl->configPath_ + "/" + panelIdToString(id) + ".json";
        std::ifstream ifs(filename);
        if (ifs) {
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            std::string json = buffer.str();
            if (!json.empty()) {
                savePanel(id, json);
            }
        }
    }
}

void
ConfigManager::saveAll()
{
    if (!fs::exists(pImpl->configPath_)) {
        fs::create_directories(pImpl->configPath_);
    }
    for (auto id : availablePanels()) {
        std::string filename = pImpl->configPath_ + "/" + panelIdToString(id) + ".json";
        std::ofstream ofs(filename);
        if (ofs) {
            ofs << getPanel(id);
        }
    }
}

void
ConfigManager::setAutoSave(bool enabled)
{
    pImpl->autoSave_ = enabled;
}

bool
ConfigManager::autoSave() const
{
    return pImpl->autoSave_;
}

void
ConfigManager::setConfigPath(const std::string& path)
{
    pImpl->configPath_ = path;
    if (!fs::exists(pImpl->configPath_)) {
        fs::create_directories(pImpl->configPath_);
    }
}

std::string
ConfigManager::configPath() const
{
    return pImpl->configPath_;
}

/* ================================================================
   Import / Export
   ================================================================ */

bool
ConfigManager::exportSettings(const std::string& filePath) const
{
    std::ofstream ofs(filePath);
    if (!ofs) return false;

    std::ostringstream oss;
    jsonBeginObject(oss);

    bool first = true;
    for (auto id : availablePanels()) {
        if (!first) jsonComma(oss);
        first = false;
        jsonKey(oss, panelIdToString(id));
        // Embed the panel JSON as a string value
        std::string panelJson = getPanel(id);
        // Parse to validate then re-serialise
        oss << panelJson;
    }

    jsonEndObject(oss);
    ofs << oss.str();
    return true;
}

bool
ConfigManager::importSettings(const std::string& filePath)
{
    std::ifstream ifs(filePath);
    if (!ifs) return false;

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json = buffer.str();
    if (json.empty()) return false;

    // Parse the outer object and dispatch to panels
    JSONParser parser(json);
    parser.skipWhitespace();
    if (!parser.match('{')) return false;

    while (parser.pos < parser.len) {
        auto key = parser.parseString();
        if (!key) break;
        parser.skipWhitespace();
        if (!parser.match(':')) break;

        // Parse nested object as raw string
        parser.skipWhitespace();
        if (parser.match('{')) {
            // Count braces to find matching close
            int depth = 1;
            size_t start = parser.pos - 1;
            while (parser.pos < parser.len && depth > 0) {
                if (parser.data[parser.pos] == '{') ++depth;
                else if (parser.data[parser.pos] == '}') --depth;
                ++parser.pos;
            }
            std::string panelJson(parser.data + start, parser.pos - start);
            try {
                PanelId id = stringToPanelId(*key);
                savePanel(id, panelJson);
            } catch (...) {
                pImpl->rawPanels_[*key] = panelJson;
            }
        } else {
            // Skip primitive value
            auto val = parser.parseValue();
            (void)val;
        }

        parser.skipWhitespace();
        if (!parser.match(',')) break;
    }

    return true;
}

bool
ConfigManager::exportPanel(PanelId id, const std::string& filePath) const
{
    std::ofstream ofs(filePath);
    if (!ofs) return false;
    ofs << getPanel(id);
    return true;
}

bool
ConfigManager::importPanel(PanelId id, const std::string& filePath)
{
    std::ifstream ifs(filePath);
    if (!ifs) return false;
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json = buffer.str();
    if (!json.empty()) {
        savePanel(id, json);
        return true;
    }
    return false;
}

std::string
ConfigManager::createBackup() const
{
    // Generate timestamped filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << pImpl->configPath_ << "/backup_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".json";
    std::string backupPath = oss.str();

    if (exportSettings(backupPath)) {
        return backupPath;
    }
    return "";
}

bool
ConfigManager::restoreFromBackup(const std::string& backupPath)
{
    return importSettings(backupPath);
}

/* ================================================================
   Change notifications
   ================================================================ */

void
ConfigManager::onChanged(ChangeCallback cb)
{
    pImpl->changeCb_ = std::move(cb);
}

void
ConfigManager::notifyChanged(PanelId id, const std::string& key)
{
    if (pImpl->changeCb_) {
        pImpl->changeCb_(id, key);
    }
}

/* ================================================================
   Validation
   ================================================================ */

std::vector<std::pair<ConfigManager::PanelId, std::string>>
ConfigManager::validateAll() const
{
    std::vector<std::pair<PanelId, std::string>> errors;

    auto aiErrors = pImpl->aiPanel_->validate();
    for (const auto& e : aiErrors) {
        errors.emplace_back(PanelId::AI, e);
    }

    auto uiErrors = pImpl->uiPanel_->validate();
    for (const auto& e : uiErrors) {
        errors.emplace_back(PanelId::UI, e);
    }

    auto payErrors = pImpl->paymentPanel_->validate();
    for (const auto& e : payErrors) {
        errors.emplace_back(PanelId::Payment, e);
    }

    auto ideErrors = pImpl->idePanel_->validate();
    for (const auto& e : ideErrors) {
        errors.emplace_back(PanelId::IDE, e);
    }

    return errors;
}

size_t
ConfigManager::panelCount() const
{
    return 7; // AI, UI, Payment, IDE, iTalk, Sounds, OSS
}

std::vector<ConfigManager::PanelId>
ConfigManager::availablePanels() const
{
    return {
        PanelId::AI,
        PanelId::UI,
        PanelId::Payment,
        PanelId::IDE,
        PanelId::iTalk,
        PanelId::Sounds,
        PanelId::OSS
    };
}

} // namespace powsys365::config
