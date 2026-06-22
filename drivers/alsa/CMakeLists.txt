find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_cockpit_library(alsa_audio alsa_pcm.cc)
target_link_libraries(alsa_audio PUBLIC audio PkgConfig::ALSA)
