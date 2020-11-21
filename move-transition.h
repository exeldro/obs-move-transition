#pragma once

#include <obs-module.h>
#include <util/darray.h>

#define MOVE_SOURCE_FILTER_ID "move_source_filter"
#define MOVE_VALUE_FILTER_ID "move_value_filter"
#define MOVE_AUDIO_VALUE_FILTER_ID "move_audio_value_filter"

#define S_MATCH "match"
#define S_MOVE_ALL "move_all"
#define S_MOVE_MATCH "move_match"
#define S_MOVE_IN "move_in"
#define S_MOVE_OUT "move_out"

#define S_NAME_PART_MATCH "name_part_match"
#define S_NAME_NUMBER_MATCH "name_number_match"
#define S_NAME_LAST_WORD_MATCH "name_last_word_match"

#define S_GENERAL "general"
#define S_SOURCE "source"
#define S_POSITION_IN "position_in"
#define S_POSITION_OUT "position_out"
#define S_ZOOM_IN "zoom_in"
#define S_ZOOM_OUT "zoom_out"
#define S_EASING_MATCH "easing_match"
#define S_EASING_IN "easing_in"
#define S_EASING_OUT "easing_out"
#define S_EASING_FUNCTION_MATCH "easing_function_match"
#define S_EASING_FUNCTION_IN "easing_function_in"
#define S_EASING_FUNCTION_OUT "easing_function_out"
#define S_CURVE_MATCH "curve_match"
#define S_CURVE_IN "curve_in"
#define S_CURVE_OUT "curve_out"
#define S_CURVE_OVERRIDE_MATCH "curve_override_match"
#define S_CURVE_OVERRIDE_IN "curve_override_in"
#define S_CURVE_OVERRIDE_OUT "curve_override_out"
#define S_TRANSITION_MATCH "transition_match"
#define S_TRANSITION_SCALE "transition_scale_match"
#define S_TRANSITION_IN "transition_in"
#define S_TRANSITION_OUT "transition_out"
#define S_START_DELAY_MATCH_FROM "start_delay_match_from"
#define S_START_DELAY_MATCH_TO "start_delay_match_to"
#define S_START_DELAY_IN "start_delay_in"
#define S_START_DELAY_OUT "start_delay_out"
#define S_END_DELAY_MATCH_FROM "end_delay_match_from"
#define S_END_DELAY_MATCH_TO "end_delay_match_to"
#define S_END_DELAY_IN "end_delay_in"
#define S_END_DELAY_OUT "end_delay_out"
#define S_CUSTOM_DURATION "custom_duration"
#define S_DURATION "duration"
#define S_ROT "rot"
#define S_POS "pos"
#define S_SCALE "scale"
#define S_BOUNDS "bounds"
#define S_CROP_LEFT "crop_left"
#define S_CROP_TOP "crop_top"
#define S_CROP_RIGHT "crop_right"
#define S_CROP_BOTTOM "crop_bottom"
#define S_TRANSFORM_TEXT "transform_text"
#define S_SWITCH_PERCENTAGE "switch_percentage"
#define S_CACHE_TRANSITIONS "cache_transitions"
#define S_START_TRIGGER "start_trigger"
#define S_STOP_TRIGGER "stop_trigger"
#define S_START_DELAY "start_delay"
#define S_END_DELAY "end_delay"
#define S_ACTIONS "actions"
#define S_SIMULTANEOUS_MOVE "simultaneous_move"
#define S_NEXT_MOVE "next_move"
#define S_NEXT_MOVE_ON "next_move_on"
#define S_FILTER "filter"
#define S_SINGLE_SETTING "single_setting"
#define S_SETTING_VALUE "setting_value"
#define S_SETTING_NAME "setting_name"
#define S_SETTING_INT "setting_int"
#define S_SETTING_FLOAT "setting_float"
#define S_SETTING_COLOR "setting_color"
#define S_SETTINGS "settings"
#define S_SETTING_FROM "setting_from"
#define S_SETTING_TO "setting_to"
#define S_VALUE_TYPE "value_type"
#define S_TRANSFORM "transform"
#define S_TRANSFORM_RELATIVE "transform_relative"
#define S_VISIBILITY_ORDER "visibility_order"
#define S_CHANGE_VISIBILITY "change_visibility"
#define S_CHANGE_ORDER "change_order"
#define S_ORDER_POSITION "order_position"
#define S_MEDIA_ACTION "media_action"
#define S_MEDIA_ACTION_START "media_action_start"
#define S_MEDIA_ACTION_START_TIME "media_action_start_time"
#define S_MEDIA_ACTION_END "media_action_end"
#define S_MEDIA_ACTION_END_TIME "media_action_end_time"
#define S_AUDIO_ACTION "audio_action"
#define S_MUTE_ACTION "mute_action"
#define S_AUDIO_FADE "audio_fade"
#define S_AUDIO_FADE_PERCENT "audio_fade_percent"

#define NO_OVERRIDE (-1)

#define ZOOM_NO 0
#define ZOOM_YES 1

#define EASE_NONE 0
#define EASE_IN 1
#define EASE_OUT 2
#define EASE_IN_OUT 3

#define EASING_QUADRATIC 1
#define EASING_CUBIC 2
#define EASING_QUARTIC 3
#define EASING_QUINTIC 4
#define EASING_SINE 5
#define EASING_CIRCULAR 6
#define EASING_EXPONENTIAL 7
#define EASING_ELASTIC 8
#define EASING_BOUNCE 9
#define EASING_BACK 10

#define POS_NONE 0
#define POS_CENTER (1 << 0)
#define POS_EDGE (1 << 1)
#define POS_LEFT (1 << 2)
#define POS_RIGHT (1 << 3)
#define POS_TOP (1 << 4)
#define POS_BOTTOM (1 << 5)
#define POS_SWIPE (1 << 6)

#define START_TRIGGER_NONE 0
#define START_TRIGGER_ACTIVATE 1
#define START_TRIGGER_DEACTIVATE 2
#define START_TRIGGER_SHOW 3
#define START_TRIGGER_HIDE 4
#define START_TRIGGER_ENABLE 5
#define START_TRIGGER_SOURCE_ACTIVATE 6
#define START_TRIGGER_SOURCE_DEACTIVATE 7
#define START_TRIGGER_SOURCE_SHOW 8
#define START_TRIGGER_SOURCE_HIDE 9
#define START_TRIGGER_ENABLE_DISABLE 10
#define START_TRIGGER_MEDIA_STARTED 11
#define START_TRIGGER_MEDIA_ENDED 12

#define MOVE_VALUE_UNKNOWN 0
#define MOVE_VALUE_INT 1
#define MOVE_VALUE_FLOAT 2
#define MOVE_VALUE_COLOR 3

#define NEXT_MOVE_ON_END 0
#define NEXT_MOVE_ON_HOTKEY 1
#define NEXT_MOVE_REVERSE "Reverse"

#define CHANGE_VISIBILITY_NONE 0
#define CHANGE_VISIBILITY_SHOW_START 1
#define CHANGE_VISIBILITY_HIDE_END 2
#define CHANGE_VISIBILITY_TOGGLE 3
#define CHANGE_VISIBILITY_SHOW_END 4
#define CHANGE_VISIBILITY_HIDE_START 5
#define CHANGE_VISIBILITY_TOGGLE_START 6
#define CHANGE_VISIBILITY_TOGGLE_END 7
#define CHANGE_VISIBILITY_SHOW_START_END 8
#define CHANGE_VISIBILITY_HIDE_START_END 9

#define CHANGE_ORDER_NONE 0
#define CHANGE_ORDER_RELATIVE (1 << 0)
#define CHANGE_ORDER_ABSOLUTE (1 << 1)
#define CHANGE_ORDER_START (1 << 2)
#define CHANGE_ORDER_END (1 << 3)

#define MEDIA_ACTION_NONE 0
#define MEDIA_ACTION_PLAY 1
#define MEDIA_ACTION_PAUSE 2
#define MEDIA_ACTION_STOP 3
#define MEDIA_ACTION_RESTART 4
#define MEDIA_ACTION_NEXT 5
#define MEDIA_ACTION_PREVIOUS 6
#define MEDIA_ACTION_PLAY_FROM 7
#define MEDIA_ACTION_PAUSE_AT 8

#define MUTE_ACTION_NONE 0
#define MUTE_ACTION_MUTE_START 1
#define MUTE_ACTION_UNMUTE_START 2
#define MUTE_ACTION_MUTE_END 3
#define MUTE_ACTION_UNMUTE_END 4
#define MUTE_ACTION_MUTE_DURING 5
#define MUTE_ACTION_UNMUTE_DURING 6


struct move_value_info {
	obs_source_t *source;
	char *filter_name;
	obs_source_t *filter;
	char *setting_filter_name;
	char *setting_name;

	obs_hotkey_id move_start_hotkey;

	bool custom_duration;
	uint64_t duration;
	uint64_t start_delay;
	uint64_t end_delay;
	uint32_t start_trigger;
	uint32_t stop_trigger;
	bool moving;
	float running_duration;
	char *simultaneous_move_name;
	char *next_move_name;
	bool enabled;

	long long easing;
	long long easing_function;

	long long int_to;
	long long int_from;

	double double_to;
	double double_from;

	struct vec4 color_to;
	struct vec4 color_from;

	obs_data_array_t *settings;

	long long value_type;
	DARRAY(obs_source_t *) filters_done;

	long long next_move_on;
	bool reverse;
};
