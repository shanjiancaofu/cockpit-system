add_library(ui_camera STATIC
    camera_control_model.cc
    camera_frame_client.cc
    camera_frame_model.cc
    camera_image_provider.cc
)
set_target_properties(ui_camera PROPERTIES AUTOMOC ON)
target_include_directories(ui_camera PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ui_camera PUBLIC Qt6::Core Qt6::Gui Qt6::Quick camera_shm contracts)
