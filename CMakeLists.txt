cmake_minimum_required(VERSION 3.12..4.0)

project(move-transition VERSION 2.5.7)
set(PROJECT_FULL_NAME "Move Transition")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(move-transition_HEADERS
	move-transition.h
	easing.h
	version.h)
set(move-transition_SOURCES
	move-transition.c
	move-transition-override-filter.c
	move-source-filter.c
	move-value-filter.c
	audio-move.c
	easing.c)

if(WIN32)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(resource.rc.in move-transition.rc)
	list(APPEND move-transition_SOURCES
		move-transition.rc)
endif()

if(BUILD_OUT_OF_TREE)
    find_package(LibObs REQUIRED)
endif()

add_library(move-transition MODULE
	${move-transition_HEADERS}
	${move-transition_SOURCES})
target_link_libraries(move-transition
	obs-frontend-api
	libobs)

if(BUILD_OUT_OF_TREE)
    if(NOT LIB_OUT_DIR)
        set(LIB_OUT_DIR "/lib/obs-plugins")
    endif()
    if(NOT DATA_OUT_DIR)
        set(DATA_OUT_DIR "/share/obs/obs-plugins/move-transition")
    endif()
    set_target_properties(move-transition PROPERTIES PREFIX "")
    install(TARGETS move-transition
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${LIB_OUT_DIR})
    install(DIRECTORY data/locale
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${DATA_OUT_DIR})
else()
    target_include_directories(move-transition PRIVATE
        "${CMAKE_SOURCE_DIR}/UI/obs-frontend-api")
    set_target_properties(move-transition PROPERTIES FOLDER "plugins/exeldro")
    install_obs_plugin_with_data(move-transition data)
endif()
