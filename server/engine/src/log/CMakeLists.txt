add_library(abe_engine_log STATIC
    abe_log.cpp
)

set_target_properties(abe_engine_log
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_include_directories(abe_engine_log
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(spdlog REQUIRED)

target_link_libraries(abe_engine_log
    PRIVATE
        abe_base_time
        spdlog::spdlog
)
