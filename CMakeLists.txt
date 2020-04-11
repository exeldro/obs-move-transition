project(move-transition)

set(move-transition_HEADERS
	move-transition.h
	easing.h)
set(move-transition_SOURCES
	move-transition.c
	easing.c)

add_library(move-transition MODULE
	${move-transition_HEADERS}
	${move-transition_SOURCES})
target_link_libraries(move-transition
	obs-frontend-api
	libobs)

install_obs_plugin_with_data(move-transition data)
