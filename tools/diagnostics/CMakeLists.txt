add_library(diagnostic_cli cli_output.cc)
target_include_directories(diagnostic_cli PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(diagnostic_cli PUBLIC contracts json_util)
