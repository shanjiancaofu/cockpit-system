function(add_navigator_module target_name module_name entry_source)
    add_library(${target_name} MODULE ${entry_source})
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR})
    if(UNIX AND NOT APPLE)
        # A Navigator module communicates exclusively through CockpitModuleGetApi.
        # Keep module implementation symbols out of the process-wide namespace.
        target_link_options(${target_name} PRIVATE
            "LINKER:--exclude-libs,ALL"
            "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cockpit/exchange/abi/module.exports"
        )
        set_property(
            TARGET ${target_name}
            APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cockpit/exchange/abi/module.exports"
        )
    endif()
    set_target_properties(${target_name} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME ${module_name}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/cockpit/modules
        INSTALL_RPATH "$ORIGIN/../.."
    )
endfunction()
