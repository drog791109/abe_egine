add_executable(abe_base_memory_test
    abe_pool_test.c
)

set_target_properties(abe_base_memory_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_memory_test
    PRIVATE
        abe_base_memory
)

add_test(NAME abe_base_memory_test COMMAND abe_base_memory_test)

add_executable(abe_base_mem_pool_test
    abe_mem_pool_test.c
)

set_target_properties(abe_base_mem_pool_test
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_link_libraries(abe_base_mem_pool_test
    PRIVATE
        abe_base_memory
)

add_test(NAME abe_base_mem_pool_test COMMAND abe_base_mem_pool_test)

add_test(
    NAME abe_no_direct_alloc_test
    COMMAND
        /bin/bash
        ${CMAKE_CURRENT_SOURCE_DIR}/abe_no_direct_alloc_test.sh
        ${CMAKE_SOURCE_DIR}/../..)
