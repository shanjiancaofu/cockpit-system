add_library(audio_service INTERFACE)
target_link_libraries(audio_service INTERFACE audio_processing)

add_executable(audio-service main.cc)
target_link_libraries(audio-service PRIVATE audio_runtime runtime)
