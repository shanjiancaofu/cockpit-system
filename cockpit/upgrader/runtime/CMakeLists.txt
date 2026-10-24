add_library(upgrader_runtime STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/upgrader/runtime/upgrade_transaction.cc
    ${PROJECT_SOURCE_DIR}/cockpit/upgrader/runtime/upgrader_runtime.cc
)
target_include_directories(upgrader_runtime PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(upgrader_runtime PUBLIC config logging)
