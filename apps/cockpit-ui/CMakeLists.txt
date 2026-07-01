find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

add_library(ui_model STATIC vehicle_state_model.cc)
set_target_properties(ui_model PROPERTIES AUTOMOC ON)
target_include_directories(ui_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_model PUBLIC Qt6::Core)

add_executable(cockpit-ui
    camera_frame_client.cc
    camera_frame_model.cc
    camera_image_provider.cc
    gateway_client.cc
    main.cc
    qml.qrc
)
set_target_properties(cockpit-ui PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_link_libraries(cockpit-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        contracts
        camera_shm
        logging
        runtime
        ui_model
)
