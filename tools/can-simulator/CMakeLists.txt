if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(can-simulator main.cc)
    target_link_libraries(can-simulator PRIVATE logging runtime socketcan vehicle)
endif()
