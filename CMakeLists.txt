project (fuse-mbtiles)
cmake_minimum_required (VERSION 2.8)
include_directories (fuse)
aux_source_directory (. SRC_LIST)
add_executable (${PROJECT_NAME} ${SRC_LIST})
add_definitions (-std=c++11)
add_definitions (-D_FILE_OFFSET_BITS=64)
target_link_libraries (${PROJECT_NAME} fuse sqlite3 z)
