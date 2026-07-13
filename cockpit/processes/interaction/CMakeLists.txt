add_executable(voice-interaction-service main.cc)
target_link_libraries(voice-interaction-service PRIVATE agent_runtime runtime)
