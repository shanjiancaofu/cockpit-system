add_executable(cockpit_smoke_test smoke_test.cc)
target_link_libraries(cockpit_smoke_test PRIVATE can vehicle)
add_test(NAME cockpit_smoke_test COMMAND cockpit_smoke_test)

add_executable(system_config_test system_config_test.cc)
target_link_libraries(system_config_test PRIVATE config)
target_compile_definitions(system_config_test
    PRIVATE
        VALID_CONFIG_PATH="${PROJECT_SOURCE_DIR}/configs/config.yaml"
        INVALID_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config.yaml"
)
add_test(NAME system_config_test COMMAND system_config_test)
