add_cockpit_library(audio_wav wav_file.cc)
target_link_libraries(audio_wav PUBLIC audio_frames)
