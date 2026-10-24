add_library(can STATIC
    can_frame.cc
    socket_can_adapter.cc
)
target_link_libraries(can PUBLIC socketcan)
