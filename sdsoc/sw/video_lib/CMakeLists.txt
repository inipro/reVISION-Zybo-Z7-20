cmake_minimum_required(VERSION 3.3)

project(video_lib C)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX $ENV{HOME} CACHE PATH "default install path" FORCE)
endif()

file(GLOB SRCS src/*.c)
add_library(video STATIC ${SRCS})
set_source_files_properties(src/drm_helper.c PROPERTIES COMPILE_DEFINITIONS WITH_SDSOC)

#add_definitions(-DDEBUG_MODE)

find_package(PkgConfig)
pkg_check_modules(GLIB glib-2.0)
pkg_check_modules(DRM libdrm)

target_include_directories(video PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include/video_lib>
)
target_include_directories(video
	PRIVATE	${CMAKE_CURRENT_SOURCE_DIR}/src/include
	PRIVATE ${DRM_INCLUDE_DIRS}
	PUBLIC	${GLIB_INCLUDE_DIRS}
)

target_link_libraries(video)

install(TARGETS video EXPORT video_lib
		ARCHIVE DESTINATION lib)
install(EXPORT video_lib FILE video-config.cmake DESTINATION lib/cmake/video)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
		DESTINATION include/video_lib
		FILES_MATCHING PATTERN "*.h*"
)
