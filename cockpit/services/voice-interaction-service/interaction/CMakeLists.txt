add_cockpit_library(voice_interaction
    voice_interaction_service.cc
)
target_link_libraries(voice_interaction PUBLIC event voice)
