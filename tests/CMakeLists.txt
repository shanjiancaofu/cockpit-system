add_executable(cockpit_smoke_test smoke_test.cc)
target_link_libraries(cockpit_smoke_test PRIVATE can vehicle)
add_test(NAME cockpit_smoke_test COMMAND cockpit_smoke_test)

add_executable(audio_wav_test audio_wav_test.cc)
target_link_libraries(audio_wav_test PRIVATE audio)
add_test(NAME audio_wav_test COMMAND audio_wav_test)

add_executable(system_config_test system_config_test.cc)
target_link_libraries(system_config_test PRIVATE config)
target_compile_definitions(system_config_test
    PRIVATE
        VALID_CONFIG_PATH="${PROJECT_SOURCE_DIR}/configs/config.yaml"
        INVALID_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_config.yaml"
        INVALID_AUDIO_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid_audio_config.yaml"
)
add_test(NAME system_config_test COMMAND system_config_test)

if(TARGET ui_model)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_executable(vehicle_state_model_test vehicle_state_model_test.cc)
    set_target_properties(vehicle_state_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(vehicle_state_model_test PRIVATE Qt6::Test ui_model)
    add_test(NAME vehicle_state_model_test COMMAND vehicle_state_model_test)
endif()
