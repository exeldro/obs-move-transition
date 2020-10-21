project(move-transition VERSION 1.8.0)
set(PROJECT_FULL_NAME "Move Transition")

set(move-transition_HEADERS
	move-transition.h
	easing.h)
set(move-transition_SOURCES
	move-transition.c
	move-transition-override-filter.c
	move-source-filter.c
	move-value-filter.c
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

add_library(move-transition MODULE
	${move-transition_HEADERS}
	${move-transition_SOURCES})
target_link_libraries(move-transition
	obs-frontend-api
	libobs)

set_target_properties(move-transition PROPERTIES FOLDER "plugins/exeldro")
install_obs_plugin_with_data(move-transition data)
