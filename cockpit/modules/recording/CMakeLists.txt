add_cockpit_library(recording
    recording_catalog.cc
    recording_event.cc
    recording_session.cc
)
find_package(yaml-cpp REQUIRED)
target_link_libraries(recording PUBLIC utils vehicle yaml-cpp)
