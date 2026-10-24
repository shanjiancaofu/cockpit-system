add_library(sentinel STATIC sentinel_service.cc)
target_include_directories(sentinel PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(sentinel PUBLIC base time_util Threads::Threads)
