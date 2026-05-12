cmake_minimum_required(VERSION 3.25)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets WebEngine)

qt_standard_project_setup()

qt_add_executable(powsys365_ide main.cpp
    src/editor.cpp
    src/monaco_editor.cpp
    src/project_manager.cpp
)

qt_add_qml_module(powsys365_ide
    URI POWSYS365IDE
    VERSION 1.0
    QML_FILES
        alexis/main.qml
        alexis/Editor.qml
        alexis/ProjectTree.qml
        alexis/OutputPanel.qml
)

target_link_libraries(powsys365_ide
    PRIVATE Qt6::Quick Qt6::Widgets Qt6::WebEngine powsys365_core
)

target_include_directories(powsys365_ide PRIVATE include)