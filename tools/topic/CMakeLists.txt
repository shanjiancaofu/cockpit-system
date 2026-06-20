add_library(topic_commands STATIC
    topic_command_line.cc
    topic_echo.cc
    topic_hz.cc
    topic_info.cc
    topic_list.cc
    topic_message.cc
    topic_pub.cc
    topic_store.cc
    topic_text.cc
    topic_usage.cc
)
target_include_directories(topic_commands
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(topic_commands PRIVATE config utils)

add_executable(topic main.cc)
target_link_libraries(topic PRIVATE config topic_commands)
