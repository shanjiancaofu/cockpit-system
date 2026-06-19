add_library(core STATIC
    config/Config.cc
    logging/Logger.cc
    runtime/Args.cc
    runtime/ServiceRuntime.cc
    utils/Time.cc
    vehicle/VehicleState.cc
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(MSVC)
    target_compile_options(core PRIVATE /W4)
else()
    target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
endif()
