add_common_module(runtime
    Args.cc
    ServiceRuntime.cc
)

target_link_libraries(runtime
    PUBLIC
        config
        logging
)
