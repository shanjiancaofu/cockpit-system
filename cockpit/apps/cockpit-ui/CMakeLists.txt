find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

add_subdirectory(vehicle)
add_subdirectory(camera)
add_subdirectory(health)

add_library(ui_model INTERFACE)
target_link_libraries(ui_model INTERFACE ui_vehicle ui_health)

add_executable(cockpit-ui main.cc qml.qrc)
set_target_properties(cockpit-ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_link_libraries(cockpit-ui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    logging
    runtime
    ui_camera
    ui_health
    ui_vehicle
)
