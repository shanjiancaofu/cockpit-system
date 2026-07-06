add_library(ui_vehicle STATIC gateway_client.cc vehicle_state_model.cc)
set_target_properties(ui_vehicle PROPERTIES AUTOMOC ON)
target_include_directories(ui_vehicle PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_vehicle PUBLIC Qt6::Core contracts)
