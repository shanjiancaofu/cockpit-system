add_library(audio_frames STATIC frames/audio_frame.cc frames/pcm_format.cc)

add_library(audio_stream_protocol STATIC transport/audio_stream_protocol.cc)
target_link_libraries(audio_stream_protocol PUBLIC audio_frames)

add_library(audio_analysis STATIC analysis/audio_level_meter.cc)
target_link_libraries(audio_analysis PUBLIC audio_frames)

add_library(audio_wav STATIC wav/wav_file.cc)
target_link_libraries(audio_wav PUBLIC audio_frames)

add_library(audio_capture STATIC
    capture/alsa_capture_source.cc
    capture/audio_capture_stream.cc
    capture/wav_capture_source.cc
)
target_link_libraries(audio_capture PUBLIC audio_frames audio_wav alsa_audio Threads::Threads)

add_library(audio_playback STATIC playback/alsa_audio_player.cc)
target_link_libraries(audio_playback PUBLIC audio_wav alsa_audio)

add_library(audio INTERFACE)
target_link_libraries(audio INTERFACE
    audio_analysis
    audio_capture
    audio_frames
    audio_playback
    audio_stream_protocol
    audio_wav
)
