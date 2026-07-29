include_guard(GLOBAL)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Protobuf 3.12.4 EXACT REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
find_program(COCKPIT_GRPC_CPP_PLUGIN_EXECUTABLE
    NAMES grpc_cpp_plugin
    REQUIRED
)
