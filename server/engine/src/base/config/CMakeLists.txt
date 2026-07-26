find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
pkg_check_modules(XML2 REQUIRED IMPORTED_TARGET libxml-2.0)

add_library(abe_base_config STATIC
    abe_config.c
)

set_target_properties(abe_base_config
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_base_config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_base_config
    PUBLIC
        abe_base_error
        abe_base_memory
    PRIVATE
        PkgConfig::JSONC
        PkgConfig::XML2
)
