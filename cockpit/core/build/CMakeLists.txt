configure_file(build_info_config.h.in generated/build_info_config.h @ONLY)

add_cockpit_library(build_info build_info.cc)
target_include_directories(build_info
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
