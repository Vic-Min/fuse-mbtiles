project (fuse-mbtiles)
cmake_minimum_required (VERSION 3.1)

# trying to use the latest supported C++ standard, but not lower than C++14 
if (CMAKE_VERSION VERSION_LESS "3.8")
	set (CMAKE_CXX_STANDARD 14)
	set (CMAKE_CXX_STANDARD_REQUIRED ON)
elseif (CMAKE_VERSION VERSION_LESS "3.12")
	set (CMAKE_CXX_STANDARD 17)
	set (CMAKE_CXX_STANDARD_REQUIRED OFF)
elseif (CMAKE_VERSION VERSION_LESS "3.20")
	set (CMAKE_CXX_STANDARD 20)
	set (CMAKE_CXX_STANDARD_REQUIRED OFF)
else()
	set (CMAKE_CXX_STANDARD 23)
	set (CMAKE_CXX_STANDARD_REQUIRED OFF)
endif()
# disable compiler specific extensions for g++, clang, MSVS ...
set (CMAKE_CXX_EXTENSIONS OFF)


include_directories (fuse)
aux_source_directory (. SRC_LIST)
add_executable (${PROJECT_NAME} ${SRC_LIST})


get_target_property(USED_CXX_STANDARD ${PROJECT_NAME} CXX_STANDARD)
if (USED_CXX_STANDARD LESS 17)
	# use boost::optional
	find_package(Boost REQUIRED)
	include_directories(${Boost_INCLUDE_DIRS})
endif()


add_definitions (-D_FILE_OFFSET_BITS=64)
target_link_libraries (${PROJECT_NAME} fuse sqlite3 z)
