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

set(SOURCES "fuse-mbtiles.cpp")

option(USE_LOGGER "Use logger" OFF)
if(USE_LOGGER)
	add_definitions( -DUSE_LOGGER )
	set(LOGGER_DIR . CACHE STRING "Logger include and source directory")
	include_directories(${LOGGER_DIR})
	option(USE_LOGGER_P7 "Use logger P7" OFF)
	if(USE_LOGGER_P7)
		add_definitions( -DUSE_LOGGER_P7 )
		set(P7_INCLUDE_DIR /usr/include/P7 CACHE STRING "P7 logger include directory")
		include_directories(${P7_INCLUDE_DIR})
	endif()
	list(APPEND SOURCES "${LOGGER_DIR}/Logger.cpp")
	list(APPEND HEADERS "${LOGGER_DIR}/Logger.h")
endif() 

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

get_target_property(USED_CXX_STANDARD ${PROJECT_NAME} CXX_STANDARD)
if (USED_CXX_STANDARD LESS 17)
	# use boost::optional
	find_package(Boost REQUIRED)
	include_directories(${Boost_INCLUDE_DIRS})
endif()

add_definitions (-D_FILE_OFFSET_BITS=64)
target_link_libraries (${PROJECT_NAME} fuse sqlite3 z)
if(USE_LOGGER_P7)
	target_link_libraries(${PROJECT_NAME} pthread rt p7.a)
endif()

