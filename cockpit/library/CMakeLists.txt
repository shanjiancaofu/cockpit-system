include(GNUInstallDirs)

function(add_cockpit_module target_name module_name source)
    add_library(${target_name} MODULE ${source})
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR})
    set_target_properties(${target_name} PROPERTIES
        OUTPUT_NAME ${module_name}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/cockpit/modules
    )
    install(TARGETS ${target_name}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/cockpit/modules
        COMPONENT Runtime
    )
endfunction()

add_cockpit_module(cockpit_module_transfer transfer transfer/entry.cc)
add_cockpit_module(cockpit_module_vehicle_driver vehicle_driver driver/vehicle/entry.cc)
add_cockpit_module(cockpit_module_audio_driver audio_driver driver/audio/entry.cc)
add_cockpit_module(cockpit_module_camera_driver camera_driver driver/camera/entry.cc)
add_cockpit_module(cockpit_module_agent agent agent/entry.cc)
add_cockpit_module(cockpit_module_hmi hmi hmi/entry.cc)
add_cockpit_module(cockpit_module_carupload carupload carupload/entry.cc)
add_cockpit_module(cockpit_module_recording recording recording/entry.cc)
add_cockpit_module(cockpit_module_upgrader upgrader upgrader/entry.cc)
add_cockpit_module(cockpit_module_debugger debugger debugger/entry.cc)
add_cockpit_module(cockpit_module_calibration calibration calibration/entry.cc)
add_cockpit_module(cockpit_module_watchdog watchdog watchdog/entry.cc)
