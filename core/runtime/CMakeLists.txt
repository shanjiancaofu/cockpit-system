add_cockpit_library(runtime
    Args.cc
    ServiceRuntime.cc
)

target_link_libraries(runtime
    PUBLIC
        config
        logging
)
