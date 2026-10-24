add_library(chassis_safety STATIC chassis/chassis_safety_adapter.cc)
target_include_directories(chassis_safety PUBLIC ${PROJECT_SOURCE_DIR})
