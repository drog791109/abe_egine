add_executable(abe_base_shm_test
    abe_shm_pool_test.c
)

set_target_properties(abe_base_shm_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_shm_test
    PRIVATE
        abe_base_shm
)

add_test(NAME abe_base_shm_test COMMAND abe_base_shm_test)
set_tests_properties(abe_base_shm_test PROPERTIES TIMEOUT 10)
