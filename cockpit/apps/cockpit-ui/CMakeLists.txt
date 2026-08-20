find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

add_library(ui_app_launcher STATIC apps/app_launcher_model.cc)
set_target_properties(ui_app_launcher PROPERTIES AUTOMOC ON)
target_include_directories(ui_app_launcher PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_app_launcher PUBLIC Qt6::Core)

add_library(ui_hmi STATIC hmi_control.cc)
set_target_properties(ui_hmi PROPERTIES AUTOMOC ON)
target_include_directories(ui_hmi PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_hmi PUBLIC Qt6::Core contracts logging)

add_library(ui_vehicle STATIC vehicle/gateway_client.cc vehicle/vehicle_state_model.cc)
set_target_properties(ui_vehicle PROPERTIES AUTOMOC ON)
target_include_directories(ui_vehicle PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_vehicle PUBLIC Qt6::Core contracts)

add_library(ui_camera STATIC
    camera/camera_control_model.cc
    camera/camera_frame_client.cc
    camera/camera_frame_model.cc
    camera/camera_image_provider.cc
)
set_target_properties(ui_camera PROPERTIES AUTOMOC ON)
target_include_directories(ui_camera PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_camera PUBLIC Qt6::Core Qt6::Gui Qt6::Quick camera_shm contracts)

add_library(ui_health STATIC health/service_health_model.cc)
set_target_properties(ui_health PROPERTIES AUTOMOC ON)
target_include_directories(ui_health PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_health PUBLIC Qt6::Core contracts service_health)

add_library(ui_media STATIC media/media_control_model.cc)
set_target_properties(ui_media PROPERTIES AUTOMOC ON)
target_include_directories(ui_media PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_media PUBLIC Qt6::Core)

add_library(ui_voice STATIC voice/voice_status_model.cc)
set_target_properties(ui_voice PROPERTIES AUTOMOC ON)
target_include_directories(ui_voice PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_voice PUBLIC Qt6::Core contracts)

add_library(ui_model INTERFACE)
target_link_libraries(ui_model INTERFACE ui_app_launcher ui_vehicle ui_health ui_media ui_voice)

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
    ui_app_launcher
    ui_camera
    ui_health
    ui_hmi
    ui_media
    ui_vehicle
    ui_voice
)
