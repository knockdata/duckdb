cmake_minimum_required(VERSION 3.5...3.29)

project(ObjectfsExtension)

include_directories(src/include)

set(OBJECTFS_EXTENSION_FILES
    src/objectfs_extension.cpp
    src/object_file_system.cpp
    src/server_client.cpp)

build_static_extension(objectfs ${OBJECTFS_EXTENSION_FILES})

# winsock is the socket implementation on Windows; everything else has it in libc
if(WIN32)
  target_link_libraries(objectfs_extension ws2_32)
endif()

install(
  TARGETS objectfs_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
