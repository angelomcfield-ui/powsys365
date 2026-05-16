# AUDITORIA DE BUILD Y TESTS - POWSYS365 v3.0.0
## Fecha de auditoria: 2026
## Auditor: Sistema automatizado

---

## 1. RESUMEN EJECUTIVO

| Aspecto | Estado |
|---------|--------|
| CMakeLists.txt raiz | Parcialmente funcional |
| Sub-CMakeLists.txt | **3 errores fatales de sintaxis** |
| Tests C++ (tests/cpp/) | Codigo REAL, bien estructurado (Catch2 v3) |
| Tests Python | Codigo REAL, fixtures correctas (pytest) |
| Tests core/ | Usan `assert()` no Catch2 |
| Scripts build.sh/install.sh | Funcionales, bien estructurados |
| CI/CD (.github/workflows/) | **AUSENTE** |
| vcpkg.json | Inconsistencia de version |
| Dependencias externas | FetchContent bien configurado |
| Compilacion estimada | **FALLARA** (errores fatales) |

---

## 2. ERRORES FATALES (impiden la compilacion)

### 2.1 reliability/CMakeLists.txt - ERROR DE SINTAXIS CMake
**Archivo:** `reliability/CMakeLists.txt`  
**Linea:** 3  
**Codigo problematico:**
```cmake
RELIABILITY_SOURCES
set(reliability_sources)
```
**Problema:** La linea `RELIABILITY_SOURCES` no es un comando CMake valido. Falta `set(`  
**Impacto:** CMake fallara inmediatamente con `Unknown CMake command "RELIABILITY_SOURCES"`  
**Fix:**
```cmake
set(RELIABILITY_SOURCES
    monte_carlo_reliability.cpp
    reliability_analyzer.cpp
)
```

### 2.2 i18n/CMakeLists.txt - ERROR DE SINTAXIS CMake
**Archivo:** `i18n/CMakeLists.txt`  
**Linea:** 3  
**Codigo problematico:**
```cmake
I18N_SOURCES
set(i18n_sources)
```
**Problema:** Identico al anterior - comando CMake invalido  
**Impacto:** CMake fallara con `Unknown CMake command "I18N_SOURCES"`  
**Fix:**
```cmake
set(I18N_SOURCES
    TranslationManager.cpp
)
```

### 2.3 icon_engine/CMakeLists.txt - ERROR DE SINTAXIS CMake
**Archivo:** `icon_engine/CMakeLists.txt`  
**Linea:** 3  
**Codigo problematico:**
```cmake
ICON_ENGINE_SOURCES
set(icon_engine_sources)
```
**Problema:** Identico a los dos anteriores  
**Impacto:** CMake fallara con `Unknown CMake command "ICON_ENGINE_SOURCES"`  
**Fix:**
```cmake
set(ICON_ENGINE_SOURCES
    IconProvider.cpp
    IconRenderer.cpp
    ThemeManager.cpp
)
```

### 2.4 licensing/ - DIRECTORIO SIN CMakeLists.txt
**Archivo:** `licensing/CMakeLists.txt` **AUSENTE**  
**Linea:** N/A en CMakeLists.txt raiz linea 327  
**Codigo problematico en raiz:**
```cmake
if(BUILD_LICENSING)
    add_subdirectory(licensing)   # <-- NO HAY CMakeLists.txt
endif()
```
**Problema:** El directorio `licensing/` existe con archivos C++ (LicenseManager.cpp, etc.) pero NO tiene CMakeLists.txt  
**Impacto:** CMake fallara con `add_subdirectory given source "licensing" which is not an existing directory` o similar  
**Fix:** Crear `licensing/CMakeLists.txt` con contenido minimo

### 2.5 python/CMakeLists.txt - ARCHIVOS FUENTE AUSENTES
**Archivo:** `python/CMakeLists.txt`  
**Lineas:** 17-26  
**Codigo problematico:**
```cmake
pybind11_add_module(_powsys365_native
    src/bindings.cpp              # NO EXISTE
    src/power_system_bindings.cpp # NO EXISTE
    src/load_flow_bindings.cpp    # NO EXISTE
    src/network_topology_bindings.cpp # NO EXISTE
    src/bus_bindings.cpp          # NO EXISTE
    src/branch_bindings.cpp       # NO EXISTE
    src/generator_bindings.cpp    # NO EXISTE
    src/load_bindings.cpp         # NO EXISTE
    src/utils_bindings.cpp        # NO EXISTE
)
```
**Problema:** Solo existe `python/bindings.cpp` (629 lineas). No existe el directorio `python/src/`  
**Impacto:** Fallo de compilacion - archivos no encontrados  
**Fix:** Corregir la lista de fuentes a solo `bindings.cpp` o crear los archivos faltantes

---

## 3. ERRORES GRAVE (no fatales pero provocaran fallos)

### 3.1 tests/CMakeLists.txt - set_tests_properties referencia nombres incorrectos
**Archivo:** `tests/CMakeLists.txt`  
**Lineas:** 213-216  
**Codigo problematico:**
```cmake
set_tests_properties(test_core PROPERTIES LABELS "unit;core")
set_tests_properties(test_math PROPERTIES LABELS "unit;math")
set_tests_properties(test_loadflow PROPERTIES LABELS "unit;loadflow")
set_tests_properties(test_integration PROPERTIES LABELS "integration")
```
**Problema:** `test_core`, `test_math`, `test_loadflow`, `test_integration` son nombres de ejecutables, NO nombres de tests CTest. `catch_discover_tests()` registra tests individuales, no el ejecutable como test CTest.  
**Impacto:** CMake warning/fallo: `set_tests_properties Can not find test to add properties to: test_core`  
**Fix:** Eliminar estas lineas o usar `set_target_properties()`

### 3.2 CMakeLists.txt raiz - ALIAS autoreferencial
**Archivo:** `CMakeLists.txt`  
**Linea:** 216  
**Codigo problematico:**
```cmake
else()
    add_library(Eigen3::Eigen ALIAS Eigen3::Eigen)
endif()
```
**Problema:** No se puede crear un ALIAS de un target a si mismo. Si Eigen3 fue encontrado via `find_package`, el target `Eigen3::Eigen` ya existe.  
**Impacto:** CMake warning: `add_library cannot create ALIAS target "Eigen3::Eigen" because target "Eigen3::Eigen" is itself an ALIAS.`  
**Fix:** Eliminar esta linea completamente.

### 3.3 io/CMakeLists.txt - add_library DUPLICADO
**Archivo:** `io/CMakeLists.txt`  
**Lineas:** 52 y 103  
**Problema:** Define `add_library(powsys365_io STATIC ...)` DOS VECES. La primera vez se redefine inmediatamente `IO_SOURCES` y se redeclara la libreria.  
**Impacto:** CMake warning: `add_library() called with previously defined target name "powsys365_io"`. El segundo add_library sobreescribe al primero.  
**Fix:** Eliminar el primer `add_library()` (lineas 51-77) y mantener solo el segundo.

### 3.4 audio/CMakeLists.txt - Qt6 REQUIRED en modulo opcional
**Archivo:** `audio/CMakeLists.txt`  
**Linea:** 12  
**Codigo problematico:**
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core)
```
**Problema:** `audio` es un modulo opcional (`BUILD_AUDIO=ON` por defecto). Si Qt6 no esta instalado, la configuracion fallara incluso si BUILD_AUDIO=OFF porque el CMakeLists.txt del modulo se procesa antes del `if(NOT BUILD_AUDIO)`.  
**Nota:** En realidad no, porque CMakeLists.txt raiz procesa `add_subdirectory(audio)` DENTRO de `if(BUILD_AUDIO)`. Pero dentro del propio CMakeLists.txt, `find_package(Qt6 REQUIRED)` fallara si no hay Qt6.  
**Impacto:** Si Qt6 no esta disponible y BUILD_AUDIO=ON, fallo de configuracion.  
**Fix:** Usar `QUIET` y condicional, o mover la logica.

### 3.5 help/CMakeLists.txt - Qt6 REQUIRED en modulo opcional
**Archivo:** `help/CMakeLists.txt`  
**Linea:** 12  
**Problema identico al 3.4.** `find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Widgets)` en modulo opcional.

### 3.6 cmake/POWSYS365Config.cmake.in - include(FindOptionalPackage) no existe
**Archivo:** `cmake/POWSYS365Config.cmake.in`  
**Linea:** 18  
**Codigo problematico:**
```cmake
include(FindOptionalPackage)
```
**Problema:** `FindOptionalPackage` no es un modulo estandar de CMake. No existe en la distribucion CMake.  
**Impacto:** Cuando un consumidor hace `find_package(POWSYS365)`, fallara con `Unknown CMake command` o modulo no encontrado.  
**Fix:** Eliminar la linea o implementar el modulo.

### 3.7 gis/CMakeLists.txt - find_package(CURL REQUIRED)
**Archivo:** `gis/CMakeLists.txt`  
**Linea:** 39  
**Problema:** CURL REQUIRED en modulo opcional. Si CURL no esta instalado y BUILD_GIS=ON (por defecto), fallara.  
**Fix:** Usar `QUIET` y manejar fallback.

### 3.8 xtalk/CMakeLists.txt - find_package(CURL REQUIRED)
**Archivo:** `xtalk/CMakeLists.txt`  
**Linea:** 39  
**Problema identico al 3.7.**

---

## 4. PROBLEMAS MODERADOS

### 4.1 vcpkg.json - version inconsistente
**Archivo:** `vcpkg.json`  
**Linea:** 3  
**Valor:** `"version": "1.0.0"`  
**Problema:** CMakeLists.txt raiz define `VERSION 3.0.0`, pero vcpkg.json dice "1.0.0".  
**Fix:** Sincronizar a "3.0.0".

### 4.2 tests/CMakeLists.txt - GLOB de subdirectorios inexistentes
**Archivo:** `tests/CMakeLists.txt`  
**Lineas:** 38-48, 54-64, 69-79, etc.  
**Problema:** Hace `file(GLOB CORE_UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")` para subdirectorios `core/`, `math/`, `loadflow/`, `simulation/`, `scada/`, `results/`, `ai/`, `regression/`, `utils/`, `data/` - NINGUNO de estos directorios existe.  
**Impacto:** Los `if(...)` que envuelven estos bloques seran falsos, por lo que los ejecutables de test NO se crearan para esas categorias. Solo `tests/cpp/` y `tests/integration/` y `tests/python/` existen como directorios de test, pero `tests/CMakeLists.txt` NO los referencia.  
**Resultado:** Los tests en `tests/cpp/` (test_load_flow.cpp, test_math_utils.cpp, test_short_circuit.cpp, test_ybus.cpp) y `tests/integration/test_end_to_end.cpp` NO seran compilados por tests/CMakeLists.txt.

### 4.3 core/CMakeLists.txt - tiene su propio project() anidado
**Archivo:** `core/CMakeLists.txt`  
**Linea:** 2  
**Codigo:**
```cmake
project(powsys365_core VERSION 1.0.0 LANGUAGES CXX)
```
**Problema:** En un proyecto que usa `add_subdirectory(core)` esto crea un project() anidado. Funciona en CMake moderno pero no es la convencion estandar.  
**Impacto:** Minimo, pero puede confundir a herramientas que esperan un solo project().

### 4.4 Múltiples estilos de CMake inconsistentes
El proyecto usa estilos de CMake muy diferentes entre modulos:
- **Raiz:** FetchContent, target-based, FILE_SET headers
- **core:** Estilo "clasico" con set() para sources
- **harmonics:** Español en comentarios, codigo mezclado es/en
- **reliability/i18n/icon_engine:** Solo 4-10 lineas, incompletos
- **io:** Logica de stubs auto-generados con `file(WRITE ...)`
- **ui:** Project() independiente con add_executable() directo
- **models:** Español en comentarios

### 4.5 ui/CMakeLists.txt - ejecutable independiente sin dependencias del proyecto
**Archivo:** `ui/CMakeLists.txt`  
**Problema:** Define `add_executable(${PROJECT_NAME} ...)` donde PROJECT_NAME es "POWSYS365" (coincide con el ejecutable raiz). No enlaza con NINGUN target de powsys365 (core, simulation, etc.). Solo enlaza Qt6.  
**Impacto:** El ejecutable UI no tendra acceso a la libreria core.

---

## 5. ANALISIS DE TESTS

### 5.1 Tests C++ (tests/cpp/) - CODIGO REAL, CALIDAD ALTA

| Archivo | Lineas | Contenido | Tipo |
|---------|--------|-----------|------|
| main_test.cpp | 30 | Punto de entrada Catch2 v3 | Framework |
| test_load_flow.cpp | 312 | 8 test cases: NR, FDLF, GS, empty system, no slack, Q-limits, balance, consistency | REAL |
| test_math_utils.cpp | 400 | 20+ test cases: convergencia, potencias inyectadas, solve_sparse, angulos, SparseLU, clamps | REAL |
| test_short_circuit.cpp | 263 | 10 test cases: 3ph, SLG, 2ph, 2ph-ground, dispatcher, secuencias | REAL |
| test_ybus.cpp | 269 | 10 test cases: dimensiones, sparsity, dominancia diagonal, simetria, topologia, builder | REAL |
| test_end_to_end.cpp | 408 | 7 test cases: flujo completo NR, balance, lineas, cortocircuito post-flujo, comparacion NR/FDLF | REAL |

**Veredicto:** Los tests en `tests/cpp/` tienen codigo REAL, no stubs. Usan Catch2 v3 correctamente (`<catch2/catch_all.hpp>`, `TEST_CASE`, `REQUIRE`). Incluyen tests de integracion con valores de referencia IEEE 14. **Calidad: EXCELENTE.**

**PROBLEMA:** `tests/CMakeLists.txt` NO referencia `tests/cpp/` ni `tests/integration/`. Los tests nunca se compilen via el build principal. Solo `core/tests/test_load_flow.cpp` se compila via `core/CMakeLists.txt`, pero ese test usa `assert()` en lugar de Catch2.

### 5.2 Tests core/ (core/tests/test_load_flow.cpp)
**Archivo:** `core/tests/test_load_flow.cpp`  
**Lineas:** 152  
**Contenido:** 5 funciones de test con `assert()`, no Catch2. Imprime resultados a stdout.  
**Problema:** Este test es compilado por `core/CMakeLists.txt` linea 120 pero NO usa Catch2. El `add_test()` lo ejecuta como ejecutable simple, sin integracion con CTest/Catch2.

### 5.3 Tests Python (tests/python/)

| Archivo | Lineas | Contenido |
|---------|--------|-----------|
| conftest.py | 63 | Fixtures pytest: ieee14_network, empty_network, minimal_network, isolated_bus_network |
| test_analysis.py | 249 | Tests de analisis con mocks Python (NR, FD, short-circuit) |
| test_network.py | 371 | Tests de Network class: buses, lines, generators, loads, transformers, IEEE14, JSON round-trip |

**Veredicto:** Codigo REAL, pytest correcto, fixtures bien estructuradas. Los tests de analysis usan mocks (fallbacks Python puros). Los tests de network prueban la API Python directamente. **Calidad: BUENA.**

### 5.4 Faltan subdirectorios de tests referenciados
Los siguientes directorios son buscados por `tests/CMakeLists.txt` pero NO EXISTEN:
- `tests/core/` -> los tests reales estan en `tests/cpp/`
- `tests/math/` -> los tests reales estan en `tests/cpp/test_math_utils.cpp`
- `tests/loadflow/` -> los tests reales estan en `tests/cpp/test_load_flow.cpp`
- `tests/simulation/`, `tests/scada/`, `tests/results/`, `tests/ai/`
- `tests/regression/`
- `tests/utils/`, `tests/data/`

---

## 6. ANALISIS DE SCRIPTS DE BUILD

### 6.1 build.sh - BIEN ESTRUCTURADO
- Parseo de argumentos completo (--platform, --build-type, --clean, etc.)
- Deteccion automatica de plataforma
- Verificacion de prerequisitos (CMake, compilador, Python, Qt6)
- Generador automatico (Ninja > Makefiles)
- Opciones CMake bien formadas
- Ejecucion de tests con ctest
- Generacion de paquetes con cpack

**Problema:** Las opciones CMake que pasa (`-DENABLE_SCADA=`, `-DENABLE_AI=`, etc.) NO coinciden con los nombres de opciones en CMakeLists.txt raiz (`BUILD_SCADA`, `BUILD_AI`). build.sh pasa `-DENABLE_SCADA` pero CMakeLists.txt espera `-DBUILD_SCADA`. **Las opciones del script NO afectaran la configuracion.**

### 6.2 install.sh - COMPLETO
- Instalacion/desinstalacion/verificacion
- Instalacion de dependencias por plataforma
- Integracion de escritorio (Linux/macOS)
- Post-instalacion (ldconfig, PATH)
- Activacion de licencia
- Copia de recursos, esquemas, documentacion

**Problema:** El script busca `${BUILD_DIR}/bin/powsys365` pero el target ejecutable principal no esta definido en CMakeLists.txt raiz. Ningun `add_executable()` existe a nivel raiz.

### 6.3 install_deps.sh - FUNCIONAL
- Cubre macOS (brew), Linux (apt/dnf/pacman), Windows (vcpkg)
- Lista completa de dependencias

---

## 7. ANALISIS DE DEPENDENCIAS EXTERNAS

### 7.1 FetchContent bien configurado
- nlohmann/json v3.11.3
- Catch2 v3.7.0
- pybind11 v2.13.0
- tl::expected v1.1.0
- Eigen3 (find_package QUIET + fallback FetchContent)

### 7.2 Problemas de dependencias
- **SUNDIALS** (simulation): FetchContent desde LLNL/sundials.git. Compilacion de SUNDIALS desde fuente puede tardar 5-15 minutos.
- **libIEC61850** (scada): FetchContent desde mz-automation/libiec61850.git. La compilacion puede requerir dependencias adicionales.
- **opendnp3** (scada): FetchContent desde dnp3/opendnp3.git. Puede tener problemas de compatibilidad con C++17.
- **open62541** (scada): FetchContent. Compilacion larga, requiere mbedtls.
- **libmodbus** (scada): FetchContent. Requiere autotools (no CMake puro).
- **libharu** (results): FetchContent. Bien.
- **libxlsxwriter** (results): FetchContent. Requiere zlib.
- **FMILibrary** (simulation): Solo busca via find_package, sin FetchContent fallback completo.

### 7.3 Eigen3 doble busqueda
CMakeLists.txt raiz (linea 204) y core/CMakeLists.txt (linea 21) ambos buscan Eigen3. Si Eigen3 no esta instalado, el raiz lo baja via FetchContent, y core/CMakeLists.txt lo busca de nuevo. Funciona pero es redundante.

---

## 8. VERIFICACION CRUZADA: add_subdirectory vs directorios

| add_subdirectory() en raiz | Directorio existe? | CMakeLists.txt existe? | Estado |
|----------------------------|-------------------|----------------------|--------|
| third_party | SI | SI | OK |
| core | SI | SI | OK |
| simulation | SI | SI | OK |
| scada | SI | SI | OK |
| results | SI | SI | OK |
| ai | SI | SI | OK |
| ide | SI | SI | OK |
| python | SI | SI | OK (pero src/ faltante) |
| ui | SI | SI | OK (pero project() independiente) |
| models | SI | SI | OK |
| harmonics | SI | SI | OK |
| markets | SI | SI | OK |
| **reliability** | SI | **SI (ERROR SINTAXIS)** | **FALLA** |
| **licensing** | SI | **NO** | **FALLA** |
| **integration** | SI | **NO** | **FALLA** |
| **i18n** | SI | **SI (ERROR SINTAXIS)** | **FALLA** |
| **icon_engine** | SI | **SI (ERROR SINTAXIS)** | **FALLA** |
| io | SI | SI | OK (pero add_library duplicado) |
| gis | SI | SI | OK |
| xtalk | SI | SI | OK |
| legal | SI | SI | OK |
| audio | SI | SI | OK |
| help | SI | SI | OK |
| line_design | SI | SI | OK |
| config | SI | SI | OK |
| tests | SI | SI | OK (pero GLOB de dirs inexistentes) |

---

## 9. VERIFICACION: target_link_libraries referencia targets existentes

| Target referenciado | Definido por | Estado |
|---------------------|-------------|--------|
| powsys365_core | core/CMakeLists.txt | OK |
| powsys365_simulation | simulation/CMakeLists.txt | OK |
| powsys365_scada | scada/CMakeLists.txt | OK |
| powsys365_results | results/CMakeLists.txt | OK |
| powsys365_ai | ai/CMakeLists.txt | OK |
| powsys365_warnings | CMakeLists.txt raiz (linea 98) | OK |
| powsys365_sanitizers | CMakeLists.txt raiz (linea 153) | OK (solo Debug) |
| powsys365_test_utils | tests/CMakeLists.txt | OK (condicional) |
| Eigen3::Eigen | find_package/FetchContent | OK |
| nlohmann_json::nlohmann_json | FetchContent | OK |
| Catch2::Catch2WithMain | FetchContent | OK (BUILD_TESTS) |
| pybind11::module | FetchContent | OK (BUILD_PYTHON) |
| Qt6::* | find_package | Condicional |
| OpenMP::OpenMP_CXX | find_package | Condicional |
| SUNDIALS::* | find_package/FetchContent | Condicional |

---

## 10. PREGUNTAS CLAVE RESPONDIDAS

### 10.1 Los CMakeLists.txt definen targets reales con add_library/add_executable?
**Respuesta:** SI para la mayoria. `powsys365_core`, `powsys365_simulation`, `powsys365_scada`, `powsys365_results`, `powsys365_ai`, `powsys365_ide` son targets STATIC reales con archivos fuente. Los modulos `reliability`, `i18n`, `icon_engine` definen INTERFACE targets (vacios debido a errores de sintaxis). No hay `add_executable()` a nivel raiz - el unico ejecutable esta en `ui/` que es independiente.

### 10.2 Las dependencias externas se resuelven correctamente?
**Respuesta:** PARCIALMENTE. FetchContent funciona bien para Catch2, nlohmann/json, pybind11, tl::expected. Eigen3 tiene doble busqueda. Las dependencias opcionales (SUNDIALS, libIEC61850, opendnp3, open62541) requieren compilacion desde fuente lo que puede tardar horas. CURL REQUIRED en gis/ y xtalk/ fallara si no esta instalado.

### 10.3 Los tests tienen codigo real o son stubs?
**Respuesta:** CODIGO REAL. Los tests en `tests/cpp/` son de alta calidad, con tests de convergencia NR/FDLF/GS, validacion IEEE 14, pruebas de balance de potencia, cortocircuito, etc. Los tests Python tambien son reales con fixtures. **PROBLEMA:** tests/CMakeLists.txt NO los compila porque busca subdirectorios equivocados.

### 10.4 El CI/CD workflow es funcional?
**Respuesta:** NO EXISTE. El directorio `.github/workflows/` esta AUSENTE. No hay CI/CD configurado.

### 10.5 El script de build tiene la logica correcta?
**Respuesta:** PARCIALMENTE. El script build.sh es bien estructurado pero las opciones CMake que pasa (`-DENABLE_*`) no coinciden con los nombres en CMakeLists.txt (`-DBUILD_*`). Las opciones del script no afectaran la configuracion.

### 10.6 Hay problemas de dependencias entre modulos?
**Respuesta:** El orden de add_subdirectory en CMakeLists.txt raiz es correcto (core primero, luego simulation, scada, results, ai, ide, python, ui). Los target_link_libraries son correctos. No hay dependencias circulares.

---

## 11. VEREDICTO FINAL

### Compilaria este proyecto?
**RESPUESTA: NO. Fallaria en la fase de configuracion de CMake por multiples errores fatales.**

### Errores esperados al intentar compilar:

1. **`CMake Error: Unknown CMake command "RELIABILITY_SOURCES"`** (reliability/CMakeLists.txt:3)
2. **`CMake Error: Unknown CMake command "I18N_SOURCES"`** (i18n/CMakeLists.txt:3)
3. **`CMake Error: Unknown CMake command "ICON_ENGINE_SOURCES"`** (icon_engine/CMakeLists.txt:3)
4. **`CMake Error: add_subdirectory given source "licensing" which is not an existing directory`** (CMakeLists.txt raiz:327 - licensing/ sin CMakeLists.txt)
5. **`CMake Error: Cannot find source file: src/bindings.cpp`** (python/CMakeLists.txt:17 - python/src/ no existe)
6. **`CMake Error: add_library cannot create ALIAS target "Eigen3::Eigen"`** (CMakeLists.txt raiz:216)
7. **`CMake Warning (dev): Policy CMP0111 is not set`** (ui/CMakeLists.txt usa CMAKE_AUTOMOC sin Qt6 garantizado)

### Para compilar exitosamente se requiere:

1. Corregir los 3 errores de sintaxis en reliability/, i18n/, icon_engine/
2. Crear licensing/CMakeLists.txt
3. Corregir python/CMakeLists.txt para usar bindings.cpp en lugar de src/*.cpp
4. Eliminar el ALIAS autoreferencial de Eigen3
5. Corregir tests/CMakeLists.txt para referenciar tests/cpp/ en lugar de tests/core/, tests/math/, etc.
6. Eliminar o corregir las lineas set_tests_properties que referencian nombres incorrectos
7. Corregir build.sh para usar -DBUILD_* en lugar de -DENABLE_*
8. Corregir io/CMakeLists.txt (eliminar add_library duplicado)
9. Corregir cmake/POWSYS365Config.cmake.in (eliminar FindOptionalPackage)

### Estimacion post-reparacion:
Despues de corregir los errores fatales, el proyecto **probablemente compilaria** con:
- Eigen3 como unica dependencia obligatoria (via FetchContent si no esta instalada)
- 20+ modulos opcionales que pueden desactivarse individualmente
- Tiempo de compilacion estimado: 10-30 minutos (sin Qt6/SUNDIALS/SCADA), 1-3 horas (full build con FetchContent)

### Rating general:

| Criterio | Puntuacion | Nota |
|----------|-----------|------|
| Estructura CMake raiz | 7/10 | Bien estructurado, opciones claras |
| Sub-CMakeLists.txt | 3/10 | 3 errores fatales, inconsistencia de estilos |
| Tests C++ | 9/10 | Codigo real, alta calidad, pero no integrados |
| Tests Python | 8/10 | Buenos tests con pytest |
| Scripts de build | 7/10 | Bien estructurados pero opciones desincronizadas |
| CI/CD | 0/10 | Ausente |
| Dependencias externas | 7/10 | FetchContent bien usado, algunos REQUIRED innecesarios |
| Documentacion | 5/10 | Comentarios abundantes, pero no documentacion tecnica del build |

**PUNTUACION GLOBAL: 5.5/10 - El proyecto tiene una base solida pero necesita correcciones criticas antes de poder compilarse.**
