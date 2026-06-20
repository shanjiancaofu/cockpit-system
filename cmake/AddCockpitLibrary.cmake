function(add_cockpit_library target)
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
