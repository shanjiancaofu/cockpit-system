add_cockpit_library(voice_assistant assistant/mock_voice_assistant.cc)

add_cockpit_library(voice_actions
    actions/cockpit_action_dispatcher.cc
    actions/hmi_command_provider.cc
    actions/mock_action_dispatcher.cc
)
target_link_libraries(voice_actions PUBLIC voice_assistant)

add_cockpit_library(voice_asr asr/mock_speech_recognizer.cc)
target_link_libraries(voice_asr PUBLIC audio)
if(TARGET whisper)
    add_cockpit_library(whisper_asr asr/whisper_speech_recognizer.cc)
    target_link_libraries(whisper_asr PUBLIC audio whisper)
endif()
if(TARGET sherpa-onnx-c-api)
    add_cockpit_library(sherpa_onnx_asr asr/sherpa_onnx_speech_recognizer.cc)
    target_link_libraries(sherpa_onnx_asr PUBLIC audio PRIVATE sherpa-onnx-c-api)
endif()

add_cockpit_library(voice_responses responses/async_voice_response_sink.cc)

add_cockpit_library(voice_tts tts/mock_speech_synthesizer.cc)
target_link_libraries(voice_tts PUBLIC audio)

add_library(voice INTERFACE)
target_link_libraries(voice INTERFACE
    voice_actions
    voice_asr
    voice_assistant
    voice_responses
    voice_tts
)
