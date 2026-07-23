find_package(Protobuf 3.21.12 CONFIG REQUIRED)
if(NOT Protobuf_VERSION VERSION_EQUAL 3.21.12)
    message(FATAL_ERROR "cockpit-system requires Protobuf 3.21.12; found ${Protobuf_VERSION}")
endif()
find_package(gRPC 1.51.3 EXACT CONFIG REQUIRED)
set(COCKPIT_PROTOC protobuf::protoc)
set(COCKPIT_GRPC_CPP_PLUGIN "$<TARGET_FILE:gRPC::grpc_cpp_plugin>")
set(COCKPIT_GRPCPP_TARGET gRPC::grpc++)

set(proto_files
    audio.proto
    camera.proto
    cloud.proto
    common.proto
    gateway.proto
    hmi.proto
    recording.proto
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
        COMMAND ${COCKPIT_PROTOC}
        ARGS
            "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}"
            "--cpp_out=${generated_dir}"
            "--grpc_out=${generated_dir}"
            "--plugin=protoc-gen-grpc=${COCKPIT_GRPC_CPP_PLUGIN}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${proto_file}"
            ${proto_files}
        VERBATIM
    )
endforeach()

add_library(contracts STATIC
    ${generated_sources}
    ${generated_headers}
)
target_include_directories(contracts PUBLIC "${generated_dir}")
target_link_libraries(contracts
    PUBLIC
        protobuf::libprotobuf
        ${COCKPIT_GRPCPP_TARGET}
)
