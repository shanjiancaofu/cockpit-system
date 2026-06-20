add_executable(can-simulator main.cc)
target_link_libraries(can-simulator PRIVATE can core)
