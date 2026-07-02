include(GNUInstallDirs)

set(cockpit_runtime_targets
    audio-service
    camera-service
    cloud-uplink-service
    cockpit-gateway-service
    vehicle-data-service
    voice-interaction-service
    audio-probe
    camera-ctl
    camera-preview-probe
    camera-probe
    can-simulator
    cockpit-ctl
    topic
    voice-ctl
)

if(TARGET cockpit-ui)
    list(APPEND cockpit_runtime_targets cockpit-ui)
endif()

foreach(target_name IN LISTS cockpit_runtime_targets)
    if(TARGET ${target_name})
        install(TARGETS ${target_name}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
        )
    endif()
endforeach()

set(cockpit_bundled_library_targets
    whisper
    ggml
    ggml-base
    ggml-cpu
    ggml-cuda
)

foreach(target_name IN LISTS cockpit_bundled_library_targets)
    if(TARGET ${target_name})
        set_target_properties(${target_name} PROPERTIES INSTALL_RPATH "$ORIGIN")
        install(TARGETS ${target_name}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT Development
        )
    endif()
endforeach()

install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cockpit-system
    COMPONENT Runtime
)
