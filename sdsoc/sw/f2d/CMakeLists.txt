cmake_minimum_required(VERSION 2.8.9)

project(f2d)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX $ENV{HOME} CACHE PATH "default install path" FORCE)
endif()

find_package(video REQUIRED)

find_package(PkgConfig)
pkg_check_modules(OpenCV opencv)
pkg_check_modules(GLIB glib-2.0)
pkg_check_modules(DRM libdrm)

set(SRCS main.c top/filter2d.c top/filter2d_cv.cpp)

set_source_files_properties(main.c PROPERTIES COMPILE_DEFINITIONS SAMPLE_FILTER2D)

add_executable(f2d.elf ${SRCS})

target_include_directories(f2d.elf 
	PRIVATE ${OpenCV_INCLUDE_DIRS} 
	PRIVATE	${CMAKE_CURRENT_SOURCE_DIR}/top 
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(f2d.elf
	${OpenCV_LIBRARIES} 
	video 
	${DRM_LIBRARIES} 
	${GLIB_LIBRARIES} 
	mediactl 
	v4l2subdev 
	pthread
)

if (SW_ONLY)
	add_library(sds_lib STATIC IMPORTED)
	set_target_properties(sds_lib PROPERTIES IMPORTED_LOCATION /usr/lib/libsds_lib.a)
	target_link_libraries(f2d.elf sds_lib) 
else ()
	add_definitions(-DWITH_SDSOC)
	add_library(f2d SHARED IMPORTED)
	set_target_properties(f2d PROPERTIES IMPORTED_LOCATION $ENV{HOME}/lib/libf2d.so)
	target_link_libraries(f2d.elf f2d)
endif()

install(TARGETS f2d.elf DESTINATION bin)
