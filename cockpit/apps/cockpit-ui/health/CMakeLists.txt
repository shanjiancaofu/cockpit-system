add_library(ui_health STATIC service_health_model.cc)
set_target_properties(ui_health PROPERTIES AUTOMOC ON)
target_include_directories(ui_health PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_health PUBLIC Qt6::Core contracts)
