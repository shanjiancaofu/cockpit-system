add_library(logging STATIC logging/logger.cc)
target_include_directories(logging PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(logging PUBLIC Threads::Threads)
