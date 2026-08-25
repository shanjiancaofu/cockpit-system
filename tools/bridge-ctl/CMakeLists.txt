add_executable(bridge-ctl main.cc)
target_include_directories(bridge-ctl PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(bridge-ctl PRIVATE contracts diagnostic_cli logging runtime)
