# ABI module names and installed .so names intentionally remain stable.
add_navigator_module(cockpit_module_transfer transfer ${PROJECT_SOURCE_DIR}/cockpit/transfer/runtime/transfer_entry.cc)
target_link_libraries(cockpit_module_transfer PRIVATE transfer_runtime)

add_navigator_module(
    cockpit_module_vehicle_driver
    vehicle_driver
    ${PROJECT_SOURCE_DIR}/cockpit/driver/vehicle/runtime/vehicle_entry.cc
)
target_link_libraries(cockpit_module_vehicle_driver PRIVATE vehicle_runtime)

add_navigator_module(
    cockpit_module_audio_driver
    audio_driver
    ${PROJECT_SOURCE_DIR}/cockpit/driver/audio/runtime/audio_entry.cc
)
target_link_libraries(cockpit_module_audio_driver PRIVATE audio_runtime)

add_navigator_module(
    cockpit_module_camera_driver
    camera_driver
    ${PROJECT_SOURCE_DIR}/cockpit/driver/camera/runtime/camera_entry.cc
)
target_link_libraries(cockpit_module_camera_driver PRIVATE camera_runtime)

add_navigator_module(cockpit_module_agent agent ${PROJECT_SOURCE_DIR}/cockpit/voice/runtime/agent_entry.cc)
target_link_libraries(cockpit_module_agent PRIVATE agent_runtime)

add_navigator_module(cockpit_module_hmi hmi ${PROJECT_SOURCE_DIR}/cockpit/hmi/runtime/hmi_entry.cc)
target_link_libraries(cockpit_module_hmi PRIVATE hmi_runtime)

add_navigator_module(cockpit_module_media media ${PROJECT_SOURCE_DIR}/cockpit/hmi/media/runtime/media_entry.cc)
target_link_libraries(cockpit_module_media PRIVATE media_runtime)

add_navigator_module(cockpit_module_carupload carupload ${PROJECT_SOURCE_DIR}/cockpit/carcloud/carupload/carupload_entry.cc)
target_link_libraries(cockpit_module_carupload PRIVATE carupload_runtime)

add_navigator_module(cockpit_module_recording recording ${PROJECT_SOURCE_DIR}/cockpit/spider/recording/runtime/recording_entry.cc)
target_link_libraries(cockpit_module_recording PRIVATE recording_runtime)

add_navigator_module(cockpit_module_sentinel sentinel ${PROJECT_SOURCE_DIR}/cockpit/vanguard/sentinel/runtime/sentinel_entry.cc)
target_link_libraries(cockpit_module_sentinel PRIVATE sentinel_runtime)

add_navigator_module(cockpit_module_bridge bridge ${PROJECT_SOURCE_DIR}/cockpit/ros/bridge/runtime/bridge_entry.cc)
target_link_libraries(cockpit_module_bridge PRIVATE bridge_runtime)

add_navigator_module(cockpit_module_upgrader upgrader ${PROJECT_SOURCE_DIR}/cockpit/upgrader/runtime/upgrader_entry.cc)
target_link_libraries(cockpit_module_upgrader PRIVATE upgrader_runtime)

add_navigator_module(cockpit_module_debugger debugger ${PROJECT_SOURCE_DIR}/cockpit/debugger/runtime/debugger_entry.cc)
add_navigator_module(
    cockpit_module_calibration
    calibration
    ${PROJECT_SOURCE_DIR}/cockpit/eunomia/runtime/calibration_entry.cc
)
add_navigator_module(cockpit_module_watchdog watchdog ${PROJECT_SOURCE_DIR}/cockpit/shield/watchdog/watchdog_entry.cc)
