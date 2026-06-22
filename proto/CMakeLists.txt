find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(proto_files
    audio.proto
    cloud.proto
    common.proto
    gateway.proto
    vehicle_state.proto
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
        COMMAND "${Protobuf_PROTOC_EXECUTABLE}"
        ARGS
            "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}"
            "--cpp_out=${generated_dir}"
            "--grpc_out=${generated_dir}"
            "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}"
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
        PkgConfig::GRPCPP
)
