find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_library(alsa_audio STATIC
    alsa_pcm.cc
)
target_link_libraries(alsa_audio PUBLIC PkgConfig::ALSA)
