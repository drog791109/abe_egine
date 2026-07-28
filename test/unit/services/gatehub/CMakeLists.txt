add_executable(abe_gatehub_registry_test
    abe_gatehub_registry_test.cpp
)

set_target_properties(abe_gatehub_registry_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_gatehub_registry_test
    PRIVATE
        abe_gatehub_server
        abe_common_id
)

add_test(NAME abe_gatehub_registry_test COMMAND abe_gatehub_registry_test)
