cmake_minimum_required(VERSION 3.25)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets)

qt_standard_project_setup()

qt_add_executable(powsys365_ui main.cpp
    cpp/main_window_controller.cpp
    cpp/sld_scene.cpp
    cpp/theme_manager.cpp
)

qt_add_qml_module(powsys365_ui
    URI POWSYS365
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/MainWindow.qml
        qml/SLDCanvas.qml
        qml/BusComponent.qml
        qml/LineComponent.qml
        qml/GeneratorComponent.qml
        qml/LoadComponent.qml
        qml/Toolbar.qml
        qml/PropertiesPanel.qml
        qml/DarkTheme.qml
        qml/LightTheme.qml
    RESOURCES
        resources/qml.qrc
)

target_link_libraries(powsys365_ui
    PRIVATE Qt6::Quick Qt6::Widgets powsys365_core
)

target_include_directories(powsys365_ui PRIVATE cpp)