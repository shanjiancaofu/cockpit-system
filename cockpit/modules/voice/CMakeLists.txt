add_library(voice_assistant STATIC assistant/mock_voice_assistant.cc)

add_library(voice_actions STATIC
    actions/cockpit_action_dispatcher.cc
    actions/hmi_command_provider.cc
    actions/mock_action_dispatcher.cc
)
target_link_libraries(voice_actions PUBLIC voice_assistant)

add_library(voice_asr STATIC asr/mock_speech_recognizer.cc)
target_link_libraries(voice_asr PUBLIC audio)
add_library(sherpa_onnx_asr STATIC asr/sherpa_onnx_speech_recognizer.cc)
target_link_libraries(sherpa_onnx_asr PUBLIC audio PRIVATE sherpa-onnx-c-api)

add_library(voice_responses STATIC responses/async_voice_response_sink.cc)

add_library(voice_tts STATIC tts/mock_speech_synthesizer.cc)
target_link_libraries(voice_tts PUBLIC audio)

add_library(voice INTERFACE)
target_link_libraries(voice INTERFACE
    voice_actions
    voice_asr
    voice_assistant
    voice_responses
    voice_tts
)
