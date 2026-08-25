set(proto_files
    audio.proto
    camera.proto
    cloud.proto
    common.proto
    gateway.proto
    hmi.proto
    media.proto
    recording.proto
    sentinel.proto
    vehicle_state.proto
    voice.proto
)
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")

set(generated_sources)
set(generated_headers)
foreach(proto_file IN LISTS proto_files)
    get_filename_component(proto_name "${proto_file}" NAME_WE)
    list(APPEND generated_sources
        "${generated_dir}/${proto_name}.pb.cc"
        "${generated_dir}/${proto_name}.grpc.pb.cc"
    )
    list(APPEND generated_headers
        "${generated_dir}/${proto_name}.pb.h"
        "${generated_dir}/${proto_name}.grpc.pb.h"
    )

    add_custom_command(
        OUTPUT
            "${generated_dir}/${proto_name}.pb.cc"
            "${generated_dir}/${proto_name}.pb.h"
            "${generated_dir}/${proto_name}.grpc.pb.cc"
            "${generated_dir}/${proto_name}.grpc.pb.h"
        COMMAND protobuf::protoc
        ARGS
            "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}"
            "--cpp_out=${generated_dir}"
            "--grpc_out=${generated_dir}"
            "--plugin=protoc-gen-grpc=${COCKPIT_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}"
            ${proto_files}
        VERBATIM
    )
endforeach()

add_library(contracts SHARED
    ${generated_sources}
    ${generated_headers}
)
set_target_properties(contracts PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)
target_include_directories(contracts PUBLIC "${generated_dir}")
target_link_libraries(contracts
    PUBLIC
        protobuf::libprotobuf
        PkgConfig::GRPCPP
)
