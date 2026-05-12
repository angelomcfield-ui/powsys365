#include "fmi_importer.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>

// For ZIP extraction
#include <cstdio>
#include <array>

#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <errno.h>
#endif

namespace powsys365::simulation::fmi {

// ============================================================================
// DynamicLibrary Implementation
// ============================================================================

DynamicLibrary::~DynamicLibrary() {
    unload();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept {
    m_handle = other.m_handle;
#ifdef _WIN32
    other.m_handle = nullptr;
#else
    other.m_handle = nullptr;
#endif
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        unload();
        m_handle = other.m_handle;
#ifdef _WIN32
        other.m_handle = nullptr;
#else
        other.m_handle = nullptr;
#endif
    }
    return *this;
}

bool DynamicLibrary::load(const std::string& libraryPath) {
    unload();

#ifdef _WIN32
    m_handle = LoadLibraryA(libraryPath.c_str());
    if (!m_handle) {
        DWORD err = GetLastError();
        return false;
    }
#else
    m_handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        return false;
    }
#endif
    return true;
}

void DynamicLibrary::unload() {
#ifdef _WIN32
    if (m_handle) {
        FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
#else
    if (m_handle) {
        dlclose(m_handle);
        m_handle = nullptr;
    }
#endif
}

bool DynamicLibrary::isLoaded() const noexcept {
#ifdef _WIN32
    return m_handle != nullptr;
#else
    return m_handle != nullptr;
#endif
}

std::string DynamicLibrary::lastError() const {
#ifdef _WIN32
    DWORD err = GetLastError();
    if (err == 0) return "";
    LPSTR msgBuf = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msgBuf, 0, nullptr
    );
    std::string msg(msgBuf, size);
    LocalFree(msgBuf);
    return msg;
#else
    const char* err = dlerror();
    return err ? std::string(err) : "";
#endif
}

// ============================================================================
// FMI2Functions Implementation
// ============================================================================

bool FMI2Functions::loadFromLibrary(DynamicLibrary& lib) {
    if (!lib.isLoaded()) return false;

    getTypesPlatform        = lib.getSymbol<fmi2GetTypesPlatformTYPE>("fmi2GetTypesPlatform");
    getVersion              = lib.getSymbol<fmi2GetVersionTYPE>("fmi2GetVersion");
    setDebugLogging         = lib.getSymbol<fmi2SetDebugLoggingTYPE>("fmi2SetDebugLogging");
    instantiate             = lib.getSymbol<fmi2InstantiateTYPE>("fmi2Instantiate");
    freeInstance            = lib.getSymbol<fmi2FreeInstanceTYPE>("fmi2FreeInstance");
    setupExperiment         = lib.getSymbol<fmi2SetupExperimentTYPE>("fmi2SetupExperiment");
    enterInitializationMode = lib.getSymbol<fmi2EnterInitializationModeTYPE>("fmi2EnterInitializationMode");
    exitInitializationMode  = lib.getSymbol<fmi2ExitInitializationModeTYPE>("fmi2ExitInitializationMode");
    terminate               = lib.getSymbol<fmi2TerminateTYPE>("fmi2Terminate");
    reset                   = lib.getSymbol<fmi2ResetTYPE>("fmi2Reset");

    getReal    = lib.getSymbol<fmi2GetRealTYPE>("fmi2GetReal");
    getInteger = lib.getSymbol<fmi2GetIntegerTYPE>("fmi2GetInteger");
    getBoolean = lib.getSymbol<fmi2GetBooleanTYPE>("fmi2GetBoolean");
    getString  = lib.getSymbol<fmi2GetStringTYPE>("fmi2GetString");
    setReal    = lib.getSymbol<fmi2SetRealTYPE>("fmi2SetReal");
    setInteger = lib.getSymbol<fmi2SetIntegerTYPE>("fmi2SetInteger");
    setBoolean = lib.getSymbol<fmi2SetBooleanTYPE>("fmi2SetBoolean");
    setString  = lib.getSymbol<fmi2SetStringTYPE>("fmi2SetString");

    // Model Exchange functions
    enterEventMode        = lib.getSymbol<fmi2EnterEventModeTYPE>("fmi2EnterEventMode");
    newDiscreteStates     = lib.getSymbol<fmi2NewDiscreteStatesTYPE>("fmi2NewDiscreteStates");
    enterContinuousTimeMode = lib.getSymbol<fmi2EnterContinuousTimeModeTYPE>("fmi2EnterContinuousTimeMode");
    completedIntegratorStep = lib.getSymbol<fmi2CompletedIntegratorStepTYPE>("fmi2CompletedIntegratorStep");
    setTime               = lib.getSymbol<fmi2SetTimeTYPE>("fmi2SetTime");
    setContinuousStates   = lib.getSymbol<fmi2SetContinuousStatesTYPE>("fmi2SetContinuousStates");
    getDerivatives        = lib.getSymbol<fmi2GetDerivativesTYPE>("fmi2GetDerivatives");
    getEventIndicators    = lib.getSymbol<fmi2GetEventIndicatorsTYPE>("fmi2GetEventIndicators");
    getContinuousStates   = lib.getSymbol<fmi2GetContinuousStatesTYPE>("fmi2GetContinuousStates");
    getNominalsOfContinuousStates = lib.getSymbol<fmi2GetNominalsOfContinuousStatesTYPE>("fmi2GetNominalsOfContinuousStates");

    // Co-Simulation functions
    doStep       = lib.getSymbol<fmi2DoStepTYPE>("fmi2DoStep");
    cancelStep   = lib.getSymbol<fmi2CancelStepTYPE>("fmi2CancelStep");
    getRealStatus    = lib.getSymbol<fmi2GetRealStatusTYPE>("fmi2GetRealStatus");
    getIntegerStatus = lib.getSymbol<fmi2GetIntegerStatusTYPE>("fmi2GetIntegerStatus");
    getBooleanStatus = lib.getSymbol<fmi2GetBooleanStatusTYPE>("fmi2GetBooleanStatus");
    getStringStatus  = lib.getSymbol<fmi2GetStringStatusTYPE>("fmi2GetStringStatus");

    // Validate core functions are present
    return (instantiate != nullptr) && (freeInstance != nullptr) &&
           (setupExperiment != nullptr) && (terminate != nullptr) &&
           (getReal != nullptr) && (setReal != nullptr);
}

bool FMI2Functions::isValidForCoSimulation() const {
    return (doStep != nullptr) && (isValidForModelExchange() == false ? true : true);
}

bool FMI2Functions::isValidForModelExchange() const {
    return (getDerivatives != nullptr) && (setTime != nullptr) &&
           (getContinuousStates != nullptr);
}

// ============================================================================
// ZIP Extraction Implementation
// ============================================================================

namespace {

bool createDirectory(const std::string& path) {
    try {
        std::filesystem::create_directories(path);
        return true;
    } catch (...) {
        return false;
    }
}

bool executeCommand(const std::string& cmd, std::string& output) {
    std::array<char, 4096> buffer;
    output.clear();

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return false;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif
    return status == 0;
}

} // anonymous namespace

bool ZIPExtractor::extract(const std::string& zipPath, const std::string& destPath, std::string& errorMsg) {
    errorMsg.clear();

    // First try system unzip command
    if (extractSystemUnzip(zipPath, destPath, errorMsg)) {
        return true;
    }

    // Try miniz-based extraction
    if (extractMiniz(zipPath, destPath, errorMsg)) {
        return true;
    }

    return false;
}

bool ZIPExtractor::extractSystemUnzip(const std::string& zipPath, const std::string& destPath, std::string& errorMsg) {
    std::string output;

    // Ensure destination exists
    if (!createDirectory(destPath)) {
        errorMsg = "Failed to create destination directory";
        return false;
    }

    // Try unzip command (Unix/Linux/MacOS)
    std::string cmd = "unzip -o -q \"" + zipPath + "\" -d \"" + destPath + "\" 2>&1";
    if (executeCommand(cmd, output)) {
        return true;
    }

    // Try 7z (Windows/Universal)
    cmd = "7z x \"" + zipPath + "\" -o\"" + destPath + "\" -y 2>&1";
    if (executeCommand(cmd, output)) {
        return true;
    }

    // Try jar (Java)
    cmd = "jar xf \"" + zipPath + "\" 2>&1";
    std::string jarOutput;
    if (executeCommand(cmd, jarOutput)) {
        return true;
    }

    // Try PowerShell (Windows)
#ifdef _WIN32
    cmd = "powershell -command \"Expand-Archive -Path '\"" + zipPath + "\"' -DestinationPath '\"" + destPath + "\"' -Force\" 2>&1";
    if (executeCommand(cmd, output)) {
        return true;
    }
#endif

    errorMsg = "No system ZIP extraction tool available. Tried: unzip, 7z, jar, PowerShell";
    return false;
}

bool ZIPExtractor::extractMiniz(const std::string& zipPath, const std::string& destPath, std::string& errorMsg) {
    // Minimal built-in ZIP extraction using basic file I/O
    // This handles the most common ZIP format (uncompressed and DEFLATE)

    std::ifstream file(zipPath, std::ios::binary);
    if (!file) {
        errorMsg = "Cannot open ZIP file: " + zipPath;
        return false;
    }

    if (!createDirectory(destPath)) {
        errorMsg = "Failed to create destination directory";
        return false;
    }

    // ZIP local file header signature
    constexpr uint32_t LOCAL_FILE_HEADER_SIG = 0x04034b50;
    constexpr uint32_t CENTRAL_DIR_SIG = 0x02014b50;
    constexpr uint16_t COMPRESSION_STORED = 0;
    constexpr uint16_t COMPRESSION_DEFLATE = 8;

    std::string buffer;
    buffer.resize(65536);

    size_t filesExtracted = 0;

    while (file.good()) {
        // Read signature
        uint32_t sig = 0;
        file.read(reinterpret_cast<char*>(&sig), 4);
        if (!file.good()) break;

        if (sig == CENTRAL_DIR_SIG || sig == 0x06054b50 || sig == 0x05054b50) {
            // Central directory or end of central directory - we're done with files
            break;
        }

        if (sig != LOCAL_FILE_HEADER_SIG) {
            // Not a local file header, skip ahead
            continue;
        }

        // Parse local file header
        uint16_t version, flags, compression, modTime, modDate;
        uint32_t crc32, compressedSize, uncompressedSize;
        uint16_t nameLen, extraLen;

        file.read(reinterpret_cast<char*>(&version), 2);
        file.read(reinterpret_cast<char*>(&flags), 2);
        file.read(reinterpret_cast<char*>(&compression), 2);
        file.read(reinterpret_cast<char*>(&modTime), 2);
        file.read(reinterpret_cast<char*>(&modDate), 2);
        file.read(reinterpret_cast<char*>(&crc32), 4);
        file.read(reinterpret_cast<char*>(&compressedSize), 4);
        file.read(reinterpret_cast<char*>(&uncompressedSize), 4);
        file.read(reinterpret_cast<char*>(&nameLen), 2);
        file.read(reinterpret_cast<char*>(&extraLen), 2);

        if (!file.good()) {
            errorMsg = "Truncated ZIP file header";
            return false;
        }

        // Read file name
        std::string fileName;
        fileName.resize(nameLen);
        file.read(fileName.data(), nameLen);

        // Skip extra field
        file.seekg(extraLen, std::ios::cur);

        if (!file.good()) {
            errorMsg = "Truncated ZIP file";
            return false;
        }

        // Build output path
        std::string outPath = destPath;
        if (!outPath.empty() && outPath.back() != '/' && outPath.back() != '\\') {
            outPath += '/';
        }
        outPath += fileName;

        // Handle directory entries
        if (fileName.back() == '/' || fileName.back() == '\\') {
            createDirectory(outPath);
            continue;
        }

        // Create parent directory
        size_t lastSep = outPath.find_last_of("/\\");
        if (lastSep != std::string::npos) {
            createDirectory(outPath.substr(0, lastSep));
        }

        // Extract file data
        if (compression == COMPRESSION_STORED) {
            std::ofstream outFile(outPath, std::ios::binary);
            if (!outFile) {
                errorMsg = "Cannot create output file: " + outPath;
                return false;
            }

            // Copy uncompressed data
            uint32_t remaining = compressedSize;
            while (remaining > 0) {
                uint32_t toRead = static_cast<uint32_t>(std::min<size_t>(remaining, buffer.size()));
                file.read(buffer.data(), toRead);
                outFile.write(buffer.data(), file.gcount());
                remaining -= static_cast<uint32_t>(file.gcount());
            }
            filesExtracted++;
        } else if (compression == COMPRESSION_DEFLATE) {
            // For DEFLATE compression, we need zlib/miniz
            // Try to use system zlib if available
            std::string zlibOutput;
            std::string zlibError;

            // Save current position and read compressed data to temp file
            auto currentPos = file.tellg();

            // Create a temp file with compressed data
            std::string tempCompressed = destPath + "/.temp_compressed";
            {
                std::ofstream tempFile(tempCompressed, std::ios::binary);
                uint32_t remaining = compressedSize;
                while (remaining > 0) {
                    uint32_t toRead = static_cast<uint32_t>(std::min<size_t>(remaining, buffer.size()));
                    file.read(buffer.data(), toRead);
                    tempFile.write(buffer.data(), file.gcount());
                    remaining -= static_cast<uint32_t>(file.gcount());
                }
            }

            // Try to decompress using system tools
            bool decompressed = false;

            // Try Python zlib
            std::string pyCmd = "python3 -c \"import zlib; open('\"" + outPath + "\"','wb').write(zlib.decompress(open('\"" + tempCompressed + "\"','rb').read()))\" 2>&1";
            if (executeCommand(pyCmd, zlibOutput)) {
                decompressed = true;
            }

            // Try Python 3 specifically
            if (!decompressed) {
                pyCmd = "python -c \"import zlib; open('" + outPath + "','wb').write(zlib.decompress(open('" + tempCompressed + "','rb').read()))\" 2>&1";
                if (executeCommand(pyCmd, zlibOutput)) {
                    decompressed = true;
                }
            }

            // Remove temp file
            std::remove(tempCompressed.c_str());

            if (!decompressed) {
                // Skip the data and note it
                errorMsg = "DEFLATE compression not supported without zlib";
                file.seekg(currentPos);
                file.seekg(compressedSize, std::ios::cur);
                continue;
            }
            filesExtracted++;
        } else {
            errorMsg = "Unsupported compression method: " + std::to_string(compression);
            file.seekg(compressedSize, std::ios::cur);
            continue;
        }
    }

    if (filesExtracted == 0) {
        errorMsg = "No files could be extracted from ZIP archive";
        return false;
    }

    return true;
}

// ============================================================================
// FMUImporter Implementation
// ============================================================================

FMUImporter::FMUImporter(const std::string& fmuPath) : m_fmuPath(fmuPath) {
    m_fmi2Functions = std::make_unique<FMI2Functions>();
}

FMUImporter::~FMUImporter() {
    unloadLibrary();
    cleanupExtracted();
}

FMUImporter::FMUImporter(FMUImporter&&) noexcept = default;
FMUImporter& FMUImporter::operator=(FMUImporter&&) noexcept = default;

// ------------------------------------------------------------------------
// Extraction
// ------------------------------------------------------------------------

bool FMUImporter::extract() {
    if (m_extracted) return true;

    m_lastError.clear();

    // Validate FMU path
    if (m_fmuPath.empty()) {
        m_lastError = "FMU path is empty";
        return false;
    }

    std::ifstream testFile(m_fmuPath);
    if (!testFile) {
        m_lastError = "FMU file not found: " + m_fmuPath;
        return false;
    }
    testFile.close();

    // Create extraction directory
    std::filesystem::path fmuPath(m_fmuPath);
    std::string fmuName = fmuPath.stem().string();

    // Use temp directory
    std::string tempDir;
#ifdef _WIN32
    char tempBuf[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempBuf) > 0) {
        tempDir = std::string(tempBuf);
    } else {
        tempDir = ".";
    }
#else
    const char* tmp = std::getenv("TMPDIR");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = std::getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    tempDir = tmp;
#endif

    // Create unique extraction directory
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string pidStr = std::to_string(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    );

    m_extractedPath = tempDir + "/fmi_" + fmuName + "_" + pidStr + "_" + std::to_string(ms);

    // Extract ZIP
    std::string extractError;
    if (!ZIPExtractor::extract(m_fmuPath, m_extractedPath, extractError)) {
        m_lastError = "Failed to extract FMU: " + extractError;
        return false;
    }

    m_extracted = true;
    return true;
}

const std::string& FMUImporter::extractedPath() const noexcept {
    return m_extractedPath;
}

// ------------------------------------------------------------------------
// Model Description Parsing
// ------------------------------------------------------------------------

bool FMUImporter::parseModelDescription() {
    if (m_modelDescParsed) return true;

    if (!m_extracted) {
        if (!extract()) {
            return false;
        }
    }

    // Find modelDescription.xml
    std::string xmlPath = m_extractedPath + "/modelDescription.xml";

    // Check if directly in extracted path
    std::ifstream testFile(xmlPath);
    if (!testFile) {
        // Check if nested in a subdirectory (common in some FMU generators)
        try {
            for (const auto& entry : std::filesystem::directory_iterator(m_extractedPath)) {
                if (entry.is_directory()) {
                    std::string nestedXml = entry.path().string() + "/modelDescription.xml";
                    std::ifstream nestedFile(nestedXml);
                    if (nestedFile) {
                        m_extractedPath = entry.path().string();
                        xmlPath = nestedXml;
                        break;
                    }
                }
            }
        } catch (...) {
            // Ignore filesystem errors
        }
    }

    if (!m_modelDesc.parseFromFile(xmlPath)) {
        m_lastError = "Failed to parse modelDescription.xml";
        auto errors = m_modelDesc.validationErrors();
        if (!errors.empty()) {
            m_lastError += ": " + errors[0];
        }
        return false;
    }

    m_modelDescParsed = true;
    return true;
}

const FMIModelDescription* FMUImporter::modelDescription() const noexcept {
    return m_modelDescParsed ? &m_modelDesc : nullptr;
}

// ------------------------------------------------------------------------
// Library Loading
// ------------------------------------------------------------------------

bool FMUImporter::loadLibrary() {
    if (m_lib.isLoaded()) return true;

    if (!m_modelDescParsed) {
        if (!parseModelDescription()) {
            return false;
        }
    }

    std::string libPath = getLibraryPath();
    if (libPath.empty()) {
        m_lastError = "Cannot determine library path for current platform";
        return false;
    }

    if (!m_lib.load(libPath)) {
        m_lastError = "Failed to load library: " + libPath + " - " + m_lib.lastError();
        return false;
    }

    // Load FMI 2.0 functions
    if (!m_fmi2Functions->loadFromLibrary(m_lib)) {
        m_lastError = "Failed to load required FMI 2.0 functions from library";
        m_lib.unload();
        return false;
    }

    return true;
}

bool FMUImporter::isLibraryLoaded() const noexcept {
    return m_lib.isLoaded();
}

void FMUImporter::unloadLibrary() {
    m_lib.unload();
    m_fmi2Functions = std::make_unique<FMI2Functions>();
}

std::string FMUImporter::getLibraryPath() const {
    std::string binariesPath = m_extractedPath + "/binaries/";

#ifdef _WIN32
    // Windows x64
    std::string winPath = binariesPath + "win64/";
    if (std::filesystem::exists(winPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(winPath)) {
            if (entry.path().extension() == ".dll") {
                return entry.path().string();
            }
        }
    }
    // Windows x86
    winPath = binariesPath + "win32/";
    if (std::filesystem::exists(winPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(winPath)) {
            if (entry.path().extension() == ".dll") {
                return entry.path().string();
            }
        }
    }
#elif defined(__APPLE__)
    // macOS
    std::string macPath = binariesPath + "darwin64/";
    if (std::filesystem::exists(macPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(macPath)) {
            if (entry.path().extension() == ".dylib" || entry.path().extension() == ".so") {
                return entry.path().string();
            }
        }
    }
    // Try darwin-arm64
    macPath = binariesPath + "darwin-arm64/";
    if (std::filesystem::exists(macPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(macPath)) {
            if (entry.path().extension() == ".dylib" || entry.path().extension() == ".so") {
                return entry.path().string();
            }
        }
    }
    // Try darwin-x86_64
    macPath = binariesPath + "darwin-x86_64/";
    if (std::filesystem::exists(macPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(macPath)) {
            if (entry.path().extension() == ".dylib" || entry.path().extension() == ".so") {
                return entry.path().string();
            }
        }
    }
#else
    // Linux
    std::string linuxPath = binariesPath + "linux64/";
    if (std::filesystem::exists(linuxPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(linuxPath)) {
            if (entry.path().extension() == ".so") {
                return entry.path().string();
            }
        }
    }
    // Try x86_64-linux-gnu
    linuxPath = binariesPath + "x86_64-linux-gnu/";
    if (std::filesystem::exists(linuxPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(linuxPath)) {
            if (entry.path().extension() == ".so") {
                return entry.path().string();
            }
        }
    }
    // Try any linux variant
    try {
        for (const auto& entry : std::filesystem::directory_iterator(binariesPath)) {
            if (entry.is_directory() && entry.path().filename().string().find("linux") != std::string::npos) {
                for (const auto& libEntry : std::filesystem::directory_iterator(entry.path())) {
                    if (libEntry.path().extension() == ".so") {
                        return libEntry.path().string();
                    }
                }
            }
        }
    } catch (...) {}
#endif

    // Fallback: search entire binaries directory
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(binariesPath)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension();
                if (ext == ".so" || ext == ".dll" || ext == ".dylib") {
                    return entry.path().string();
                }
            }
        }
    } catch (...) {
        // Cannot modify m_lastError in const method
        (void)0;
    }

    return "";
}

// ------------------------------------------------------------------------
// FMI 2.0 Functions Access
// ------------------------------------------------------------------------

FMI2Functions* FMUImporter::fmi2Functions() noexcept {
    return m_fmi2Functions.get();
}

const FMI2Functions* FMUImporter::fmi2Functions() const noexcept {
    return m_fmi2Functions.get();
}

// ------------------------------------------------------------------------
// Cleanup
// ------------------------------------------------------------------------

void FMUImporter::cleanupExtracted() {
    if (!m_extracted || m_extractedPath.empty()) return;

    try {
        std::filesystem::remove_all(m_extractedPath);
    } catch (...) {
        // Best effort cleanup
    }

    m_extracted = false;
    m_extractedPath.clear();
}

// ------------------------------------------------------------------------
// Status
// ------------------------------------------------------------------------

bool FMUImporter::isValid() const noexcept {
    return m_modelDescParsed && m_modelDesc.isValid() && m_lib.isLoaded();
}

const std::string& FMUImporter::lastError() const noexcept {
    return m_lastError;
}

const std::string& FMUImporter::fmuPath() const noexcept {
    return m_fmuPath;
}

} // namespace powsys365::simulation::fmi
