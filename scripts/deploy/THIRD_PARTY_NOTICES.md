# Third-Party Notices

cockpit-system links to third-party software supplied by the target operating system or selected
at build time. Their respective licenses remain applicable.

Optional bundled runtime libraries may include:

- whisper.cpp and GGML: MIT License.
- sherpa-onnx: Apache License 2.0. Optional sherpa-onnx model files retain their respective
  model licenses.

System-provided runtime dependencies may include Qt, ALSA, gRPC, protobuf, yaml-cpp, GStreamer,
and their transitive dependencies. Consult the installed package metadata on the target image for
the exact versions and license texts.
