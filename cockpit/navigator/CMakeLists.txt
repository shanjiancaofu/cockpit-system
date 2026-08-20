add_library(navigator_core STATIC
    common/module_id.cc
    common/run_mode.cc
    connection/ipc_connector.cc
    diagnostics/crash_reporter.cc
    dl_api/module_loader.cc
    navigator.cc
    process/process_manager.cc
    run_config/run_config.cc
)
target_include_directories(navigator_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(navigator_core PUBLIC build_info json_util logging time_util Threads::Threads
    ${CMAKE_DL_LIBS})

add_executable(cockpit-navigator main.cc)
target_link_libraries(cockpit-navigator PRIVATE navigator_core runtime)

function(add_navigator_module target_name module_name entry_source)
    add_library(${target_name} MODULE ${entry_source})
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR})
    if(UNIX AND NOT APPLE)
        # A Navigator module communicates exclusively through CockpitModuleGetApi.
        # Keep module implementation symbols out of the process-wide namespace.
        target_link_options(${target_name} PRIVATE
            "LINKER:--exclude-libs,ALL"
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/common/module.exports"
        )
        set_property(
            TARGET ${target_name}
            APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/common/module.exports"
        )
    endif()
    set_target_properties(${target_name} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME ${module_name}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/cockpit/modules
        INSTALL_RPATH "$ORIGIN/../.."
    )
endfunction()

add_navigator_module(cockpit_module_transfer transfer library/transfer/transfer_entry.cc)
target_link_libraries(cockpit_module_transfer PRIVATE transfer_runtime)

add_navigator_module(
    cockpit_module_vehicle_driver
    vehicle_driver
    library/driver/vehicle/vehicle_entry.cc
)
target_link_libraries(cockpit_module_vehicle_driver PRIVATE vehicle_runtime)

add_navigator_module(
    cockpit_module_audio_driver
    audio_driver
    library/driver/audio/audio_entry.cc
)
target_link_libraries(cockpit_module_audio_driver PRIVATE audio_runtime)

add_navigator_module(
    cockpit_module_camera_driver
    camera_driver
    library/driver/camera/camera_entry.cc
)
target_link_libraries(cockpit_module_camera_driver PRIVATE camera_runtime)

add_navigator_module(cockpit_module_agent agent library/agent/agent_entry.cc)
target_link_libraries(cockpit_module_agent PRIVATE agent_runtime)

add_navigator_module(cockpit_module_hmi hmi library/hmi/hmi_entry.cc)
target_link_libraries(cockpit_module_hmi PRIVATE hmi_runtime)

add_navigator_module(cockpit_module_media media library/media/media_entry.cc)
target_link_libraries(cockpit_module_media PRIVATE media_runtime)

add_navigator_module(cockpit_module_carupload carupload library/carupload/carupload_entry.cc)
target_link_libraries(cockpit_module_carupload PRIVATE carupload_runtime)

add_navigator_module(cockpit_module_recording recording library/recording/recording_entry.cc)
target_link_libraries(cockpit_module_recording PRIVATE recording_runtime)

add_navigator_module(cockpit_module_upgrader upgrader library/upgrader/upgrader_entry.cc)
target_link_libraries(cockpit_module_upgrader PRIVATE upgrader_runtime)

add_navigator_module(cockpit_module_debugger debugger library/debugger/debugger_entry.cc)
add_navigator_module(
    cockpit_module_calibration
    calibration
    library/calibration/calibration_entry.cc
)
add_navigator_module(cockpit_module_watchdog watchdog library/watchdog/watchdog_entry.cc)
