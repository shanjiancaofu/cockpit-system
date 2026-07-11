add_cockpit_library(recording
    recording_catalog.cc
    recording_data_file.cc
    recording_event.cc
    recording_session.cc
    recording_timeline.cc
    recording_integrity.cc
    file_checksum.cc
)
find_package(yaml-cpp REQUIRED)
target_link_libraries(recording PUBLIC base json_util utils vehicle yaml-cpp)
