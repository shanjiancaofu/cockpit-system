if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(can_simulator_commands STATIC can_simulator.cc)
    target_include_directories(can_simulator_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(can_simulator_commands PRIVATE logging runtime socketcan vehicle)

    add_executable(can-simulator main.cc)
    target_link_libraries(can-simulator PRIVATE can_simulator_commands)
endif()
