add_library(abe_adapter_math STATIC
    abe_math_adapter.cpp
)

set_target_properties(abe_adapter_math
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_adapter_math
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(abe_adapter_math
    PUBLIC
        abe_base_math
)
