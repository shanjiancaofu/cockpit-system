add_cockpit_library(recording_event_client
    recording_event_publisher.cc
)
target_link_libraries(recording_event_client PUBLIC contracts logging)
