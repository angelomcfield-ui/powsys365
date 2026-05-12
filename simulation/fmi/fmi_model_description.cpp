#include "fmi_model_description.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <numeric>
#include <limits>
#include <iostream>

// ============================================================================
// Minimal XML Parser (no external dependencies)
// ============================================================================

namespace {

// Simple XML node structure
struct XMLNode {
    std::string name;
    std::string text;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<XMLNode> children;
    const XMLNode* parent = nullptr;

    const XMLNode* findChild(const std::string& tagName) const {
        for (const auto& child : children) {
            if (child.name == tagName) return &child;
        }
        return nullptr;
    }

    std::vector<const XMLNode*> findChildren(const std::string& tagName) const {
        std::vector<const XMLNode*> result;
        for (const auto& child : children) {
            if (child.name == tagName) result.push_back(&child);
        }
        return result;
    }

    std::string attr(const std::string& key, const std::string& defaultVal = "") const {
        auto it = attributes.find(key);
        return (it != attributes.end()) ? it->second : defaultVal;
    }

    bool hasAttr(const std::string& key) const {
        return attributes.find(key) != attributes.end();
    }
};

// Trim whitespace
static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// Simple XML parser - handles FMI modelDescription.xml format
class SimpleXMLParser {
public:
    bool parse(const std::string& xml) {
        m_xml = xml;
        m_pos = 0;
        m_root = XMLNode{};
        
        skipBOM();
        skipWhitespace();
        
        if (!parseProlog()) {
            // Not fatal - continue
        }
        
        skipWhitespace();
        return parseElement(m_root, nullptr);
    }

    const XMLNode& root() const { return m_root; }

private:
    std::string m_xml;
    size_t m_pos = 0;
    XMLNode m_root;

    void skipBOM() {
        if (m_xml.size() >= 3 && 
            static_cast<unsigned char>(m_xml[0]) == 0xEF &&
            static_cast<unsigned char>(m_xml[1]) == 0xBB &&
            static_cast<unsigned char>(m_xml[2]) == 0xBF) {
            m_pos += 3;
        }
    }

    void skipWhitespace() {
        while (m_pos < m_xml.size() && isspace(static_cast<unsigned char>(m_xml[m_pos]))) {
            ++m_pos;
        }
    }

    bool parseProlog() {
        if (m_pos + 5 < m_xml.size() && m_xml.substr(m_pos, 5) == "<?xml") {
            m_pos += 5;
            while (m_pos < m_xml.size() && !(m_xml[m_pos-1] == '?' && m_xml[m_pos] == '>')) {
                ++m_pos;
            }
            if (m_pos < m_xml.size()) ++m_pos; // skip '>'
            return true;
        }
        return false;
    }

    bool parseElement(XMLNode& node, const XMLNode* parent) {
        skipWhitespace();
        if (m_pos >= m_xml.size() || m_xml[m_pos] != '<') {
            return false;
        }

        ++m_pos; // skip '<'

        // Check for comment
        if (m_pos + 2 < m_xml.size() && m_xml[m_pos] == '!' && m_xml[m_pos+1] == '-') {
            m_pos += 2;
            while (m_pos + 2 < m_xml.size() && !(m_xml[m_pos] == '-' && m_xml[m_pos+1] == '-' && m_xml[m_pos+2] == '>')) {
                ++m_pos;
            }
            m_pos += 3;
            return parseElement(node, parent); // try next
        }

        // Parse tag name
        size_t nameStart = m_pos;
        while (m_pos < m_xml.size() && !isspace(static_cast<unsigned char>(m_xml[m_pos])) && 
               m_xml[m_pos] != '>' && m_xml[m_pos] != '/') {
            ++m_pos;
        }
        node.name = m_xml.substr(nameStart, m_pos - nameStart);
        node.parent = parent;

        // Parse attributes
        skipWhitespace();
        while (m_pos < m_xml.size() && m_xml[m_pos] != '>' && m_xml[m_pos] != '/') {
            size_t keyStart = m_pos;
            while (m_pos < m_xml.size() && m_xml[m_pos] != '=' && !isspace(static_cast<unsigned char>(m_xml[m_pos])) &&
                   m_xml[m_pos] != '>' && m_xml[m_pos] != '/') {
                ++m_pos;
            }
            std::string key = m_xml.substr(keyStart, m_pos - keyStart);
            skipWhitespace();
            if (m_pos < m_xml.size() && m_xml[m_pos] == '=') {
                ++m_pos;
                skipWhitespace();
                char quote = m_xml[m_pos];
                if (quote == '"' || quote == '\'') {
                    ++m_pos;
                    size_t valStart = m_pos;
                    while (m_pos < m_xml.size() && m_xml[m_pos] != quote) {
                        ++m_pos;
                    }
                    std::string value = m_xml.substr(valStart, m_pos - valStart);
                    node.attributes[key] = value;
                    if (m_pos < m_xml.size()) ++m_pos; // skip quote
                }
            }
            skipWhitespace();
        }

        // Self-closing tag
        if (m_pos < m_xml.size() && m_xml[m_pos] == '/') {
            ++m_pos; // skip '/'
            if (m_pos < m_xml.size() && m_xml[m_pos] == '>') ++m_pos;
            return true;
        }

        // Closing '>'
        if (m_pos < m_xml.size() && m_xml[m_pos] == '>') {
            ++m_pos;
        }

        // Parse children and text
        while (m_pos < m_xml.size()) {
            skipWhitespace();
            if (m_pos + 2 < m_xml.size() && m_xml[m_pos] == '<' && m_xml[m_pos+1] == '/') {
                // End tag
                m_pos += 2; // skip '</'
                while (m_pos < m_xml.size() && m_xml[m_pos] != '>') ++m_pos;
                if (m_pos < m_xml.size()) ++m_pos; // skip '>'
                break;
            }
            if (m_pos < m_xml.size() && m_xml[m_pos] == '<') {
                if (m_pos + 3 < m_xml.size() && m_xml[m_pos+1] == '!' && m_xml[m_pos+2] == '-') {
                    // Comment
                    m_pos += 3;
                    while (m_pos + 2 < m_xml.size() && !(m_xml[m_pos] == '-' && m_xml[m_pos+1] == '-' && m_xml[m_pos+2] == '>')) {
                        ++m_pos;
                    }
                    m_pos += 3;
                    continue;
                }
                XMLNode child;
                if (parseElement(child, &node)) {
                    node.children.push_back(std::move(child));
                }
            } else {
                // Text content
                size_t textStart = m_pos;
                while (m_pos < m_xml.size() && m_xml[m_pos] != '<') {
                    ++m_pos;
                }
                node.text += trim(m_xml.substr(textStart, m_pos - textStart));
            }
        }

        return true;
    }
};

// Read file to string
std::string readFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Safe string to double
double safeStod(const std::string& s, double defaultVal = 0.0) {
    if (s.empty()) return defaultVal;
    try {
        return std::stod(s);
    } catch (...) {
        return defaultVal;
    }
}

// Safe string to uint32_t
uint32_t safeStoul(const std::string& s, uint32_t defaultVal = 0) {
    if (s.empty()) return defaultVal;
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {
        return defaultVal;
    }
}

// Safe string to int64_t
int64_t safeStoll(const std::string& s, int64_t defaultVal = 0) {
    if (s.empty()) return defaultVal;
    try {
        return std::stoll(s);
    } catch (...) {
        return defaultVal;
    }
}

} // anonymous namespace

namespace powsys365::simulation::fmi {

// ============================================================================
// String Conversion Implementation
// ============================================================================

const char* toString(FMIVersion v) {
    switch (v) {
        case FMIVersion::FMI1_0: return "1.0";
        case FMIVersion::FMI2_0: return "2.0";
        case FMIVersion::FMI3_0: return "3.0";
        default: return "unknown";
    }
}

const char* toString(FMIType t) {
    switch (t) {
        case FMIType::Real: return "Real";
        case FMIType::Integer: return "Integer";
        case FMIType::Boolean: return "Boolean";
        case FMIType::String: return "String";
        case FMIType::Enumeration: return "Enumeration";
        case FMIType::Float32: return "Float32";
        case FMIType::Float64: return "Float64";
        case FMIType::Int8: return "Int8";
        case FMIType::UInt8: return "UInt8";
        case FMIType::Int16: return "Int16";
        case FMIType::UInt16: return "UInt16";
        case FMIType::Int32: return "Int32";
        case FMIType::UInt32: return "UInt32";
        case FMIType::Int64: return "Int64";
        case FMIType::UInt64: return "UInt64";
        case FMIType::Binary: return "Binary";
        case FMIType::Clock: return "Clock";
        default: return "unknown";
    }
}

const char* toString(Causality c) {
    switch (c) {
        case Causality::Local: return "local";
        case Causality::Parameter: return "parameter";
        case Causality::CalculatedParameter: return "calculatedParameter";
        case Causality::Input: return "input";
        case Causality::Output: return "output";
        case Causality::Independent: return "independent";
        case Causality::StructuralParameter: return "structuralParameter";
        case Causality::Constant: return "constant";
        default: return "unknown";
    }
}

const char* toString(Variability v) {
    switch (v) {
        case Variability::Constant: return "constant";
        case Variability::Fixed: return "fixed";
        case Variability::Tunable: return "tunable";
        case Variability::Discrete: return "discrete";
        case Variability::Continuous: return "continuous";
        default: return "unknown";
    }
}

const char* toString(Initial i) {
    switch (i) {
        case Initial::Exact: return "exact";
        case Initial::Approx: return "approx";
        case Initial::Calculated: return "calculated";
        case Initial::Automatic: return "automatic";
        default: return "unknown";
    }
}

FMIVersion parseFMIVersion(const std::string& s) {
    if (s == "1.0" || s == "1") return FMIVersion::FMI1_0;
    if (s == "2.0" || s == "2") return FMIVersion::FMI2_0;
    if (s == "3.0" || s == "3") return FMIVersion::FMI3_0;
    // Check prefix
    if (s.size() >= 3 && s[0] == '2' && s[1] == '.') return FMIVersion::FMI2_0;
    if (s.size() >= 3 && s[0] == '3' && s[1] == '.') return FMIVersion::FMI3_0;
    return FMIVersion::Unknown;
}

FMIType parseFMIType(const std::string& s) {
    if (s == "Real") return FMIType::Real;
    if (s == "Integer") return FMIType::Integer;
    if (s == "Boolean") return FMIType::Boolean;
    if (s == "String") return FMIType::String;
    if (s == "Enumeration") return FMIType::Enumeration;
    if (s == "Float32") return FMIType::Float32;
    if (s == "Float64") return FMIType::Float64;
    if (s == "Int8") return FMIType::Int8;
    if (s == "UInt8") return FMIType::UInt8;
    if (s == "Int16") return FMIType::Int16;
    if (s == "UInt16") return FMIType::UInt16;
    if (s == "Int32") return FMIType::Int32;
    if (s == "UInt32") return FMIType::UInt32;
    if (s == "Int64") return FMIType::Int64;
    if (s == "UInt64") return FMIType::UInt64;
    if (s == "Binary") return FMIType::Binary;
    if (s == "Clock") return FMIType::Clock;
    return FMIType::Real; // Default
}

Causality parseCausality(const std::string& s) {
    if (s == "local") return Causality::Local;
    if (s == "parameter") return Causality::Parameter;
    if (s == "calculatedParameter") return Causality::CalculatedParameter;
    if (s == "input") return Causality::Input;
    if (s == "output") return Causality::Output;
    if (s == "independent") return Causality::Independent;
    if (s == "structuralParameter") return Causality::StructuralParameter;
    if (s == "constant") return Causality::Constant;
    return Causality::Local;
}

Variability parseVariability(const std::string& s) {
    if (s == "constant") return Variability::Constant;
    if (s == "fixed") return Variability::Fixed;
    if (s == "tunable") return Variability::Tunable;
    if (s == "discrete") return Variability::Discrete;
    if (s == "continuous") return Variability::Continuous;
    return Variability::Unknown;
}

Initial parseInitial(const std::string& s) {
    if (s == "exact") return Initial::Exact;
    if (s == "approx") return Initial::Approx;
    if (s == "calculated") return Initial::Calculated;
    return Initial::Automatic;
}

// ============================================================================
// FMIVariable Implementation
// ============================================================================

std::string FMIVariable::toString() const {
    std::ostringstream oss;
    oss << "FMIVariable{name='" << name << "', vr=" << valueReference
        << ", type=" << fmi::toString(type)
        << ", causality=" << fmi::toString(causality)
        << ", variability=" << fmi::toString(variability)
        << ", initial=" << fmi::toString(initial);
    if (!unit.empty()) oss << ", unit=" << unit;
    if (type == FMIType::String) {
        oss << ", start='" << startValueString << "'";
    } else if (type == FMIType::Boolean) {
        oss << ", start=" << (startValueBool ? "true" : "false");
    } else if (type == FMIType::Integer) {
        oss << ", start=" << startValueInt;
    } else {
        oss << ", start=" << startValue;
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// FMIModelDescription::Impl - PIMPL Pattern
// ============================================================================

class FMIModelDescription::Impl {
public:
    // Model info
    std::string modelName;
    std::string guid;
    std::string version;
    std::string description;
    std::string author;
    std::string copyright;
    std::string license;
    std::string generationTool;
    std::string generationDateAndTime;

    FMIVersion fmiVersionEnum = FMIVersion::Unknown;
    double numberOfEventIndicatorsVal = 0;
    uint32_t numberOfContinuousStatesVal = 0;

    // Variables
    std::vector<FMIVariable> variablesList;
    std::unordered_map<std::string, size_t> nameIndex;
    std::unordered_map<uint32_t, size_t> vrIndex;

    // Interface info
    ModelExchangeInfo meInfo;
    CoSimulationInfo csInfo;
    ScheduledExecutionInfo seInfo;
    bool hasME = false;
    bool hasCS = false;
    bool hasSE = false;

    // Units
    std::vector<UnitDefinition> units;
    std::unordered_map<std::string, size_t> unitIndex;

    // Default experiment
    bool hasDefaultExperimentFlag = false;
    double defaultStartTimeVal = 0.0;
    double defaultStopTimeVal = 0.0;
    double defaultToleranceVal = 1e-4;
    double defaultStepSizeVal = 0.001;

    // Validation
    bool valid = false;
    std::vector<std::string> errors;

    // Internal methods
    void parseFMI2(const XMLNode& root);
    void parseFMI3(const XMLNode& root);
    void parseModelVariablesFMI2(const XMLNode& mvNode);
    void parseModelVariablesFMI3(const XMLNode& mvNode);
    void parseScalarVariableFMI2(const XMLNode& node);
    void parseVariableFMI3(const XMLNode& node);
    void parseUnitDefinitions(const XMLNode& root);
    void parseUnitDefinitionsFMI3(const XMLNode& root);
    void parseDefaultExperiment(const XMLNode& root);
    void buildIndices();
    void validate();
};

// ============================================================================
// FMIModelDescription Implementation
// ============================================================================

FMIModelDescription::FMIModelDescription() : pImpl(std::make_unique<Impl>()) {}
FMIModelDescription::~FMIModelDescription() = default;
FMIModelDescription::FMIModelDescription(FMIModelDescription&&) noexcept = default;
FMIModelDescription& FMIModelDescription::operator=(FMIModelDescription&&) noexcept = default;

bool FMIModelDescription::parseFromFile(const std::string& xmlPath) {
    std::string content = readFileToString(xmlPath);
    if (content.empty()) {
        pImpl->errors.push_back("Failed to read file: " + xmlPath);
        pImpl->valid = false;
        return false;
    }
    return parseFromString(content);
}

bool FMIModelDescription::parseFromString(const std::string& xmlContent) {
    pImpl->valid = false;
    pImpl->errors.clear();
    pImpl->variablesList.clear();
    pImpl->nameIndex.clear();
    pImpl->vrIndex.clear();
    pImpl->units.clear();
    pImpl->unitIndex.clear();

    SimpleXMLParser parser;
    if (!parser.parse(xmlContent)) {
        pImpl->errors.push_back("Failed to parse XML content");
        return false;
    }

    const XMLNode& root = parser.root();
    if (root.name != "fmiModelDescription") {
        pImpl->errors.push_back("Root element is not 'fmiModelDescription', found: " + root.name);
        return false;
    }

    // Extract basic attributes
    pImpl->modelName = root.attr("modelName");
    pImpl->guid = root.attr("guid");
    pImpl->version = root.attr("version");
    pImpl->description = root.attr("description");
    pImpl->author = root.attr("author");
    pImpl->copyright = root.attr("copyright");
    pImpl->license = root.attr("license");
    pImpl->generationTool = root.attr("generationTool");
    pImpl->generationDateAndTime = root.attr("generationDateAndTime");

    std::string fmiVersionAttr = root.attr("fmiVersion");
    pImpl->fmiVersionEnum = parseFMIVersion(fmiVersionAttr);

    // Parse event indicators and continuous states
    std::string eventInd = root.attr("numberOfEventIndicators");
    if (!eventInd.empty()) {
        pImpl->numberOfEventIndicatorsVal = safeStod(eventInd);
    }

    // Route to version-specific parser
    if (pImpl->fmiVersionEnum == FMIVersion::FMI2_0) {
        pImpl->parseFMI2(root);
    } else if (pImpl->fmiVersionEnum == FMIVersion::FMI3_0) {
        pImpl->parseFMI3(root);
    } else if (pImpl->fmiVersionEnum == FMIVersion::FMI1_0) {
        // For FMI 1.0, try FMI 2.0 parser as they're close
        pImpl->parseFMI2(root);
    } else {
        // Unknown version - try auto-detect
        if (root.findChild("ModelVariables")) {
            pImpl->fmiVersionEnum = FMIVersion::FMI2_0;
            pImpl->parseFMI2(root);
        } else if (root.findChild("ModelVariables")) {
            pImpl->fmiVersionEnum = FMIVersion::FMI3_0;
            pImpl->parseFMI3(root);
        }
    }

    pImpl->parseDefaultExperiment(root);
    pImpl->buildIndices();
    pImpl->validate();

    return pImpl->valid;
}

// ============================================================================
// Accessors
// ============================================================================

const std::string& FMIModelDescription::modelName() const noexcept { return pImpl->modelName; }
const std::string& FMIModelDescription::guid() const noexcept { return pImpl->guid; }
const std::string& FMIModelDescription::version() const noexcept { return pImpl->version; }
const std::string& FMIModelDescription::description() const noexcept { return pImpl->description; }
const std::string& FMIModelDescription::author() const noexcept { return pImpl->author; }
const std::string& FMIModelDescription::copyright() const noexcept { return pImpl->copyright; }
const std::string& FMIModelDescription::license() const noexcept { return pImpl->license; }
const std::string& FMIModelDescription::generationTool() const noexcept { return pImpl->generationTool; }
const std::string& FMIModelDescription::generationDateAndTime() const noexcept { return pImpl->generationDateAndTime; }

FMIVersion FMIModelDescription::fmiVersion() const noexcept { return pImpl->fmiVersionEnum; }
double FMIModelDescription::numberOfEventIndicators() const noexcept { return pImpl->numberOfEventIndicatorsVal; }
uint32_t FMIModelDescription::numberOfContinuousStates() const noexcept { return pImpl->numberOfContinuousStatesVal; }

const std::vector<FMIVariable>& FMIModelDescription::variables() const noexcept {
    return pImpl->variablesList;
}

const FMIVariable* FMIModelDescription::findVariableByName(const std::string& name) const {
    auto it = pImpl->nameIndex.find(name);
    if (it != pImpl->nameIndex.end()) {
        return &pImpl->variablesList[it->second];
    }
    return nullptr;
}

const FMIVariable* FMIModelDescription::findVariableByVR(uint32_t vr) const {
    auto it = pImpl->vrIndex.find(vr);
    if (it != pImpl->vrIndex.end()) {
        return &pImpl->variablesList[it->second];
    }
    return nullptr;
}

const FMIVariable* FMIModelDescription::findVariableByVR(uint32_t vr, FMIType type) const {
    // Find by VR and verify type
    auto it = pImpl->vrIndex.find(vr);
    if (it != pImpl->vrIndex.end()) {
        const FMIVariable* var = &pImpl->variablesList[it->second];
        if (var->type == type) return var;
    }
    return nullptr;
}

std::vector<const FMIVariable*> FMIModelDescription::getInputs() const {
    return getVariablesByCausality(Causality::Input);
}

std::vector<const FMIVariable*> FMIModelDescription::getOutputs() const {
    return getVariablesByCausality(Causality::Output);
}

std::vector<const FMIVariable*> FMIModelDescription::getParameters() const {
    return getVariablesByCausality(Causality::Parameter);
}

std::vector<const FMIVariable*> FMIModelDescription::getContinuousStates() const {
    std::vector<const FMIVariable*> result;
    for (const auto& var : pImpl->variablesList) {
        if (var.variability == Variability::Continuous && 
            (var.causality == Causality::Local || var.causality == Causality::Output)) {
            result.push_back(&var);
        }
    }
    return result;
}

std::vector<const FMIVariable*> FMIModelDescription::getLocalVariables() const {
    return getVariablesByCausality(Causality::Local);
}

std::vector<const FMIVariable*> FMIModelDescription::getVariablesByCausality(Causality c) const {
    std::vector<const FMIVariable*> result;
    for (const auto& var : pImpl->variablesList) {
        if (var.causality == c) result.push_back(&var);
    }
    return result;
}

std::vector<const FMIVariable*> FMIModelDescription::getVariablesByType(FMIType t) const {
    std::vector<const FMIVariable*> result;
    for (const auto& var : pImpl->variablesList) {
        if (var.type == t) result.push_back(&var);
    }
    return result;
}

bool FMIModelDescription::supportsModelExchange() const noexcept { return pImpl->hasME; }
bool FMIModelDescription::supportsCoSimulation() const noexcept { return pImpl->hasCS; }
bool FMIModelDescription::supportsScheduledExecution() const noexcept { return pImpl->hasSE; }

const ModelExchangeInfo& FMIModelDescription::modelExchangeInfo() const noexcept { return pImpl->meInfo; }
const CoSimulationInfo& FMIModelDescription::coSimulationInfo() const noexcept { return pImpl->csInfo; }
const ScheduledExecutionInfo& FMIModelDescription::scheduledExecutionInfo() const noexcept { return pImpl->seInfo; }

const std::vector<UnitDefinition>& FMIModelDescription::unitDefinitions() const noexcept { return pImpl->units; }

const UnitDefinition* FMIModelDescription::findUnit(const std::string& name) const {
    auto it = pImpl->unitIndex.find(name);
    if (it != pImpl->unitIndex.end()) return &pImpl->units[it->second];
    return nullptr;
}

bool FMIModelDescription::hasDefaultExperiment() const noexcept { return pImpl->hasDefaultExperimentFlag; }
double FMIModelDescription::defaultStartTime() const noexcept { return pImpl->defaultStartTimeVal; }
double FMIModelDescription::defaultStopTime() const noexcept { return pImpl->defaultStopTimeVal; }
double FMIModelDescription::defaultTolerance() const noexcept { return pImpl->defaultToleranceVal; }
double FMIModelDescription::defaultStepSize() const noexcept { return pImpl->defaultStepSizeVal; }

bool FMIModelDescription::isValid() const noexcept { return pImpl->valid; }
std::vector<std::string> FMIModelDescription::validationErrors() const { return pImpl->errors; }

// ============================================================================
// Impl: FMI 2.0 Parsing
// ============================================================================

void FMIModelDescription::Impl::parseFMI2(const XMLNode& root) {
    // Parse ModelExchange
    if (const XMLNode* me = root.findChild("ModelExchange")) {
        hasME = true;
        meInfo.modelIdentifier = me->attr("modelIdentifier");
        meInfo.providesDirectionalDerivative = (me->attr("providesDirectionalDerivative") == "true");
        meInfo.needsExecutionTool = (me->attr("needsExecutionTool") == "true");
        meInfo.completedIntegratorStepNotNeeded = (me->attr("completedIntegratorStepNotNeeded") == "true");
        meInfo.canBeInstantiatedOnlyOncePerProcess = (me->attr("canBeInstantiatedOnlyOncePerProcess") == "true");
        meInfo.canNotUseMemoryManagementFunctions = (me->attr("canNotUseMemoryManagementFunctions") == "true");
        meInfo.canGetAndSetFMUState = (me->attr("canGetAndSetFMUState") == "true");
        meInfo.canSerializeFMUState = (me->attr("canSerializeFMUState") == "true");
    }

    // Parse CoSimulation
    if (const XMLNode* cs = root.findChild("CoSimulation")) {
        hasCS = true;
        csInfo.modelIdentifier = cs->attr("modelIdentifier");
        csInfo.canHandleVariableCommunicationStepSize = (cs->attr("canHandleVariableCommunicationStepSize") == "true");
        csInfo.canInterpolateInputs = (cs->attr("canInterpolateInputs") == "true");
        csInfo.maxOutputDerivativeOrder = safeStoul(cs->attr("maxOutputDerivativeOrder"), 0);
        csInfo.canRunAsynchronuously = (cs->attr("canRunAsynchronuously") == "true");
        csInfo.needsExecutionTool = (cs->attr("needsExecutionTool") == "true");
        csInfo.canBeInstantiatedOnlyOncePerProcess = (cs->attr("canBeInstantiatedOnlyOncePerProcess") == "true");
        csInfo.canNotUseMemoryManagementFunctions = (cs->attr("canNotUseMemoryManagementFunctions") == "true");
        csInfo.canGetAndSetFMUState = (cs->attr("canGetAndSetFMUState") == "true");
        csInfo.canSerializeFMUState = (cs->attr("canSerializeFMUState") == "true");
        csInfo.canHandleCoSimulation = true;
        std::string fixedStep = cs->attr("fixedInternalStepSize");
        if (!fixedStep.empty()) {
            csInfo.fixedInternalStepSize = safeStod(fixedStep);
            csInfo.hasFixedInternalStepSize = true;
        }
    }

    // Parse UnitDefinitions
    parseUnitDefinitions(root);

    // Parse ModelVariables
    if (const XMLNode* mv = root.findChild("ModelVariables")) {
        parseModelVariablesFMI2(*mv);
    }

    // Parse numberOfContinuousStates from Derivatives in ModelStructure (FMI 2.0)
    if (const XMLNode* ms = root.findChild("ModelStructure")) {
        auto derivatives = ms->findChildren("Derivative");
        numberOfContinuousStatesVal = static_cast<uint32_t>(derivatives.size());
    }
}

void FMIModelDescription::Impl::parseModelVariablesFMI2(const XMLNode& mvNode) {
    for (const auto& child : mvNode.children) {
        if (child.name == "ScalarVariable") {
            parseScalarVariableFMI2(child);
        }
    }
}

void FMIModelDescription::Impl::parseScalarVariableFMI2(const XMLNode& node) {
    FMIVariable var;
    var.name = node.attr("name");
    var.valueReference = safeStoul(node.attr("valueReference"));
    var.description = node.attr("description");
    var.causality = parseCausality(node.attr("causality"));
    var.variability = parseVariability(node.attr("variability", "continuous"));
    var.initial = parseInitial(node.attr("initial"));

    // Determine type from child element
    for (const auto& child : node.children) {
        if (child.name == "Real") {
            var.type = FMIType::Real;
            var.startValue = safeStod(child.attr("start"), 0.0);
            var.minValue = safeStod(child.attr("min"), -std::numeric_limits<double>::infinity());
            var.maxValue = safeStod(child.attr("max"), std::numeric_limits<double>::infinity());
            var.nominal = safeStod(child.attr("nominal"), 1.0);
            var.unit = child.attr("unit");
            var.displayUnit = child.attr("displayUnit");
            var.derivativeOf = safeStoul(child.attr("derivative"), 0);
        } else if (child.name == "Integer") {
            var.type = FMIType::Integer;
            var.startValueInt = safeStoll(child.attr("start"), 0);
            var.minValueInt = safeStoll(child.attr("min"), std::numeric_limits<int64_t>::min());
            var.maxValueInt = safeStoll(child.attr("max"), std::numeric_limits<int64_t>::max());
        } else if (child.name == "Boolean") {
            var.type = FMIType::Boolean;
            var.startValueBool = (child.attr("start") == "true");
        } else if (child.name == "String") {
            var.type = FMIType::String;
            var.startValueString = child.attr("start");
        } else if (child.name == "Enumeration") {
            var.type = FMIType::Enumeration;
            var.startValueString = child.attr("start");
        }
    }

    // Default type detection
    if (var.type == FMIType::Real && node.children.empty()) {
        // Try to infer from name patterns
        if (var.name.find("Boolean") != std::string::npos ||
            var.name.find("boolean") != std::string::npos ||
            var.name.find("bool") != std::string::npos) {
            var.type = FMIType::Boolean;
        }
    }

    // Auto-determine initial if not specified
    if (var.initial == Initial::Automatic) {
        if (var.causality == Causality::Parameter || var.causality == Causality::Input) {
            var.initial = Initial::Exact;
        } else if (var.causality == Causality::Output || var.causality == Causality::Local) {
            if (var.variability == Variability::Fixed || var.variability == Variability::Constant) {
                var.initial = Initial::Exact;
            } else {
                var.initial = Initial::Calculated;
            }
        }
    }

    variablesList.push_back(std::move(var));
}

// ============================================================================
// Impl: FMI 3.0 Parsing
// ============================================================================

void FMIModelDescription::Impl::parseFMI3(const XMLNode& root) {
    // FMI 3.0 supports multiple interface types
    for (const auto& child : root.children) {
        if (child.name == "ModelExchange") {
            hasME = true;
            meInfo.modelIdentifier = child.attr("modelIdentifier");
            meInfo.providesDirectionalDerivative = (child.attr("providesDirectionalDerivative", "false") == "true");
            meInfo.needsExecutionTool = (child.attr("needsExecutionTool", "false") == "true");
            meInfo.canBeInstantiatedOnlyOncePerProcess = (child.attr("canBeInstantiatedOnlyOncePerProcess", "false") == "true");
            meInfo.canNotUseMemoryManagementFunctions = (child.attr("canNotUseMemoryManagementFunctions", "false") == "true");
            meInfo.canGetAndSetFMUState = (child.attr("canGetAndSetFMUState", "false") == "true");
            meInfo.canSerializeFMUState = (child.attr("canSerializeFMUState", "false") == "true");
            meInfo.providesEvaluateDiscreteStates = (child.attr("providesEvaluateDiscreteStates", "false") == "true");
        } else if (child.name == "CoSimulation") {
            hasCS = true;
            csInfo.modelIdentifier = child.attr("modelIdentifier");
            csInfo.canHandleVariableCommunicationStepSize = (child.attr("canHandleVariableCommunicationStepSize", "false") == "true");
            csInfo.canInterpolateInputs = (child.attr("canInterpolateInputs", "false") == "true");
            csInfo.maxOutputDerivativeOrder = safeStoul(child.attr("maxOutputDerivativeOrder", "0"));
            csInfo.needsExecutionTool = (child.attr("needsExecutionTool", "false") == "true");
            csInfo.canBeInstantiatedOnlyOncePerProcess = (child.attr("canBeInstantiatedOnlyOncePerProcess", "false") == "true");
            csInfo.canNotUseMemoryManagementFunctions = (child.attr("canNotUseMemoryManagementFunctions", "false") == "true");
            csInfo.canGetAndSetFMUState = (child.attr("canGetAndSetFMUState", "false") == "true");
            csInfo.canSerializeFMUState = (child.attr("canSerializeFMUState", "false") == "true");
            csInfo.canHandleCoSimulation = true;
            csInfo.providesIntermediateUpdate = (child.attr("providesIntermediateUpdate", "false") == "true");
            csInfo.mightReturnEarlyFromDoStep = (child.attr("mightReturnEarlyFromDoStep", "false") == "true");
            csInfo.canReturnEarlyAfterIntermediateUpdate = (child.attr("canReturnEarlyAfterIntermediateUpdate", "false") == "true");
            std::string fixedStep = child.attr("fixedInternalStepSize");
            if (!fixedStep.empty()) {
                csInfo.fixedInternalStepSize = safeStod(fixedStep);
                csInfo.hasFixedInternalStepSize = true;
            }
        } else if (child.name == "ScheduledExecution") {
            hasSE = true;
            seInfo.modelIdentifier = child.attr("modelIdentifier");
            seInfo.needsExecutionTool = (child.attr("needsExecutionTool", "false") == "true");
            seInfo.canBeInstantiatedOnlyOncePerProcess = (child.attr("canBeInstantiatedOnlyOncePerProcess", "false") == "true");
            seInfo.canNotUseMemoryManagementFunctions = (child.attr("canNotUseMemoryManagementFunctions", "false") == "true");
            seInfo.canGetAndSetFMUState = (child.attr("canGetAndSetFMUState", "false") == "true");
            seInfo.canSerializeFMUState = (child.attr("canSerializeFMUState", "false") == "true");
        }
    }

    // Parse UnitDefinitions (FMI 3.0 format)
    parseUnitDefinitionsFMI3(root);

    // Parse ModelVariables (FMI 3.0 format)
    if (const XMLNode* mv = root.findChild("ModelVariables")) {
        parseModelVariablesFMI3(*mv);
    }
}

void FMIModelDescription::Impl::parseModelVariablesFMI3(const XMLNode& mvNode) {
    for (const auto& child : mvNode.children) {
        // FMI 3.0 uses typed variable elements: Float64, Int32, Boolean, String, etc.
        if (child.name == "Float64" || child.name == "Float32" ||
            child.name == "Int8" || child.name == "UInt8" ||
            child.name == "Int16" || child.name == "UInt16" ||
            child.name == "Int32" || child.name == "UInt32" ||
            child.name == "Int64" || child.name == "UInt64" ||
            child.name == "Boolean" || child.name == "String" ||
            child.name == "Binary" || child.name == "Clock" ||
            child.name == "Enumeration") {
            parseVariableFMI3(child);
        }
    }
}

void FMIModelDescription::Impl::parseVariableFMI3(const XMLNode& node) {
    FMIVariable var;
    var.name = node.attr("name");
    var.valueReference = safeStoul(node.attr("valueReference"));
    var.description = node.attr("description");
    var.causality = parseCausality(node.attr("causality", "local"));
    var.variability = parseVariability(node.attr("variability", "continuous"));
    var.initial = parseInitial(node.attr("initial"));
    var.type = parseFMIType(node.name);

    // Type-specific parsing
    if (node.name == "Float64" || node.name == "Float32") {
        var.startValue = safeStod(node.attr("start"), 0.0);
        var.minValue = safeStod(node.attr("min"), -std::numeric_limits<double>::infinity());
        var.maxValue = safeStod(node.attr("max"), std::numeric_limits<double>::infinity());
        var.nominal = safeStod(node.attr("nominal"), 1.0);
        var.unit = node.attr("unit");
        var.displayUnit = node.attr("displayUnit");
        var.derivativeOf = safeStoul(node.attr("derivative"), 0);
    } else if (node.name == "Int8" || node.name == "UInt8" ||
               node.name == "Int16" || node.name == "UInt16" ||
               node.name == "Int32" || node.name == "UInt32" ||
               node.name == "Int64" || node.name == "UInt64") {
        var.startValueInt = safeStoll(node.attr("start"), 0);
        var.minValueInt = safeStoll(node.attr("min"), std::numeric_limits<int64_t>::min());
        var.maxValueInt = safeStoll(node.attr("max"), std::numeric_limits<int64_t>::max());
    } else if (node.name == "Boolean") {
        var.startValueBool = (node.attr("start") == "true");
    } else if (node.name == "String") {
        var.startValueString = node.attr("start");
    } else if (node.name == "Binary") {
        var.startValueString = node.attr("start");
    } else if (node.name == "Clock") {
        // Clock variables don't have numeric start values
    } else if (node.name == "Enumeration") {
        var.startValueString = node.attr("start");
    }

    // Auto-determine initial
    if (var.initial == Initial::Automatic) {
        if (var.causality == Causality::Parameter || var.causality == Causality::Input ||
            var.causality == Causality::StructuralParameter) {
            var.initial = Initial::Exact;
        } else if (var.causality == Causality::Constant) {
            var.initial = Initial::Exact;
        } else if (var.causality == Causality::CalculatedParameter) {
            var.initial = Initial::Calculated;
        } else {
            var.initial = Initial::Calculated;
        }
    }

    variablesList.push_back(std::move(var));
}

// ============================================================================
// Impl: Unit Definitions Parsing
// ============================================================================

void FMIModelDescription::Impl::parseUnitDefinitions(const XMLNode& root) {
    const XMLNode* udRoot = root.findChild("UnitDefinitions");
    if (!udRoot) return;

    for (const auto& unitNode : udRoot->children) {
        if (unitNode.name != "Unit") continue;
        
        UnitDefinition unit;
        unit.name = unitNode.attr("name");

        const XMLNode* baseUnit = unitNode.findChild("BaseUnit");
        if (baseUnit) {
            for (const auto& attr : baseUnit->attributes) {
                if (attr.first == "factor") {
                    unit.factor = safeStod(attr.second, 1.0);
                } else if (attr.first == "offset") {
                    unit.offset = safeStod(attr.second, 0.0);
                } else {
                    // Base unit exponents
                    try {
                        unit.baseUnits[attr.first] = std::stoi(attr.second);
                    } catch (...) {
                        unit.baseUnits[attr.first] = 0;
                    }
                }
            }
        }

        // Parse DisplayUnits
        for (const auto& duNode : unitNode.children) {
            if (duNode.name == "DisplayUnit") {
                // Store display unit info (optional, for future use)
                std::string duName = duNode.attr("name");
                double duFactor = safeStod(duNode.attr("factor"), 1.0);
                double duOffset = safeStod(duNode.attr("offset"), 0.0);
                (void)duName; (void)duFactor; (void)duOffset;
            }
        }

        unitIndex[unit.name] = units.size();
        units.push_back(std::move(unit));
    }
}

void FMIModelDescription::Impl::parseUnitDefinitionsFMI3(const XMLNode& root) {
    const XMLNode* udRoot = root.findChild("UnitDefinitions");
    if (!udRoot) return;

    for (const auto& unitNode : udRoot->children) {
        if (unitNode.name != "Unit") continue;
        
        UnitDefinition unit;
        unit.name = unitNode.attr("name");

        const XMLNode* baseUnit = unitNode.findChild("BaseUnit");
        if (baseUnit) {
            for (const auto& attr : baseUnit->attributes) {
                if (attr.first == "factor") {
                    unit.factor = safeStod(attr.second, 1.0);
                } else if (attr.first == "offset") {
                    unit.offset = safeStod(attr.second, 0.0);
                } else {
                    try {
                        unit.baseUnits[attr.first] = std::stoi(attr.second);
                    } catch (...) {
                        unit.baseUnits[attr.first] = 0;
                    }
                }
            }
        }

        unitIndex[unit.name] = units.size();
        units.push_back(std::move(unit));
    }
}

// ============================================================================
// Impl: Default Experiment
// ============================================================================

void FMIModelDescription::Impl::parseDefaultExperiment(const XMLNode& root) {
    const XMLNode* de = root.findChild("DefaultExperiment");
    if (!de) return;

    hasDefaultExperimentFlag = true;
    defaultStartTimeVal = safeStod(de->attr("startTime"), 0.0);
    defaultStopTimeVal = safeStod(de->attr("stopTime"), 1.0);
    defaultToleranceVal = safeStod(de->attr("tolerance"), 1e-4);
    defaultStepSizeVal = safeStod(de->attr("stepSize"), 0.001);
}

// ============================================================================
// Impl: Index Building
// ============================================================================

void FMIModelDescription::Impl::buildIndices() {
    nameIndex.clear();
    vrIndex.clear();
    
    for (size_t i = 0; i < variablesList.size(); ++i) {
        nameIndex[variablesList[i].name] = i;
        vrIndex[variablesList[i].valueReference] = i;
    }
}

// ============================================================================
// Impl: Validation
// ============================================================================

void FMIModelDescription::Impl::validate() {
    errors.clear();

    if (modelName.empty()) {
        errors.push_back("modelName is required");
    }
    if (guid.empty()) {
        errors.push_back("guid is required");
    }
    if (fmiVersionEnum == FMIVersion::Unknown) {
        errors.push_back("fmiVersion is required or could not be determined");
    }

    // Check for duplicate value references
    std::unordered_map<uint32_t, std::string> seenVR;
    for (const auto& var : variablesList) {
        auto it = seenVR.find(var.valueReference);
        if (it != seenVR.end()) {
            errors.push_back("Duplicate valueReference " + std::to_string(var.valueReference) +
                           " for variables: " + it->second + " and " + var.name);
        } else {
            seenVR[var.valueReference] = var.name;
        }
    }

    // Check for duplicate names
    std::unordered_map<std::string, int> seenNames;
    for (const auto& var : variablesList) {
        seenNames[var.name]++;
    }
    for (const auto& [name, count] : seenNames) {
        if (count > 1) {
            errors.push_back("Duplicate variable name: " + name);
        }
    }

    // Validate interface info
    if (hasME && meInfo.modelIdentifier.empty()) {
        errors.push_back("ModelExchange modelIdentifier is required");
    }
    if (hasCS && csInfo.modelIdentifier.empty()) {
        errors.push_back("CoSimulation modelIdentifier is required");
    }
    if (hasSE && seInfo.modelIdentifier.empty()) {
        errors.push_back("ScheduledExecution modelIdentifier is required");
    }

    // At least one interface type must be present
    if (!hasME && !hasCS && !hasSE) {
        errors.push_back("At least one interface type (ModelExchange, CoSimulation, ScheduledExecution) must be defined");
    }

    valid = errors.empty();
}

} // namespace powsys365::simulation::fmi
