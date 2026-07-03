add_subdirectory(frames)
add_subdirectory(capture)
add_subdirectory(vad)
add_subdirectory(wav)
add_subdirectory(playback)

add_library(audio INTERFACE)
target_link_libraries(audio INTERFACE audio_capture audio_frames audio_playback audio_vad audio_wav)
