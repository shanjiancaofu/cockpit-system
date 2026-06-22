find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

add_executable(cockpit-ui
    gateway_client.cc
    main.cc
    qml.qrc
    vehicle_state_model.cc
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
        logging
        runtime
)
