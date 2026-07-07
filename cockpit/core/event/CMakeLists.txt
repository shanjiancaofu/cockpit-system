add_cockpit_library(event
    event_queue.cc
    message_bus.cc
)
target_link_libraries(event PUBLIC utils)
