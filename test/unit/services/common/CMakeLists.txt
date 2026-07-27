add_executable(abe_service_args_test
    abe_service_args_test.cpp
)

set_target_properties(abe_service_args_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_service_args_test
    PRIVATE
        abe_service_common
)

add_test(NAME abe_service_args_test COMMAND abe_service_args_test)

add_executable(abe_service_runtime_test
    abe_service_runtime_test.cpp
)

set_target_properties(abe_service_runtime_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_service_runtime_test
    PRIVATE
        abe_service_common
)

add_test(NAME abe_service_runtime_test COMMAND abe_service_runtime_test)

add_executable(abe_player_store_proto_test
    abe_player_store_proto_test.cpp
)

set_target_properties(abe_player_store_proto_test
    PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)

target_link_libraries(abe_player_store_proto_test
    PRIVATE
        abe_proto_store
)

add_test(NAME abe_player_store_proto_test COMMAND abe_player_store_proto_test)
