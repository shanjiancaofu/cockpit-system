function(add_common_module target)
    add_library(${target} STATIC ${ARGN})

    target_include_directories(${target}
        PUBLIC
            ${PROJECT_SOURCE_DIR}
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(config)
add_subdirectory(logging)
add_subdirectory(utils)
add_subdirectory(vehicle)
add_subdirectory(runtime)
add_subdirectory(can)

# Compatibility aggregate for existing binaries. New code should prefer the
# smallest module targets it actually uses.
add_library(core INTERFACE)
target_link_libraries(core
    INTERFACE
        config
        logging
        runtime
        utils
        vehicle
)
