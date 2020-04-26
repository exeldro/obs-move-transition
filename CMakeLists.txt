project(move-transition)

set(move-transition_HEADERS
	move-transition.h
	easing.h)
set(move-transition_SOURCES
	move-transition.c
	move-transition-override-filter.c
	move-source-filter.c
	easing.c)

if(WIN32)
	list(APPEND move-transition_SOURCES
		move-transition.rc)
endif()

add_library(move-transition MODULE
	${move-transition_HEADERS}
	${move-transition_SOURCES})
target_link_libraries(move-transition
	obs-frontend-api
	libobs)

install_obs_plugin_with_data(move-transition data)
