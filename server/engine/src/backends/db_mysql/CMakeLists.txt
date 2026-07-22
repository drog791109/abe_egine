option(ABE_ENGINE_ENABLE_MYSQL "Build MySQL database backend when client library exists" ON)

if(NOT ABE_ENGINE_ENABLE_MYSQL)
    return()
endif()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(ABE_MYSQL_CLIENT QUIET mysqlclient)
    if(NOT ABE_MYSQL_CLIENT_FOUND)
        pkg_check_modules(ABE_MYSQL_CLIENT QUIET mariadb)
    endif()
endif()

if(NOT ABE_MYSQL_CLIENT_FOUND)
    find_path(ABE_MYSQL_INCLUDE_DIR
        NAMES mysql.h
        PATH_SUFFIXES mysql mariadb
    )

    find_library(ABE_MYSQL_LIBRARY
        NAMES mysqlclient mariadb
    )

    if(NOT ABE_MYSQL_INCLUDE_DIR OR NOT ABE_MYSQL_LIBRARY)
        message(WARNING "MySQL client development files were not found; abe_backend_db_mysql will not be built")
        return()
    endif()
endif()

add_library(abe_backend_db_mysql STATIC
    abe_db_mysql.c
)

set_target_properties(abe_backend_db_mysql
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

target_include_directories(abe_backend_db_mysql
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${ABE_MYSQL_CLIENT_INCLUDE_DIRS}
        ${ABE_MYSQL_INCLUDE_DIR}
)

target_link_directories(abe_backend_db_mysql
    PUBLIC
        ${ABE_MYSQL_CLIENT_LIBRARY_DIRS}
)

target_link_libraries(abe_backend_db_mysql
    PUBLIC
        abe_common_db
        ${ABE_MYSQL_CLIENT_LIBRARIES}
        ${ABE_MYSQL_LIBRARY}
)

target_link_options(abe_backend_db_mysql
    PUBLIC
        ${ABE_MYSQL_CLIENT_LDFLAGS_OTHER}
)

target_compile_options(abe_backend_db_mysql
    PRIVATE
        ${ABE_MYSQL_CLIENT_CFLAGS_OTHER}
)
