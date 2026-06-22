add_cockpit_library(audio
    audio_capture_stream.cc
    audio_frame.cc
    energy_vad.cc
    pcm_format.cc
    wav_file.cc
)

find_package(Threads REQUIRED)
target_link_libraries(audio PUBLIC Threads::Threads)
