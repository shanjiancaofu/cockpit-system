include_guard(GLOBAL)

string(TOLOWER "${COCKPIT_TARGET_SYSTEM}" cockpit_package_system)
set(cockpit_git_short "${COCKPIT_GIT_COMMIT}")
string(LENGTH "${cockpit_git_short}" cockpit_git_length)
if(cockpit_git_length GREATER 8)
    string(SUBSTRING "${cockpit_git_short}" 0 8 cockpit_git_short)
endif()

file(WRITE "${CMAKE_BINARY_DIR}/package-info.env"
    "COCKPIT_VERSION='${PROJECT_VERSION}'\n"
    "COCKPIT_BUILD_TYPE='${CMAKE_BUILD_TYPE}'\n"
    "COCKPIT_TARGET_SYSTEM='${COCKPIT_TARGET_SYSTEM}'\n"
    "COCKPIT_PACKAGE_SYSTEM='${cockpit_package_system}'\n"
    "COCKPIT_TARGET_ARCH='${COCKPIT_TARGET_ARCH}'\n"
    "COCKPIT_COMPILER_PATH='${COCKPIT_COMPILER_PATH}'\n"
    "COCKPIT_COMPILER_ID='${COCKPIT_COMPILER_ID}'\n"
    "COCKPIT_COMPILER_VERSION='${COCKPIT_COMPILER_VERSION}'\n"
    "COCKPIT_GIT_REVISION='${COCKPIT_GIT_COMMIT}'\n"
    "COCKPIT_GIT_SHORT='${cockpit_git_short}'\n"
    "COCKPIT_GIT_DIRTY='${COCKPIT_GIT_DIRTY}'\n"
    "COCKPIT_PROTOBUF_VERSION='${Protobuf_VERSION}'\n"
    "COCKPIT_GRPC_VERSION='${GRPCPP_VERSION}'\n"
)

set(cockpit_runtime_target_candidates
    cockpit-navigator
    audio-probe
    camera-ctl
    camera-preview-probe
    camera-probe
    can-simulator
    cockpit-ctl
    bridge-ctl
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
    cockpit_module_media
    cockpit_module_carupload
    cockpit_module_recording
    cockpit_module_sentinel
    cockpit_module_bridge
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
