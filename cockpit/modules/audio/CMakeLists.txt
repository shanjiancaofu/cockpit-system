add_library(audio_frames STATIC frames/audio_frame.cc frames/pcm_format.cc)

add_library(audio_analysis STATIC analysis/audio_level_meter.cc)
target_link_libraries(audio_analysis PUBLIC audio_frames)

add_library(audio_capture STATIC capture/audio_capture_stream.cc)
target_link_libraries(audio_capture PUBLIC audio_frames Threads::Threads)

add_library(audio_vad STATIC
    vad/plugin_voice_activity_detector.cc
    vad/speech_segmenter.cc
)
target_link_libraries(audio_vad PUBLIC audio_frames PRIVATE ${CMAKE_DL_LIBS})

add_library(audio_wav STATIC wav/wav_file.cc)
target_link_libraries(audio_wav PUBLIC audio_frames)

add_library(audio_playback INTERFACE)
target_include_directories(audio_playback INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(audio_playback INTERFACE audio_wav)

add_library(audio INTERFACE)
target_link_libraries(audio INTERFACE
    audio_analysis
    audio_capture
    audio_frames
    audio_playback
    audio_vad
    audio_wav
)
