add_cockpit_library(recording_event_client
    recording_event_publisher.cc
)
find_package(Threads REQUIRED)
target_link_libraries(recording_event_client PUBLIC contracts logging Threads::Threads)
