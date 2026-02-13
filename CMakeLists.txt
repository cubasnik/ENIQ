cmake_minimum_required(VERSION 3.10)
project(eniq_parser VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to explicitly enable libpqxx/Postgres support
option(ENABLE_PQXX "Enable libpqxx (Postgres) support" OFF)

add_executable(eniq_parser
    src/main.cpp
    src/xml_parser.cpp
    src/db_writer.cpp
    src/logger.cpp
  external/pugixml/pugixml.cpp # remove if using system pugixml
  external/sqlite3.c
)

target_include_directories(eniq_parser PRIVATE external/pugixml)
target_include_directories(eniq_parser PRIVATE ${CMAKE_SOURCE_DIR}/external)

# Build sqlite3 into a static library and link it to avoid missing symbols
add_library(sqlite3_ext STATIC external/sqlite3.c)
target_include_directories(sqlite3_ext PUBLIC ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(eniq_parser PRIVATE sqlite3_ext)

# small utility to inspect the SQLite DB
add_executable(query_db
  src/query_db.cpp
)
target_include_directories(query_db PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(query_db PRIVATE sqlite3_ext)

# Link db_writer into query_db so `g_use_russian` symbol is defined
target_sources(query_db PRIVATE src/db_writer.cpp src/logger.cpp)

# Add simple tests target
add_executable(test_parser
  tests/test_parser.cpp
  src/xml_parser.cpp
  src/db_writer.cpp
  external/pugixml/pugixml.cpp
)
target_include_directories(test_parser PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/pugixml ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(test_parser PRIVATE sqlite3_ext)

add_custom_target(run_tests
  COMMAND test_parser
  DEPENDS test_parser
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_localization
  tests/test_localization.cpp
)
target_link_libraries(test_localization PRIVATE sqlite3_ext)

add_custom_target(run_localization_tests
  COMMAND test_localization
  DEPENDS test_localization
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Namespace-aware parser unit test
add_executable(test_parser_ns
  tests/test_parser_ns.cpp
  src/xml_parser.cpp
  external/pugixml/pugixml.cpp
)
target_include_directories(test_parser_ns PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/pugixml ${CMAKE_SOURCE_DIR}/external)
target_sources(test_parser_ns PRIVATE src/db_writer.cpp)
target_sources(test_parser_ns PRIVATE src/db_writer.cpp src/logger.cpp)
target_link_libraries(test_parser_ns PRIVATE sqlite3_ext)

add_custom_target(run_ns_tests
  COMMAND test_parser_ns
  DEPENDS test_parser_ns
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_cli
  tests/test_cli.cpp
)
target_sources(test_parser PRIVATE src/logger.cpp)
target_link_libraries(test_cli PRIVATE sqlite3_ext)

add_custom_target(run_cli_tests
  COMMAND test_cli
  DEPENDS test_cli
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

find_package(PkgConfig QUIET)
if(PkgConfig)
  pkg_check_modules(PQXX libpqxx)
endif()

# Optional gzip support
find_package(ZLIB REQUIRED)
message(STATUS "ZLIB found: enabling gzip (.gz) input support -> ${ZLIB_VERSION}")

# Determine availability: pkg-config or user-requested ENABLE_PQXX with manual vars
set(PQXX_AVAILABLE FALSE)
if(PQXX_FOUND)
  set(PQXX_AVAILABLE TRUE)
elseif(ENABLE_PQXX)
  if(DEFINED PQXX_INCLUDE_DIRS AND DEFINED PQXX_LIBRARIES)
    set(PQXX_AVAILABLE TRUE)
  else()
    message(WARNING "ENABLE_PQXX=ON but libpqxx not found via pkg-config and PQXX_INCLUDE_DIRS/PQXX_LIBRARIES not provided; Postgres support will be skipped.")
  endif()
endif()

if(PQXX_AVAILABLE)
  target_include_directories(eniq_parser PRIVATE ${PQXX_INCLUDE_DIRS})
  target_link_libraries(eniq_parser PRIVATE ${PQXX_LIBRARIES})
  # Add PostgreSQL integration test when libpqxx is available
  add_executable(test_pg_integration
    tests/test_pg_integration.cpp
  )
  target_include_directories(test_pg_integration PRIVATE ${CMAKE_SOURCE_DIR}/src ${PQXX_INCLUDE_DIRS})
  target_link_libraries(test_pg_integration PRIVATE ${PQXX_LIBRARIES})
  add_custom_target(run_pg_integration_tests
    COMMAND test_pg_integration
    DEPENDS test_pg_integration
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  enable_testing()
  add_test(NAME test_pg_integration COMMAND test_pg_integration)
else()
  message(WARNING "libpqxx not found (or ENABLE_PQXX not set); skipping Postgres support and integration test")
endif()

# Link ZLIB where appropriate
target_link_libraries(eniq_parser PRIVATE ZLIB::ZLIB)
# tests that compile xml_parser also need zlib
target_link_libraries(test_parser PRIVATE ZLIB::ZLIB)
target_link_libraries(test_parser_ns PRIVATE ZLIB::ZLIB)
target_include_directories(eniq_parser PRIVATE ${ZLIB_INCLUDE_DIR})
target_compile_definitions(eniq_parser PRIVATE HAVE_ZLIB)
target_compile_definitions(test_parser PRIVATE HAVE_ZLIB)
target_compile_definitions(test_parser_ns PRIVATE HAVE_ZLIB)

# Ensure testing is enabled and add a gzip-aware unit test
enable_testing()
add_executable(test_gz_parser
  tests/test_gz_parser.cpp
  src/xml_parser.cpp
)
target_include_directories(test_gz_parser PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external ${CMAKE_SOURCE_DIR}/external/pugixml)
target_sources(test_gz_parser PRIVATE ${CMAKE_SOURCE_DIR}/external/pugixml/pugixml.cpp)
target_link_libraries(test_gz_parser PRIVATE sqlite3_ext ZLIB::ZLIB)
target_sources(test_gz_parser PRIVATE src/logger.cpp)
target_compile_definitions(test_gz_parser PRIVATE HAVE_ZLIB)
add_test(NAME test_gz_parser COMMAND test_gz_parser)

# Test for cleanTestRecords dry-run and actual removal
add_executable(test_clean_test_records
  tests/test_clean_test_records.cpp
  src/db_writer.cpp
  src/logger.cpp
)
target_include_directories(test_clean_test_records PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(test_clean_test_records PRIVATE sqlite3_ext)
add_test(NAME test_clean_test_records COMMAND test_clean_test_records)

# Integration tests for new CLI options
add_executable(test_summary_options
  tests/test_summary_options.cpp
)
target_link_libraries(test_summary_options PRIVATE sqlite3_ext)
add_test(NAME test_summary_options COMMAND test_summary_options)

add_executable(test_csv_suffix_option
  tests/test_csv_suffix_option.cpp
)
target_link_libraries(test_csv_suffix_option PRIVATE sqlite3_ext)
add_test(NAME test_csv_suffix_option COMMAND test_csv_suffix_option)

# QPI query test
add_executable(test_qpi_query
  tests/test_qpi_query.cpp
)
target_link_libraries(test_qpi_query PRIVATE sqlite3_ext)
add_test(NAME test_qpi_query COMMAND test_qpi_query)

add_executable(test_rebuild_indexes
  tests/test_rebuild_indexes.cpp
)
target_link_libraries(test_rebuild_indexes PRIVATE sqlite3_ext)
add_test(NAME test_rebuild_indexes COMMAND test_rebuild_indexes)

add_executable(print_qpi
  tests/print_qpi.cpp
)
target_link_libraries(print_qpi PRIVATE sqlite3_ext)
add_test(NAME print_qpi COMMAND print_qpi)

# On Windows you may need to adjust link libraries and include paths.
