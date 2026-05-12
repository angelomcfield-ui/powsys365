#pragma once

#include "fmi_model_description.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

// Platform-specific dynamic library loading
#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace powsys365::simulation::fmi {

// ============================================================================
// FMI 2.0 C API Type Definitions
// ============================================================================
using fmi2Component = void*;
using fmi2ComponentEnvironment = void*;
using fmi2FMUstate = void*;
using fmi2ValueReference = uint32_t;
using fmi2Real = double;
using fmi2Integer = int32_t;
using fmi2Boolean = int;
using fmi2Char = char;
using fmi2String = const char*;
using fmi2Byte = char;
using fmi2Enum = int;

enum fmi2Status {
    fmi2OK = 0,
    fmi2Warning = 1,
    fmi2Discard = 2,
    fmi2Error = 3,
    fmi2Fatal = 4,
    fmi2Pending = 5
};

enum fmi2Type {
    fmi2ModelExchange = 0,
    fmi2CoSimulation = 1
};

typedef void (*fmi2CallbackLoggerFUNC)(
    fmi2ComponentEnvironment componentEnvironment,
    fmi2String instanceName,
    fmi2Status status,
    fmi2String category,
    fmi2String message,
    ...
);

typedef void* (*fmi2CallbackAllocateMemoryFUNC)(size_t nobj, size_t size);
typedef void (*fmi2CallbackFreeMemoryFUNC)(void* obj);
typedef void (*fmi2StepFinishedFUNC)(
    fmi2ComponentEnvironment componentEnvironment,
    fmi2Status status
);

struct fmi2CallbackFunctions {
    fmi2CallbackLoggerFUNC logger;
    fmi2CallbackAllocateMemoryFUNC allocateMemory;
    fmi2CallbackFreeMemoryFUNC freeMemory;
    fmi2StepFinishedFUNC stepFinished;
    fmi2ComponentEnvironment componentEnvironment;
};

// FMI 2.0 Function Types
typedef fmi2String    (*fmi2GetTypesPlatformTYPE)(void);
typedef fmi2String    (*fmi2GetVersionTYPE)(void);
typedef fmi2Status    (*fmi2SetDebugLoggingTYPE)(
    fmi2Component c, fmi2Boolean loggingOn, size_t nCategories, const fmi2String categories[]
);
typedef fmi2Component (*fmi2InstantiateTYPE)(
    fmi2String instanceName, fmi2Type fmuType,
    fmi2String fmuGUID, fmi2String fmuResourceLocation,
    const fmi2CallbackFunctions* functions,
    fmi2Boolean visible, fmi2Boolean loggingOn
);
typedef void          (*fmi2FreeInstanceTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2SetupExperimentTYPE)(
    fmi2Component c, fmi2Boolean toleranceDefined, fmi2Real tolerance,
    fmi2Real startTime, fmi2Boolean stopTimeDefined, fmi2Real stopTime
);
typedef fmi2Status    (*fmi2EnterInitializationModeTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2ExitInitializationModeTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2TerminateTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2ResetTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2GetRealTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Real value[]);
typedef fmi2Status    (*fmi2GetIntegerTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Integer value[]);
typedef fmi2Status    (*fmi2GetBooleanTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Boolean value[]);
typedef fmi2Status    (*fmi2GetStringTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2String value[]);
typedef fmi2Status    (*fmi2SetRealTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Real value[]);
typedef fmi2Status    (*fmi2SetIntegerTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer value[]);
typedef fmi2Status    (*fmi2SetBooleanTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Boolean value[]);
typedef fmi2Status    (*fmi2SetStringTYPE)(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2String value[]);

// Co-Simulation specific
typedef fmi2Status    (*fmi2EnterEventModeTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2NewDiscreteStatesTYPE)(fmi2Component c, void* fmi2EventInfo);
typedef fmi2Status    (*fmi2EnterContinuousTimeModeTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2CompletedIntegratorStepTYPE)(
    fmi2Component c, fmi2Boolean noSetFMUStatePriorToCurrentPoint,
    fmi2Boolean* enterEventMode, fmi2Boolean* terminateSimulation
);
typedef fmi2Status    (*fmi2SetTimeTYPE)(fmi2Component c, fmi2Real time);
typedef fmi2Status    (*fmi2SetContinuousStatesTYPE)(fmi2Component c, const fmi2Real x[], size_t nx);
typedef fmi2Status    (*fmi2GetDerivativesTYPE)(fmi2Component c, fmi2Real derivatives[], size_t nx);
typedef fmi2Status    (*fmi2GetEventIndicatorsTYPE)(fmi2Component c, fmi2Real eventIndicators[], size_t ni);
typedef fmi2Status    (*fmi2GetContinuousStatesTYPE)(fmi2Component c, fmi2Real x[], size_t nx);
typedef fmi2Status    (*fmi2GetNominalsOfContinuousStatesTYPE)(fmi2Component c, fmi2Real x_nominal[], size_t nx);

// FMI 2.0 CS specific
typedef fmi2Status    (*fmi2DoStepTYPE)(
    fmi2Component c, fmi2Real currentCommunicationPoint,
    fmi2Real communicationStepSize, fmi2Boolean noSetFMUStatePriorToCurrentPoint
);
typedef fmi2Status    (*fmi2CancelStepTYPE)(fmi2Component c);
typedef fmi2Status    (*fmi2GetStatusTYPE)(fmi2Component c, int s, void* value);
typedef fmi2Status    (*fmi2GetRealStatusTYPE)(fmi2Component c, int s, fmi2Real* value);
typedef fmi2Status    (*fmi2GetIntegerStatusTYPE)(fmi2Component c, int s, fmi2Integer* value);
typedef fmi2Status    (*fmi2GetBooleanStatusTYPE)(fmi2Component c, int s, fmi2Boolean* value);
typedef fmi2Status    (*fmi2GetStringStatusTYPE)(fmi2Component c, int s, fmi2String* value);

// Status kind for fmi2Get*Status
enum fmi2StatusKind {
    fmi2DoStepStatus = 0,
    fmi2PendingStatus = 1,
    fmi2LastSuccessfulTime = 2,
    fmi2Terminated = 3
};

// ============================================================================
// Dynamic Library Handle (RAII wrapper)
// ============================================================================
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool load(const std::string& libraryPath);
    void unload();
    bool isLoaded() const noexcept;

    template<typename T>
    T getSymbol(const std::string& symbolName) {
        if (!isLoaded()) return nullptr;
#ifdef _WIN32
        return reinterpret_cast<T>(::GetProcAddress(static_cast<HMODULE>(m_handle), symbolName.c_str()));
#else
        return reinterpret_cast<T>(dlsym(m_handle, symbolName.c_str()));
#endif
    }

    std::string lastError() const;

private:
#ifdef _WIN32
    HMODULE m_handle = nullptr;
#else
    void* m_handle = nullptr;
#endif
};

// ============================================================================
// FMI Library Functions Container
// ============================================================================
struct FMI2Functions {
    fmi2GetTypesPlatformTYPE         getTypesPlatform = nullptr;
    fmi2GetVersionTYPE               getVersion = nullptr;
    fmi2SetDebugLoggingTYPE          setDebugLogging = nullptr;
    fmi2InstantiateTYPE              instantiate = nullptr;
    fmi2FreeInstanceTYPE             freeInstance = nullptr;
    fmi2SetupExperimentTYPE          setupExperiment = nullptr;
    fmi2EnterInitializationModeTYPE  enterInitializationMode = nullptr;
    fmi2ExitInitializationModeTYPE   exitInitializationMode = nullptr;
    fmi2TerminateTYPE                terminate = nullptr;
    fmi2ResetTYPE                    reset = nullptr;
    fmi2GetRealTYPE                  getReal = nullptr;
    fmi2GetIntegerTYPE               getInteger = nullptr;
    fmi2GetBooleanTYPE               getBoolean = nullptr;
    fmi2GetStringTYPE                getString = nullptr;
    fmi2SetRealTYPE                  setReal = nullptr;
    fmi2SetIntegerTYPE               setInteger = nullptr;
    fmi2SetBooleanTYPE               setBoolean = nullptr;
    fmi2SetStringTYPE                setString = nullptr;

    // Model Exchange
    fmi2EnterEventModeTYPE           enterEventMode = nullptr;
    fmi2NewDiscreteStatesTYPE        newDiscreteStates = nullptr;
    fmi2EnterContinuousTimeModeTYPE  enterContinuousTimeMode = nullptr;
    fmi2CompletedIntegratorStepTYPE  completedIntegratorStep = nullptr;
    fmi2SetTimeTYPE                  setTime = nullptr;
    fmi2SetContinuousStatesTYPE      setContinuousStates = nullptr;
    fmi2GetDerivativesTYPE           getDerivatives = nullptr;
    fmi2GetEventIndicatorsTYPE       getEventIndicators = nullptr;
    fmi2GetContinuousStatesTYPE      getContinuousStates = nullptr;
    fmi2GetNominalsOfContinuousStatesTYPE getNominalsOfContinuousStates = nullptr;

    // Co-Simulation
    fmi2DoStepTYPE                   doStep = nullptr;
    fmi2CancelStepTYPE               cancelStep = nullptr;
    fmi2GetRealStatusTYPE            getRealStatus = nullptr;
    fmi2GetIntegerStatusTYPE         getIntegerStatus = nullptr;
    fmi2GetBooleanStatusTYPE         getBooleanStatus = nullptr;
    fmi2GetStringStatusTYPE          getStringStatus = nullptr;

    bool loadFromLibrary(DynamicLibrary& lib);
    bool isValidForCoSimulation() const;
    bool isValidForModelExchange() const;
};

// ============================================================================
// FMU Importer
// ============================================================================
class FMUImporter {
public:
    explicit FMUImporter(const std::string& fmuPath);
    ~FMUImporter();

    // Disable copy, enable move
    FMUImporter(const FMUImporter&) = delete;
    FMUImporter& operator=(const FMUImporter&) = delete;
    FMUImporter(FMUImporter&&) noexcept;
    FMUImporter& operator=(FMUImporter&&) noexcept;

    // ------------------------------------------------------------------------
    // Extraction
    // ------------------------------------------------------------------------
    bool extract();
    const std::string& extractedPath() const noexcept;

    // ------------------------------------------------------------------------
    // Model Description
    // ------------------------------------------------------------------------
    bool parseModelDescription();
    const FMIModelDescription* modelDescription() const noexcept;

    // ------------------------------------------------------------------------
    // Library Loading
    // ------------------------------------------------------------------------
    bool loadLibrary();
    bool isLibraryLoaded() const noexcept;
    void unloadLibrary();

    template<typename T>
    T* getSymbol(const std::string& symbolName) {
        if (!m_lib.isLoaded()) return nullptr;
        return m_lib.getSymbol<T>(symbolName);
    }

    // ------------------------------------------------------------------------
    // FMI 2.0 Functions
    // ------------------------------------------------------------------------
    FMI2Functions* fmi2Functions() noexcept;
    const FMI2Functions* fmi2Functions() const noexcept;

    // ------------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------------
    void cleanupExtracted();

    // ------------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------------
    bool isValid() const noexcept;
    const std::string& lastError() const noexcept;
    const std::string& fmuPath() const noexcept;

private:
    std::string m_fmuPath;
    std::string m_extractedPath;
    std::string m_lastError;

    bool m_extracted = false;
    bool m_modelDescParsed = false;

    FMIModelDescription m_modelDesc;
    DynamicLibrary m_lib;
    std::unique_ptr<FMI2Functions> m_fmi2Functions;

    // Helpers
    bool extractZipInternal();
    std::string getLibraryPath() const;
};

// ============================================================================
// ZIP Extraction Helper
// ============================================================================
class ZIPExtractor {
public:
    static bool extract(const std::string& zipPath, const std::string& destPath, std::string& errorMsg);

private:
    static bool extractSystemUnzip(const std::string& zipPath, const std::string& destPath, std::string& errorMsg);
    static bool extractMiniz(const std::string& zipPath, const std::string& destPath, std::string& errorMsg);
};

} // namespace powsys365::simulation::fmi
