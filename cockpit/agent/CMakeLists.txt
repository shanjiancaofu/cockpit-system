add_library(agent_llm STATIC
    ${PROJECT_SOURCE_DIR}/cockpit/agent/llm/llama_server_local_llm_client.cc
    ${PROJECT_SOURCE_DIR}/cockpit/agent/llm/llama_server_process.cc
    ${PROJECT_SOURCE_DIR}/cockpit/agent/llm/mock_local_llm_client.cc
)
target_include_directories(agent_llm PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(agent_llm PUBLIC json_util PRIVATE logging Threads::Threads)

