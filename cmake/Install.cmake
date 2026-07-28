include_guard(GLOBAL)

set(cockpit_runtime_target_candidates
    cockpit-navigator
    audio-probe
    camera-ctl
    camera-preview-probe
    camera-probe
    can-simulator
    cockpit-ctl
    recording-ctl
    safe-ota
    topic
    voice-ctl
    cockpit-ui
)
set(cockpit_runtime_targets)
foreach(target_name IN LISTS cockpit_runtime_target_candidates)
    if(TARGET ${target_name})
        list(APPEND cockpit_runtime_targets ${target_name})
        set_target_properties(${target_name} PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
        )
    endif()
endforeach()

set(cockpit_module_targets
    cockpit_module_transfer
    cockpit_module_vehicle_driver
    cockpit_module_audio_driver
    cockpit_module_camera_driver
    cockpit_module_agent
    cockpit_module_hmi
    cockpit_module_carupload
    cockpit_module_recording
    cockpit_module_upgrader
    cockpit_module_debugger
    cockpit_module_calibration
    cockpit_module_watchdog
)
foreach(target_name IN LISTS cockpit_module_targets)
    set_target_properties(${target_name} PROPERTIES
        INSTALL_RPATH "$ORIGIN/../.."
    )
endforeach()
set_target_properties(contracts PROPERTIES INSTALL_RPATH "$ORIGIN")

install(TARGETS ${cockpit_runtime_targets}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime
)
install(TARGETS contracts
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Runtime
)
install(TARGETS ${cockpit_module_targets}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/cockpit/modules
    COMPONENT Runtime
)

install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cockpit-system
    COMPONENT Runtime
)
