add_library(recording STATIC
    recording_catalog.cc
    recording_data_file.cc
    recording_event.cc
    recording_session.cc
    recording_timeline.cc
    recording_integrity.cc
    file_checksum.cc
)

add_library(recording_event_client STATIC client/recording_event_publisher.cc)
target_link_libraries(recording_event_client PUBLIC contracts logging Threads::Threads)
target_link_libraries(recording PUBLIC base json_util time_util vehicle yaml-cpp)
