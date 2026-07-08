add_cockpit_library(runtime
    Args.cc
    DependencyGraph.cc
    Module.cc
    ModuleManager.cc
    ServiceRuntime.cc
)

target_link_libraries(runtime
    PUBLIC
        base
        config
        logging
)
