add_cockpit_library(audio_vad energy_vad.cc speech_segmenter.cc)
target_link_libraries(audio_vad PUBLIC audio_frames)
