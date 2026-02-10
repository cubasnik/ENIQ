cmake_minimum_required(VERSION 3.10)
project(eniq_parser VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(eniq_parser
    src/main.cpp
    src/xml_parser.cpp
    src/db_writer.cpp
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
target_include_directories(query_db PRIVATE ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(query_db PRIVATE sqlite3_ext)

# Add simple tests target
add_executable(test_parser
  tests/test_parser.cpp
  src/xml_parser.cpp
  external/pugixml/pugixml.cpp
)
target_include_directories(test_parser PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/pugixml ${CMAKE_SOURCE_DIR}/external)
target_link_libraries(test_parser PRIVATE sqlite3_ext)

add_custom_target(run_tests
  COMMAND test_parser
  DEPENDS test_parser
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

find_package(PkgConfig QUIET)
if(PkgConfig)
  pkg_check_modules(PQXX libpqxx)
endif()

if(PQXX_FOUND)
  target_include_directories(eniq_parser PRIVATE ${PQXX_INCLUDE_DIRS})
  target_link_libraries(eniq_parser PRIVATE ${PQXX_LIBRARIES})
else()
  message(WARNING "libpqxx not found via pkg-config; skipping libpqxx linking (using SQLite)")
endif()

# On Windows you may need to adjust link libraries and include paths.
