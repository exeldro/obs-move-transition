#pragma once

#define S_MATCH "match"
#define S_MOVE_MATCH "move_match"
#define S_MOVE_IN "move_in"
#define S_MOVE_OUT "move_out"

#define S_NAME_PART_MATCH "name_part_match"
#define S_NAME_NUMBER_MATCH "name_number_match"
#define S_NAME_LAST_WORD_MATCH "name_last_word_match"

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
