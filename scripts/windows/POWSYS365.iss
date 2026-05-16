; =============================================================================
; POWSYS365 - Inno Setup 6 Installer Script
; =============================================================================
; Script Name : POWSYS365.iss
; Version     : 3.0.0
; Author      : XNOX L.L.C.
; Description : Professional Windows installer for POWSYS365 v3.0.0.
;               Supports x64 architecture, silent installation, file
;               associations (.raw, .json, .xml, .cim), Visual C++
;               Redistributable prerequisite check, and clean uninstall.
;
; Usage:
;   ISCC.exe POWSYS365.iss
;   ISCC.exe POWSYS365.iss /dMyDefine=Value
;
;   Silent install:   POWSYS365-3.0.0-Setup.exe /SILENT
;   Very silent:      POWSYS365-3.0.0-Setup.exe /VERYSILENT
;   Custom dir:       POWSYS365-3.0.0-Setup.exe /DIR="D:\POWSYS365"
;   Select components:POWSYS365-3.0.0-Setup.exe /COMPONENTS="core,python"
; =============================================================================

#define MyAppName            "POWSYS365"
#define MyAppVersion         "3.0.0"
#define MyAppPublisher       "XNOX L.L.C."
#define MyAppURL             "https://github.com/angelomcfield-ui/powsys365"
#define MyAppSupportURL      "https://github.com/angelomcfield-ui/powsys365/issues"
#define MyAppUpdatesURL      "https://github.com/angelomcfield-ui/powsys365/releases"
#define MyAppExeName         "POWSYS365.exe"
#define MyAppMutex           "POWSYS365_SingleInstance_Mutex_v3"

; ---------------------------------------------------------------------------
; Directory configuration (overridden by PowerShell build script)
; ---------------------------------------------------------------------------
#ifndef SourceDir
#define SourceDir           "..\..\build-windows\package"
#endif
#ifndef ProjectRoot
#define ProjectRoot         "..\.."
#endif
#ifndef OutputDir
#define OutputDir           "..\..\build-windows\dist"
#endif
#ifndef BuildType
#define BuildType           "Release"
#endif

; ---------------------------------------------------------------------------
; Architecture constants
; ---------------------------------------------------------------------------
#define MyAppArch            "x64"
#define MinWindowsVersion    "10.0.17763"

; ---------------------------------------------------------------------------
; Visual C++ Redistributable configuration
; ---------------------------------------------------------------------------
#define VCRedistName         "vc_redist.x64.exe"
#define VCRedistDisplayName  "Microsoft Visual C++ 2015-2022 Redistributable (x64)"
#define VCRedistRegKey       "SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"
#define VCRedistMinVersion   "14.38.33135.0"

[Setup]
; ---------------------------------------------------------------------------
; Application Information
; ---------------------------------------------------------------------------
AppId={{B4A5C8D9-E7F1-4A3B-9C2E-8D5F6A7B0E1C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppSupportURL}
AppUpdatesURL={#MyAppUpdatesURL}
AppCopyright=Copyright (C) 2025 {#MyAppPublisher}. All rights reserved.
AppComments=Power System Analysis Platform - Professional-grade electrical grid simulation and analysis

; ---------------------------------------------------------------------------
; Version Info (visible in file properties)
; ---------------------------------------------------------------------------
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (C) 2025 {#MyAppPublisher}
VersionInfoDescription=Power System Analysis Platform Installer
VersionInfoOriginalFileName={#MyAppName}-{#MyAppVersion}-Setup.exe
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion} ({#MyAppArch})

; ---------------------------------------------------------------------------
; Architecture & Platform
; ---------------------------------------------------------------------------
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion={#MinWindowsVersion}

; ---------------------------------------------------------------------------
; Default Installation Directory
; ---------------------------------------------------------------------------
DefaultDirName={autopf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no

; ---------------------------------------------------------------------------
; Output Configuration
; ---------------------------------------------------------------------------
OutputDir={#OutputDir}
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-{#MyAppArch}-Setup
SetupIconFile={#ProjectRoot}\resources\icon.ico
Compression=lzma2/ultra64
SolidCompression=yes
LZMANumBlockThreads=4
LZMAUseSeparateProcess=yes
LZMADictionarySize=128

; ---------------------------------------------------------------------------
; Disk Spanning (for large packages)
; ---------------------------------------------------------------------------
DiskSpanning=no

; ---------------------------------------------------------------------------
; UI & Wizard Configuration
; ---------------------------------------------------------------------------
WizardStyle=modern
WizardSizePercent=120
WizardImageFile=wizard_image.bmp
WizardSmallImageFile=wizard_small_image.bmp
WizardImageStretch=yes
WizardImageAlphaFormat=premultiplied
ShowLanguageDialog=auto

; ---------------------------------------------------------------------------
; Installation Modes
; ---------------------------------------------------------------------------
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline

; ---------------------------------------------------------------------------
; Uninstall
; ---------------------------------------------------------------------------
UninstallDisplayName={#MyAppName} v{#MyAppVersion}
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
UninstallFilesDir={app}\uninstall
UninstallDisplaySize=250000000
CreateUninstallRegKey=yes
UpdateUninstallLogAppName=yes

; ---------------------------------------------------------------------------
; Other Settings
; ---------------------------------------------------------------------------
DisableWelcomePage=no
DisableDirPage=no
DisableReadyPage=no
DisableFinishedPage=no
AllowNoIcons=no
AllowCancelDuringInstall=yes
ShowTasksTreeLines=yes
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousSetupType=yes
UsePreviousTasks=yes
UsePreviousLanguage=yes
ChangesEnvironment=yes
ChangesAssociations=yes
CloseApplications=force
CloseApplicationsFilter={#MyAppExeName},*.dll
RestartApplications=no

[Languages]
; ---------------------------------------------------------------------------
; Supported Languages
; ---------------------------------------------------------------------------
Name: "english";    MessagesFile: "compiler:Default.isl";                  LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "spanish";    MessagesFile: "compiler:Languages\\Spanish.isl";         LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "french";     MessagesFile: "compiler:Languages\\French.isl";          LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "german";     MessagesFile: "compiler:Languages\\German.isl";          LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "portuguese"; MessagesFile: "compiler:Languages\\BrazilianPortuguese.isl"; LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "italian";    MessagesFile: "compiler:Languages\\Italian.isl";         LicenseFile: "{#ProjectRoot}\LICENSE"
Name: "russian";    MessagesFile: "compiler:Languages\\Russian.isl";         LicenseFile: "{#ProjectRoot}\LICENSE"

[Types]
; ---------------------------------------------------------------------------
; Setup Types
; ---------------------------------------------------------------------------
Name: "full";     Description: "{cm:FullInstallation}";   Description: "{cm:FullInstallation}"
Name: "compact";  Description: "{cm:CompactInstallation}"
Name: "custom";   Description: "{cm:CustomInstallation}"; Flags: iscustom

[Components]
; ---------------------------------------------------------------------------
; Installation Components
; ---------------------------------------------------------------------------
Name: "core";              Description: "Core Application";                  Types: full compact custom; Flags: fixed
Name: "core\main";          Description: "Main Executable & Qt6 Runtime";     Types: full compact custom; Flags: fixed
Name: "core\libraries";     Description: "Core Libraries (Eigen3, libpq, zlib)"; Types: full compact
Name: "core\plugins";       Description: "Qt6 Platform & Image Plugins";      Types: full compact

Name: "python";            Description: "Python Integration";                Types: full custom
Name: "python\runtime";     Description: "Python Runtime & Dependencies";     Types: full
Name: "python\powsy365";    Description: "powsy365 Python Package";           Types: full
Name: "python\scripts";    Description: "AI Python Scripts & RAG Pipeline";  Types: full

Name: "resources";         Description: "Resources & Assets";                Types: full compact
Name: "resources\icons";    Description: "Icons & Themes";                    Types: full compact
Name: "resources\i18n";     Description: "Translations (12 languages)";       Types: full

Name: "database";          Description: "Database Schema & Migrations";      Types: full custom
Name: "database\schema";    Description: "SQL Schema Files";                  Types: full custom
Name: "database\seeds";     Description: "Sample Data Seeds";                 Types: full

Name: "documentation";     Description: "Documentation & Help";              Types: full custom
Name: "documentation\help"; Description: "User Manual & Help Files";          Types: full custom
Name: "documentation\docs"; Description: "API Documentation";                 Types: full custom

Name: "examples";          Description: "Example Projects & Samples";        Types: full custom
Name: "ai";                Description: "AI/LLM Integration Module";         Types: full custom

; ---------------------------------------------------------------------------
[Files]
; =============================================================================
; CORE APPLICATION FILES
; =============================================================================

; --- Main Executable ---
Source: "{#SourceDir}\bin\{#MyAppExeName}";     \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion signonce;                \
    Check: Is64BitInstallMode

; --- MSVC Runtime DLLs ---
Source: "{#SourceDir}\bin\msvcp140.dll";        \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\vcruntime140.dll";    \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\vcruntime140_1.dll";  \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\msvcp140_1.dll";      \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\msvcp140_2.dll";      \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\msvcp140_codecvt_ids.dll"; \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion

; --- Qt6 Core DLLs ---
Source: "{#SourceDir}\bin\Qt6Core.dll";          \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Gui.dll";           \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Widgets.dll";       \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Quick.dll";         \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6QuickControls2.dll"; \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Qml.dll";           \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6QmlModels.dll";     \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6QmlWorkerScript.dll"; \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Network.dll";       \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Sql.dll";           \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Charts.dll";        \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6OpenGL.dll";        \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6OpenGLWidgets.dll"; \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\Qt6Svg.dll";           \
    DestDir: "{app}\bin";                        \
    Components: core\main;                        \
    Flags: ignoreversion

; --- Qt6 Plugins (recursive) ---
Source: "{#SourceDir}\bin\plugins\*";             \
    DestDir: "{app}\bin\plugins";                 \
    Components: core\plugins;                     \
    Flags: ignoreversion recursesubdirs createallsubdirs

; --- Qt6 QML modules (recursive) ---
Source: "{#SourceDir}\bin\qml\*";                 \
    DestDir: "{app}\bin\qml";                     \
    Components: core\plugins;                     \
    Flags: ignoreversion recursesubdirs createallsubdirs

; --- Additional DLLs ---
Source: "{#SourceDir}\bin\*.dll";                 \
    DestDir: "{app}\bin";                        \
    Components: core\libraries;                   \
    Excludes: "Qt6*.dll,msvcp140*.dll,vcruntime140*.dll"; \
    Flags: ignoreversion

; --- QML Source Files ---
Source: "{#SourceDir}\bin\qml\*.qml";            \
    DestDir: "{app}\bin\qml";                    \
    Components: core\main;                        \
    Flags: ignoreversion
Source: "{#SourceDir}\bin\qml\qmldir";           \
    DestDir: "{app}\bin\qml";                    \
    Components: core\main;                        \
    Flags: ignoreversion

; =============================================================================
; PYTHON INTEGRATION FILES
; =============================================================================
Source: "{#SourceDir}\python\*";                  \
    DestDir: "{app}\python";                     \
    Components: python;                           \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; RESOURCES & ASSETS
; =============================================================================
Source: "{#SourceDir}\resources\*";               \
    DestDir: "{app}\resources";                  \
    Components: resources;                        \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; INTERNATIONALIZATION
; =============================================================================
Source: "{#SourceDir}\i18n\*.qm";                 \
    DestDir: "{app}\i18n";                       \
    Components: resources\i18n;                    \
    Flags: ignoreversion
Source: "{#SourceDir}\i18n\*.ts";                 \
    DestDir: "{app}\i18n";                       \
    Components: resources\i18n;                    \
    Flags: ignoreversion

; =============================================================================
; DATABASE SCHEMA & MIGRATIONS
; =============================================================================
Source: "{#SourceDir}\database\schema.sql";       \
    DestDir: "{app}\database";                   \
    Components: database\schema;                   \
    Flags: ignoreversion
Source: "{#SourceDir}\database\migrations\*";     \
    DestDir: "{app}\database\migrations";         \
    Components: database\schema;                   \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\database\queries\*";        \
    DestDir: "{app}\database\queries";            \
    Components: database\schema;                   \
    Flags: ignoreversion
Source: "{#SourceDir}\database\seeds\*";          \
    DestDir: "{app}\database\seeds";              \
    Components: database\seeds;                    \
    Flags: ignoreversion

; =============================================================================
; HELP & DOCUMENTATION
; =============================================================================
Source: "{#SourceDir}\help\*";                    \
    DestDir: "{app}\help";                       \
    Components: documentation\help;                \
    Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#SourceDir}\docs\*";                    \
    DestDir: "{app}\docs";                       \
    Components: documentation\docs;                \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; AI INTEGRATION FILES
; =============================================================================
Source: "{#SourceDir}\ai\*";                      \
    DestDir: "{app}\ai";                         \
    Components: ai;                               \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; CONFIGURATION TEMPLATES
; =============================================================================
Source: "{#SourceDir}\config\*";                   \
    DestDir: "{app}\config";                     \
    Components: core;                             \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; LEGAL FILES
; =============================================================================
Source: "{#ProjectRoot}\LICENSE";                  \
    DestDir: "{app}";                             \
    Components: core;                             \
    Flags: ignoreversion
Source: "{#SourceDir}\legal\*";                    \
    DestDir: "{app}\legal";                      \
    Components: core;                             \
    Flags: ignoreversion recursesubdirs createallsubdirs

; =============================================================================
; README
; =============================================================================
Source: "{#ProjectRoot}\README.md";                \
    DestDir: "{app}";                             \
    Components: core;                             \
    Flags: ignoreversion isreadme

; =============================================================================
; THIRD-PARTY LICENSES
; =============================================================================
Source: "{#ProjectRoot}\third_party\*";            \
    DestDir: "{app}\third_party";                \
    Components: core;                             \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; =============================================================================
; START MENU SHORTCUTS
; =============================================================================
Name: "{group}\{#MyAppName}";                       \
    Filename: "{app}\bin\{#MyAppExeName}";          \
    WorkingDir: "{app}";                             \
    IconFilename: "{app}\resources\icon.ico";        \
    Comment: "Launch POWSYS365"

Name: "{group}\{#MyAppName} (Safe Mode)";           \
    Filename: "{app}\bin\{#MyAppExeName}";          \
    Parameters: "--safe-mode";                       \
    WorkingDir: "{app}";                             \
    IconFilename: "{app}\resources\icon.ico"

Name: "{group}\User Manual";                        \
    Filename: "{app}\docs\README.md"

Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; \
    Filename: "{uninstallexe}"

; =============================================================================
; DESKTOP SHORTCUT
; =============================================================================
Name: "{autodesktop}\{#MyAppName}";                 \
    Filename: "{app}\bin\{#MyAppExeName}";          \
    WorkingDir: "{app}";                             \
    IconFilename: "{app}\resources\icon.ico";        \
    Comment: "Power System Analysis Platform";       \
    Tasks: desktopicon

[Tasks]
; =============================================================================
; USER TASKS
; =============================================================================
Name: "desktopicon";                                 \
    Description: "{cm:CreateDesktopIcon}";           \
    GroupDescription: "{cm:AdditionalIcons}"

Name: "quicklaunchicon";                             \
    Description: "{cm:CreateQuickLaunchIcon}";       \
    GroupDescription: "{cm:AdditionalIcons}";        \
    OnlyBelowVersion: 6.1;                            \
    Check: not IsAdminInstallMode

Name: "fileassoc";                                   \
    Description: "Associate POWSYS365 file types";   \
    GroupDescription: "File Associations:"

Name: "modifypath";                                  \
    Description: "Add to PATH environment variable"; \
    GroupDescription: "System Integration:"

[Registry]
; =============================================================================
; APPLICATION REGISTRY ENTRIES
; =============================================================================
; --- Application registration for Add/Remove Programs ---
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppName}_is1"; \
    ValueType: string;                                        \
    ValueName: "DisplayVersion";                              \
    ValueData: "{#MyAppVersion}";                             \
    Flags: uninsdeletevalue

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\{#MyAppPublisher}\{#MyAppName}";       \
    ValueType: string;                                        \
    ValueName: "InstallDir";                                  \
    ValueData: "{app}";                                       \
    Flags: uninsdeletekey

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\{#MyAppPublisher}\{#MyAppName}";       \
    ValueType: string;                                        \
    ValueName: "Version";                                     \
    ValueData: "{#MyAppVersion}"

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\{#MyAppPublisher}\{#MyAppName}";       \
    ValueType: string;                                        \
    ValueName: "BinDir";                                      \
    ValueData: "{app}\bin"

; =============================================================================
; FILE ASSOCIATIONS
; =============================================================================
; --- .raw (Raw data files) ---
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\.raw";                         \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365RawFile";                            \
    Flags: uninsdeletevalue;                                  \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365RawFile";             \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365 Raw Data File";                     \
    Flags: uninsdeletekey;                                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365RawFile\DefaultIcon"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "{app}\resources\icon.ico,0";                 \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365RawFile\shell\open\command"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: """{app}\bin\{#MyAppExeName}"" ""%1""";       \
    Tasks: fileassoc

; --- .json (JSON data files) ---
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\.json";                        \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365JSONFile";                           \
    Flags: uninsdeletevalue;                                  \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365JSONFile";            \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365 JSON Data File";                    \
    Flags: uninsdeletekey;                                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365JSONFile\DefaultIcon"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "{app}\resources\icon.ico,1";                 \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365JSONFile\shell\open\command"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: """{app}\bin\{#MyAppExeName}"" ""%1""";       \
    Tasks: fileassoc

; --- .xml (XML CIM/UCTE files) ---
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\.xml";                         \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365XMLFile";                            \
    Flags: uninsdeletevalue;                                  \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365XMLFile";             \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365 XML/CIM Data File";                 \
    Flags: uninsdeletekey;                                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365XMLFile\DefaultIcon"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "{app}\resources\icon.ico,2";                 \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365XMLFile\shell\open\command"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: """{app}\bin\{#MyAppExeName}"" ""%1""";       \
    Tasks: fileassoc

; --- .cim (CIM Common Information Model files) ---
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\.cim";                         \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365CIMFile";                            \
    Flags: uninsdeletevalue;                                  \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365CIMFile";             \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "POWSYS365 CIM File";                          \
    Flags: uninsdeletekey;                                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365CIMFile\DefaultIcon"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "{app}\resources\icon.ico,3";                 \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\POWSYS365CIMFile\shell\open\command"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: """{app}\bin\{#MyAppExeName}"" ""%1""";       \
    Tasks: fileassoc

; =============================================================================
; PATH ENVIRONMENT VARIABLE
; =============================================================================
Root: HKLM;                                                   \
    Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz;                                      \
    ValueName: "Path";                                        \
    ValueData: "{olddata};{app}\bin";                        \
    Check: NeedsAddPath('{app}\bin');                         \
    Tasks: modifypath

; =============================================================================
; WINDOWS EXPLORER CONTEXT MENU ("Open with POWSYS365")
; =============================================================================
Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\*\shell\Open with POWSYS365"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: "Open with POWSYS365";                         \
    Flags: uninsdeletekey;                                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\*\shell\Open with POWSYS365"; \
    ValueType: string;                                        \
    ValueName: "Icon";                                        \
    ValueData: "{app}\resources\icon.ico";                    \
    Tasks: fileassoc

Root: HKLM;                                                   \
    Subkey: "SOFTWARE\Classes\*\shell\Open with POWSYS365\command"; \
    ValueType: string;                                        \
    ValueName: "";                                            \
    ValueData: """{app}\bin\{#MyAppExeName}"" ""%1""";       \
    Tasks: fileassoc

[Run]
; =============================================================================
; POST-INSTALLATION ACTIONS
; =============================================================================
; --- Launch application after install ---
Filename: "{app}\bin\{#MyAppExeName}";               \
    Description: "Launch {#MyAppName}";               \
    Flags: postinstall nowait skipifsilent skipifdoesntexist

; --- Open README after install ---
Filename: "{app}\README.md";                         \
    Description: "View README";                       \
    Flags: postinstall nowait skipifsilent shellexec skipifdoesntexist unchecked

[UninstallRun]
; =============================================================================
; PRE-UNINSTALLATION ACTIONS
; =============================================================================
; Clean up any running instance before uninstall
Filename: "taskkill";                                 \
    Parameters: "/F /IM {#MyAppExeName}";             \
    Flags: runhidden skipifdoesntexist

[UninstallDelete]
; =============================================================================
; CLEAN UNINSTALL - Remove directories not tracked by installer
; =============================================================================
Type: filesandordirs;                                 \
    Name: "{app}\bin"
Type: filesandordirs;                                 \
    Name: "{app}\python"
Type: filesandordirs;                                 \
    Name: "{app}\resources"
Type: filesandordirs;                                 \
    Name: "{app}\i18n"
Type: filesandordirs;                                 \
    Name: "{app}\help"
Type: filesandordirs;                                 \
    Name: "{app}\database"
Type: filesandordirs;                                 \
    Name: "{app}\ai"
Type: filesandordirs;                                 \
    Name: "{app}\docs"
Type: filesandordirs;                                 \
    Name: "{app}\legal"
Type: filesandordirs;                                 \
    Name: "{app}\third_party"
Type: filesandordirs;                                 \
    Name: "{app}\config"
Type: filesandordirs;                                 \
    Name: "{app}\logs"
Type: filesandordirs;                                 \
    Name: "{app}\cache"
Type: filesandordirs;                                 \
    Name: "{app}\uninstall"
Type: filesandordirs;                                 \
    Name: "{app}"

[Dirs]
; =============================================================================
; CREATE ADDITIONAL DIRECTORIES
; =============================================================================
Name: "{app}\logs";                                   \
    Permissions: users-full
Name: "{app}\cache";                                 \
    Permissions: users-full
Name: "{app}\temp";                                  \
    Permissions: users-full

[Code]
// =============================================================================
// INNO SETUP CODE SECTION (Pascal Script)
// =============================================================================
// This section contains custom logic for prerequisite checks,
// installation verification, and user experience enhancements.
// =============================================================================

// ---------------------------------------------------------------------------
// Global variables
// ---------------------------------------------------------------------------
var
  VCRedistPage: TWizardPage;
  VCRedistInstalled: Boolean;
  VCRedistVersion: String;

// ---------------------------------------------------------------------------
// Check if a path is already in the PATH environment variable
// ---------------------------------------------------------------------------
function NeedsAddPath(const Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then
  begin
    Result := True;
    Exit;
  end;
  // Look for the path in the current PATH variable
  Result := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';') = 0;
  if Result then
    Result := Pos(';' + UpperCase(Param) + '\', ';' + UpperCase(OrigPath) + ';') = 0;
  if Result then
    Result := Pos(';' + UpperCase(Param), ';' + UpperCase(OrigPath)) = 0;
end;

// ---------------------------------------------------------------------------
// Check Visual C++ Redistributable installation status
// Returns True if a compatible version is installed
// ---------------------------------------------------------------------------
function IsVCRedistInstalled: Boolean;
var
  Major, Minor, Bld, Rev: Cardinal;
  Version: Int64;
  RegPath: string;
begin
  Result := False;
  VCRedistInstalled := False;
  VCRedistVersion := '0.0.0.0';

  // Check 64-bit registry
  RegPath := 'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if RegQueryDWordValue(HKLM, RegPath, 'Major', Major) and
     RegQueryDWordValue(HKLM, RegPath, 'Minor', Minor) and
     RegQueryDWordValue(HKLM, RegPath, 'Bld', Bld) and
     RegQueryDWordValue(HKLM, RegPath, 'Revision', Rev) then
  begin
    VCRedistVersion := Format('%d.%d.%d.%d', [Major, Minor, Bld, Rev]);
    // Version 14.38.xxxx.xxxx or later is acceptable
    if (Major >= 14) and (Minor >= 38) then
    begin
      Result := True;
      VCRedistInstalled := True;
      Exit;
    end;
  end;

  // Fallback: check for any VC++ 2015-2022 redistributable
  RegPath := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if RegQueryDWordValue(HKLM, RegPath, 'Major', Major) and
     RegQueryDWordValue(HKLM, RegPath, 'Minor', Minor) and
     RegQueryDWordValue(HKLM, RegPath, 'Bld', Bld) then
  begin
    VCRedistVersion := Format('%d.%d.%d.%d', [Major, Minor, Bld, Rev]);
    if (Major >= 14) and (Minor >= 30) then
    begin
      Result := True;
      VCRedistInstalled := True;
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Check if the application is currently running
// ---------------------------------------------------------------------------
function IsAppRunning: Boolean;
var
  WbemLocator, WbemServices, WQL, Results, Item: Variant;
  Query: string;
  Count: Integer;
begin
  Result := False;
  try
    WbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    WbemServices := WbemLocator.ConnectServer('localhost', 'root\CIMV2', '', '');
    Query := 'SELECT * FROM Win32_Process WHERE Name="{#MyAppExeName}"';
    WQL := 'WQL';
    Results := WbemServices.ExecQuery(Query, WQL, 48);
    Count := Results.Count;
    if Count > 0 then
    begin
      Result := True;
    end;
  except
    // If WMI fails, assume not running
    Result := False;
  end;
end;

// ---------------------------------------------------------------------------
// Initialize setup wizard
// ---------------------------------------------------------------------------
procedure InitializeWizard;
begin
  // Create custom prerequisite page for VC++ Redist
  VCRedistPage := CreateCustomPage(
    wpWelcome,
    'Visual C++ Redistributable Check',
    'Checking required Microsoft Visual C++ runtime components'
  );

  // Add informative text
  with TNewStaticText.Create(WizardForm) do
  begin
    Parent := VCRedistPage.Surface;
    Left := 0;
    Top := 0;
    Width := VCRedistPage.SurfaceWidth;
    Height := 60;
    WordWrap := True;
    Caption := '{#MyAppName} requires the Microsoft Visual C++ 2015-2022 Redistributable (x64) ' +
               'to be installed on your system. The installer will check for this component ' +
               'and install it if needed.';
  end;

  // Add VC++ status label
  with TNewStaticText.Create(WizardForm) do
  begin
    Parent := VCRedistPage.Surface;
    Left := 0;
    Top := 80;
    Width := VCRedistPage.SurfaceWidth;
    Height := 20;
    Font.Style := [fsBold];
    if IsVCRedistInstalled then
    begin
      Caption := 'Status: Visual C++ Redistributable is installed (version ' + VCRedistVersion + ')';
      Font.Color := clGreen;
    end
    else
    begin
      Caption := 'Status: Visual C++ Redistributable NOT found - will be installed';
      Font.Color := clRed;
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Next button click handler
// ---------------------------------------------------------------------------
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  // Check if application is running before installation starts
  if CurPageID = wpReady then
  begin
    if IsAppRunning then
    begin
      if MsgBox('{#MyAppName} appears to be running. The installation cannot continue ' +
                'while the application is active.' + #13#10 + #13#10 +
                'Please close all instances of {#MyAppName} and click Retry, ' +
                'or click Cancel to abort the installation.',
                mbError, MB_RETRYCANCEL) = IDRETRY then
      begin
        // Check again
        if IsAppRunning then
        begin
          MsgBox('{#MyAppName} is still running. Please close it manually and try again.',
                 mbError, MB_OK);
          Result := False;
        end;
      end
      else
      begin
        Result := False;
      end;
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Prepare to install - VC++ Redist installation
// ---------------------------------------------------------------------------
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  RedistPath: string;
begin
  Result := '';

  // Install VC++ Redistributable if not present
  if not VCRedistInstalled then
  begin
    // Check if we bundled the redistributable
    RedistPath := ExpandConstant('{tmp}\{#VCRedistName}');
    if FileExists(RedistPath) then
    begin
      WizardForm.StatusLabel.Caption := 'Installing Visual C++ Redistributable...';
      if Exec(RedistPath, '/install /quiet /norestart', '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      begin
        if ResultCode = 0 then
        begin
          VCRedistInstalled := True;
        end
        else if ResultCode = 3010 then
        begin
          // Requires restart
          NeedsRestart := True;
          VCRedistInstalled := True;
        end
        else if (ResultCode <> 0) and (ResultCode <> 1638) then
        begin
          // 1638 = newer version already installed
          Result := 'Visual C++ Redistributable installation failed with code ' +
                    IntToStr(ResultCode) + '. Please install it manually from:' + #13#10 +
                    'https://aka.ms/vs/17/release/vc_redist.x64.exe';
        end;
      end
      else
      begin
        Result := 'Failed to launch Visual C++ Redistributable installer.';
      end;
    end
    else
    begin
      // Download VC++ Redist
      if MsgBox('Microsoft Visual C++ 2015-2022 Redistributable (x64) is required ' +
                'but was not found on your system.' + #13#10 + #13#10 +
                'Would you like to download it now from Microsoft?',
                mbConfirmation, MB_YESNO) = IDYES then
      begin
        ShellExec('open', 'https://aka.ms/vs/17/release/vc_redist.x64.exe', '', '',
                  SW_SHOWNORMAL, ewNoWait, ResultCode);
      end;
      Result := 'Please install Visual C++ Redistributable and run the installer again.';
    end;
  end;
end;

// ---------------------------------------------------------------------------
// CurStepChanged - Handle installation steps
// ---------------------------------------------------------------------------
procedure CurStepChanged(CurStep: TSetupStep);
begin
  case CurStep of
    ssInstall:
    begin
      // Log installation start
      Log('{#MyAppName} v{#MyAppVersion} installation starting...');
      Log('Install directory: ' + WizardDirValue);
      Log('Components: ' + WizardSelectedComponents(False));
    end;

    ssPostInstall:
    begin
      // Create log directory with proper permissions
      ForceDirectories(ExpandConstant('{app}\logs'));

      // Write installation manifest
      SaveStringToFile(
        ExpandConstant('{app}\.install_manifest'),
        '{#MyAppName} v{#MyAppVersion}' + #13#10 +
        'Install date: ' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + #13#10 +
        'Install dir: ' + WizardDirValue + #13#10 +
        'Components: ' + WizardSelectedComponents(False) + #13#10,
        False);
    end;

    ssDone:
    begin
      Log('Installation completed successfully.');
    end;
  end;
end;

// ---------------------------------------------------------------------------
// CurUninstallStepChanged - Handle uninstallation steps
// ---------------------------------------------------------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  case CurUninstallStep of
    usUninstall:
    begin
      // Kill any running instances
      Exec('taskkill', '/F /IM {#MyAppExeName}', '', SW_HIDE,
           ewWaitUntilTerminated, 0);
      Sleep(500);
    end;

    usPostUninstall:
    begin
      // Clean up any remaining user data if requested
      if MsgBox('Would you like to remove all user data and configuration files? ' +
                'This includes logs, cache, and custom settings.' + #13#10 +
                'Note: This action cannot be undone.',
                mbConfirmation, MB_YESNO) = IDYES then
      begin
        DelTree(ExpandConstant('{app}\logs'), True, True, True);
        DelTree(ExpandConstant('{app}\cache'), True, True, True);
        DelTree(ExpandConstant('{app}\config'), True, True, True);
      end;

      // Notify completion
      Log('Uninstallation of {#MyAppName} v{#MyAppVersion} completed.');
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Initialize setup - early checks
// ---------------------------------------------------------------------------
function InitializeSetup: Boolean;
begin
  Result := True;

  // Check for minimum Windows version (Windows 10 1809+)
  if not IsWindows10OrGreater then
  begin
    MsgBox('{#MyAppName} requires Windows 10 version 1809 or later.' + #13#10 +
           'Please upgrade your operating system and try again.',
           mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  // Check for 64-bit Windows
  if not Is64BitInstallMode then
  begin
    MsgBox('{#MyAppName} requires a 64-bit version of Windows.' + #13#10 +
           '32-bit systems are not supported.',
           mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  // Check if VC++ Redist is installed (will be used in UI)
  IsVCRedistInstalled;

  Log('{#MyAppName} v{#MyAppVersion} setup initialized');
  Log('Architecture: x64');
  Log('VC++ Redist installed: ' + BoolToStr(VCRedistInstalled));
  Log('VC++ Redist version: ' + VCRedistVersion);
end;

// ---------------------------------------------------------------------------
// ShouldSkipPage - Skip VC++ page if already installed
// ---------------------------------------------------------------------------
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = VCRedistPage.ID) and VCRedistInstalled then
  begin
    // Optionally skip the VC++ check page if already installed
    // Result := True;  // Uncomment to skip for users who already have VC++
  end;
end;

// ---------------------------------------------------------------------------
// UpdateReadyMemo - Show installation summary on Ready page
// ---------------------------------------------------------------------------
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';

  if MemoDirInfo <> '' then
    Result := Result + MemoDirInfo + NewLine + NewLine;

  if MemoTypeInfo <> '' then
    Result := Result + MemoTypeInfo + NewLine + NewLine;

  if MemoComponentsInfo <> '' then
    Result := Result + MemoComponentsInfo + NewLine + NewLine;

  if MemoGroupInfo <> '' then
    Result := Result + MemoGroupInfo + NewLine + NewLine;

  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;

  // Add prerequisite info
  Result := Result + 'Prerequisites:' + NewLine;
  if VCRedistInstalled then
    Result := Result + Space + 'Visual C++ Redistributable: Installed (v' + VCRedistVersion + ')' + NewLine
  else
    Result := Result + Space + 'Visual C++ Redistributable: Will be installed' + NewLine;

  Result := Result + Space + 'Architecture: x64 (64-bit)' + NewLine;
  Result := Result + Space + 'Disk space required: ~250 MB' + NewLine;
end;
