#include "move-transition.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <stdio.h>
#include <util/dstr.h>

struct move_source_info {
	struct move_filter move_filter;

	char *source_name;
	obs_sceneitem_t *scene_item;

	float curve;

	bool transform;
	struct vec2 pos_from;
	struct vec2 pos_to;
	float rot_from;
	float rot_to;
	struct vec2 scale_from;
	struct vec2 scale_to;
	struct vec2 bounds_from;
	struct vec2 bounds_to;
	struct obs_sceneitem_crop crop_from;
	struct obs_sceneitem_crop crop_to;
	uint32_t canvas_width;
	uint32_t canvas_height;

	long long change_visibility;
	bool visibility_toggled;

	long long change_order;
	long long order_position;

	long long media_action_start;
	int64_t media_time_start;
	long long media_action_end;
	int64_t media_time_end;

	bool audio_fade;
	float audio_fade_from;
	float audio_fade_to;
	long long mute_action;
	bool midpoint;
};

void move_source_scene_remove(void *data, calldata_t *call_data);

void move_source_item_remove(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (!move_source)
		return;
	if (!call_data)
		return;
	obs_sceneitem_t *item = calldata_ptr(call_data, "item");
	if (!item || item != move_source->scene_item)
		return;
	move_source->scene_item = NULL;
	obs_scene_t *scene = calldata_ptr(call_data, "scene");
	if (!scene)
		return;

	obs_source_t *parent = obs_scene_get_source(scene);
	if (!parent)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(parent);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "item_remove", move_source_item_remove, move_source);
	signal_handler_disconnect(sh, "remove", move_source_scene_remove, move_source);
	signal_handler_disconnect(sh, "destroy", move_source_scene_remove, move_source);
}

void move_source_scene_remove(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	obs_source_t *source = (obs_source_t *)calldata_ptr(call_data, "source");

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "item_remove", move_source_item_remove, move_source);
	signal_handler_disconnect(sh, "remove", move_source_scene_remove, move_source);
	signal_handler_disconnect(sh, "destroy", move_source_scene_remove, move_source);
}

bool find_sceneitem(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct move_source_info *move_source = data;
	const char *name = obs_source_get_name(obs_sceneitem_get_source(scene_item));
	if (!name || strcmp(name, move_source->source_name) != 0)
		return true;
	move_source->scene_item = scene_item;
	obs_source_t *parent = obs_scene_get_source(scene);
	if (!parent)
		return false;

	signal_handler_t *sh = obs_source_get_signal_handler(parent);
	if (sh) {
		signal_handler_connect(sh, "item_remove", move_source_item_remove, move_source);
		signal_handler_connect(sh, "remove", move_source_scene_remove, move_source);
		signal_handler_connect(sh, "destroy", move_source_scene_remove, move_source);
	}

	return false;
}

char obs_data_get_char(obs_data_t *data, const char *name)
{
	const char *s = obs_data_get_string(data, name);
	return (s && strlen(s)) ? s[0] : ' ';
}

void obs_data_set_char(obs_data_t *data, const char *name, char val)
{
	char s[2];
	s[0] = val;
	s[1] = 0;
	obs_data_set_string(data, name, s);
}

float calc_sign(char sign, float from, float to)
{
	if (sign == '+') {
		return from + to;
	} else if (sign == '-') {
		return from - to;
	} else if (sign == '*') {
		return from * to;
	} else if (sign == '/') {
		return to == 0.0f ? from : from / to;
	} else {
		return to;
	}
}

void calc_relative_to(struct move_source_info *move_source)
{

	obs_data_t *settings = obs_source_get_settings(move_source->move_filter.source);

	move_source->rot_to = calc_sign(obs_data_get_char(settings, "rot_sign"), move_source->rot_from,
					(float)obs_data_get_double(settings, S_ROT));

	obs_data_t *pos = obs_data_get_obj(settings, S_POS);
	move_source->pos_to.x =
		calc_sign(obs_data_get_char(pos, "x_sign"), move_source->pos_from.x, (float)obs_data_get_double(pos, "x"));

	move_source->pos_to.y =
		calc_sign(obs_data_get_char(pos, "y_sign"), move_source->pos_from.y, (float)obs_data_get_double(pos, "y"));
	obs_data_release(pos);

	obs_data_t *scale = obs_data_get_obj(settings, S_SCALE);
	move_source->scale_to.x =
		calc_sign(obs_data_get_char(scale, "x_sign"), move_source->scale_from.x, (float)obs_data_get_double(scale, "x"));

	move_source->scale_to.y =
		calc_sign(obs_data_get_char(scale, "y_sign"), move_source->scale_from.y, (float)obs_data_get_double(scale, "y"));
	obs_data_release(scale);

	obs_data_t *bounds = obs_data_get_obj(settings, S_BOUNDS);
	move_source->bounds_to.x =
		calc_sign(obs_data_get_char(bounds, "x_sign"), move_source->bounds_from.x, (float)obs_data_get_double(bounds, "x"));

	move_source->bounds_to.y =
		calc_sign(obs_data_get_char(bounds, "y_sign"), move_source->bounds_from.y, (float)obs_data_get_double(bounds, "y"));
	obs_data_release(bounds);

	obs_data_t *crop = obs_data_get_obj(settings, S_CROP);
	move_source->crop_to.left = (int)calc_sign(obs_data_get_char(crop, "left_sign"), (float)move_source->crop_from.left,
						   (float)obs_data_get_int(crop, "left"));
	move_source->crop_to.top = (int)calc_sign(obs_data_get_char(crop, "top_sign"), (float)move_source->crop_from.top,
						  (float)obs_data_get_int(crop, "top"));
	move_source->crop_to.right = (int)calc_sign(obs_data_get_char(crop, "right_sign"), (float)move_source->crop_from.right,
						    (float)obs_data_get_int(crop, "right"));
	move_source->crop_to.bottom = (int)calc_sign(obs_data_get_char(crop, "bottom_sign"), (float)move_source->crop_from.bottom,
						     (float)obs_data_get_int(crop, "bottom"));
	obs_data_release(crop);
	obs_data_release(settings);
}

void move_source_media_action(struct move_source_info *move_source, long long media_action, int64_t media_time)
{
	if (media_action == MEDIA_ACTION_PLAY) {
		const enum obs_media_state state = obs_source_media_get_state(obs_sceneitem_get_source(move_source->scene_item));
		if (state == OBS_MEDIA_STATE_PAUSED) {
			obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), false);
		} else if (state != OBS_MEDIA_STATE_PLAYING) {
			obs_source_media_restart(obs_sceneitem_get_source(move_source->scene_item));
		}
	} else if (media_action == MEDIA_ACTION_PAUSE) {
		obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), true);
	} else if (media_action == MEDIA_ACTION_STOP) {
		obs_source_media_stop(obs_sceneitem_get_source(move_source->scene_item));
	} else if (media_action == MEDIA_ACTION_RESTART) {
		obs_source_media_restart(obs_sceneitem_get_source(move_source->scene_item));
	} else if (media_action == MEDIA_ACTION_NEXT) {
		obs_source_media_next(obs_sceneitem_get_source(move_source->scene_item));
	} else if (media_action == MEDIA_ACTION_PREVIOUS) {
		obs_source_media_previous(obs_sceneitem_get_source(move_source->scene_item));
	} else if (media_action == MEDIA_ACTION_PLAY_FROM) {
		const int64_t duration = obs_source_media_get_duration(obs_sceneitem_get_source(move_source->scene_item));
		if (media_time < 0 && duration + media_time > 0) {
			const enum obs_media_state state =
				obs_source_media_get_state(obs_sceneitem_get_source(move_source->scene_item));
			if (state == OBS_MEDIA_STATE_PAUSED) {
				obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), false);
			} else if (state != OBS_MEDIA_STATE_PLAYING) {
				obs_source_media_restart(obs_sceneitem_get_source(move_source->scene_item));
			}
			obs_source_media_set_time(obs_sceneitem_get_source(move_source->scene_item), duration + media_time);
		} else if (media_time >= 0 && media_time <= duration) {
			const enum obs_media_state state =
				obs_source_media_get_state(obs_sceneitem_get_source(move_source->scene_item));
			if (state == OBS_MEDIA_STATE_PAUSED) {
				obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), false);
			} else if (state != OBS_MEDIA_STATE_PLAYING) {
				obs_source_media_restart(obs_sceneitem_get_source(move_source->scene_item));
			}
			obs_source_media_set_time(obs_sceneitem_get_source(move_source->scene_item), media_time);
		}
	} else if (media_action == MEDIA_ACTION_PAUSE_AT) {
		const int64_t duration = obs_source_media_get_duration(obs_sceneitem_get_source(move_source->scene_item));
		if (media_time < 0 && duration + media_time > 0) {
			obs_source_media_set_time(obs_sceneitem_get_source(move_source->scene_item), duration + media_time);
			obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), true);
		} else if (media_time >= 0 && media_time <= duration) {
			obs_source_media_set_time(obs_sceneitem_get_source(move_source->scene_item), media_time);
			obs_source_media_play_pause(obs_sceneitem_get_source(move_source->scene_item), true);
		}
	}
}

void move_source_ended(struct move_source_info *move_source);

void move_source_start(struct move_source_info *move_source)
{
	if (!move_source->scene_item && move_source->source_name && strlen(move_source->source_name)) {
		obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (!scene)
				scene = obs_group_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem, move_source);
		}
	}
	if (!move_source->scene_item)
		return;
	if (!move_filter_start_internal(&move_source->move_filter))
		return;
	if ((move_source->change_order & CHANGE_ORDER_START) != 0) {
		if ((move_source->change_order & CHANGE_ORDER_RELATIVE) != 0 && move_source->order_position) {
			if (move_source->order_position > 0) {
				for (int i = 0; i < move_source->order_position; i++) {
					obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_UP);
				}
			} else if (move_source->order_position < 0) {
				for (int i = 0; i > move_source->order_position; i--) {
					obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_DOWN);
				}
			}
		} else if ((move_source->change_order & CHANGE_ORDER_ABSOLUTE) != 0) {
			obs_sceneitem_set_order_position(move_source->scene_item, (int)move_source->order_position);
		}
	}
	if ((move_source->change_visibility == CHANGE_VISIBILITY_SHOW_START ||
	     move_source->change_visibility == CHANGE_VISIBILITY_SHOW_START_END ||
	     move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE) &&
	    !obs_sceneitem_visible(move_source->scene_item)) {
		obs_sceneitem_set_visible(move_source->scene_item, true);
		move_source->visibility_toggled = true;
	} else {
		move_source->visibility_toggled = false;
	}
	if (move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE_START) {
		obs_sceneitem_set_visible(move_source->scene_item, !obs_sceneitem_visible(move_source->scene_item));
	} else if (move_source->change_visibility == CHANGE_VISIBILITY_HIDE_START ||
		   move_source->change_visibility == CHANGE_VISIBILITY_HIDE_START_END) {
		obs_sceneitem_set_visible(move_source->scene_item, false);
	}
	move_source_media_action(move_source, move_source->media_action_start, move_source->media_time_start);

	if ((move_source->mute_action == MUTE_ACTION_MUTE_START || move_source->mute_action == MUTE_ACTION_MUTE_DURING) &&
	    !obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
		obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), true);
	} else if ((move_source->mute_action == MUTE_ACTION_UNMUTE_START ||
		    move_source->mute_action == MUTE_ACTION_UNMUTE_DURING) &&
		   obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
		obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), false);
	}

	if (!move_source->move_filter.reverse) {
		move_source->rot_from = obs_sceneitem_get_rot(move_source->scene_item);
		obs_sceneitem_get_pos(move_source->scene_item, &move_source->pos_from);
		obs_sceneitem_get_scale(move_source->scene_item, &move_source->scale_from);
		obs_sceneitem_get_bounds(move_source->scene_item, &move_source->bounds_from);
		obs_sceneitem_get_crop(move_source->scene_item, &move_source->crop_from);
		obs_source_t *scene_source = obs_scene_get_source(obs_sceneitem_get_scene(move_source->scene_item));
		move_source->canvas_width = obs_source_get_width(scene_source);
		move_source->canvas_height = obs_source_get_height(scene_source);

		calc_relative_to(move_source);

		move_source->audio_fade_from = obs_source_get_volume(obs_sceneitem_get_source(move_source->scene_item));
	}
	move_source->midpoint = false;
}

bool move_source_start_button(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct move_source_info *move_source = data;
	move_source_start(move_source);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

void move_source_stop(struct move_source_info *move_source)
{
	move_filter_stop(&move_source->move_filter);
}

void move_source_source_activate(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_SOURCE_ACTIVATE)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_SOURCE_ACTIVATE)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_deactivate(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_SOURCE_DEACTIVATE)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_SOURCE_DEACTIVATE)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_show(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_SOURCE_SHOW)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_SOURCE_SHOW)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_hide(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_SOURCE_HIDE)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_SOURCE_HIDE)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_media_started(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_MEDIA_STARTED)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_MEDIA_STARTED)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_media_ended(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	if (move_source->move_filter.start_trigger == START_TRIGGER_MEDIA_ENDED)
		move_source_start(move_source);
	if (move_source->move_filter.stop_trigger == START_TRIGGER_MEDIA_ENDED)
		move_source_stop(move_source);
	UNUSED_PARAMETER(call_data);
}

void move_source_source_remove(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	move_source->scene_item = NULL;
	UNUSED_PARAMETER(call_data);
}

void move_source_source_changed(struct move_source_info *move_source, const char *source_name)
{

	obs_source_t *source = move_source->source_name && strlen(move_source->source_name)
				       ? obs_get_source_by_name(move_source->source_name)
				       : NULL;
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "activate", move_source_source_activate, move_source);
			signal_handler_disconnect(sh, "deactivate", move_source_source_deactivate, move_source);
			signal_handler_disconnect(sh, "show", move_source_source_show, move_source);
			signal_handler_disconnect(sh, "hide", move_source_source_hide, move_source);
			signal_handler_disconnect(sh, "media_started", move_source_source_media_started, move_source);
			signal_handler_disconnect(sh, "media_ended", move_source_source_media_ended, move_source);
			signal_handler_disconnect(sh, "remove", move_source_source_remove, move_source);
		}
		obs_source_release(source);
	}

	bfree(move_source->source_name);
	move_source->source_name = NULL;

	source = obs_get_source_by_name(source_name);
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_connect(sh, "activate", move_source_source_activate, move_source);
			signal_handler_connect(sh, "deactivate", move_source_source_deactivate, move_source);
			signal_handler_connect(sh, "show", move_source_source_show, move_source);
			signal_handler_connect(sh, "hide", move_source_source_hide, move_source);
			signal_handler_connect(sh, "media_started", move_source_source_media_started, move_source);
			signal_handler_connect(sh, "media_ended", move_source_source_media_ended, move_source);
			signal_handler_connect(sh, "remove", move_source_source_remove, move_source);

			move_source->source_name = bstrdup(source_name);
		}
		obs_source_release(source);
	}
	move_source->scene_item = NULL;
	obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
	if (parent) {
		signal_handler_t *sh = obs_source_get_signal_handler(parent);
		if (sh) {
			signal_handler_disconnect(sh, "item_remove", move_source_item_remove, move_source);
			signal_handler_disconnect(sh, "remove", move_source_scene_remove, move_source);
			signal_handler_disconnect(sh, "destroy", move_source_scene_remove, move_source);
		}
	}
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene)
		scene = obs_group_from_source(parent);
	if (move_source->source_name && scene)
		obs_scene_enum_items(scene, find_sceneitem, move_source);
}

static void obs_data_set_sign(obs_data_t *settings, const char *name, const char *val)
{
	obs_data_t *obj = obs_data_get_obj(settings, name);
	if (obj) {
		obs_data_set_string(obj, "x_sign", val);
		obs_data_set_string(obj, "y_sign", val);
		obs_data_release(obj);
	}
}

void move_source_update(void *data, obs_data_t *settings)
{
	struct move_source_info *move_source = data;

	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (!move_source->source_name || strcmp(move_source->source_name, source_name) != 0) {
		move_source_source_changed(move_source, source_name);
	}
	move_filter_update(&move_source->move_filter, settings);

	move_source->change_visibility = obs_data_get_int(settings, S_CHANGE_VISIBILITY);
	move_source->curve = (float)obs_data_get_double(settings, S_CURVE_MATCH);

	move_source->transform = obs_data_get_bool(settings, S_TRANSFORM);
	if (obs_data_has_user_value(settings, "crop_left") || obs_data_has_user_value(settings, "crop_top") ||
	    obs_data_has_user_value(settings, "crop_right") || obs_data_has_user_value(settings, "crop_bottom")) {
		obs_data_t *obj = obs_data_get_obj(settings, S_CROP);
		if (!obj) {
			obj = obs_data_create();
			obs_data_set_obj(settings, S_CROP, obj);
		}
		obs_data_set_int(obj, "left", obs_data_get_int(settings, "crop_left"));
		obs_data_set_int(obj, "top", obs_data_get_int(settings, "crop_top"));
		obs_data_set_int(obj, "right", obs_data_get_int(settings, "crop_right"));
		obs_data_set_int(obj, "bottom", obs_data_get_int(settings, "crop_bottom"));
		obs_data_release(obj);
		obs_data_unset_user_value(settings, "crop_left");
		obs_data_unset_user_value(settings, "crop_top");
		obs_data_unset_user_value(settings, "crop_right");
		obs_data_unset_user_value(settings, "crop_bottom");
	}
	if (obs_data_has_user_value(settings, S_TRANSFORM_RELATIVE)) {
		if (obs_data_get_bool(settings, S_TRANSFORM_RELATIVE)) {
			obs_data_set_sign(settings, S_POS, "+");
			obs_data_set_sign(settings, S_SCALE, "+");
			obs_data_set_sign(settings, S_BOUNDS, "+");
			obs_data_set_string(settings, "rot_sign", "+");
			obs_data_t *obj = obs_data_get_obj(settings, S_CROP);
			if (obj) {
				obs_data_set_string(obj, "left_sign", "+");
				obs_data_set_string(obj, "top_sign", "+");
				obs_data_set_string(obj, "right_sign", "+");
				obs_data_set_string(obj, "bottom_sign", "+");
				obs_data_release(obj);
			}
		}
		obs_data_unset_user_value(settings, S_TRANSFORM_RELATIVE);
	}
	calc_relative_to(move_source);

	move_source->change_order = obs_data_get_int(settings, S_CHANGE_ORDER);
	move_source->order_position = obs_data_get_int(settings, S_ORDER_POSITION);

	move_source->media_action_start = obs_data_get_int(settings, S_MEDIA_ACTION_START);
	move_source->media_time_start = obs_data_get_int(settings, S_MEDIA_ACTION_START_TIME);
	move_source->media_action_end = obs_data_get_int(settings, S_MEDIA_ACTION_END);
	move_source->media_time_end = obs_data_get_int(settings, S_MEDIA_ACTION_END_TIME);

	move_source->mute_action = obs_data_get_int(settings, S_MUTE_ACTION);
	move_source->audio_fade = obs_data_get_bool(settings, S_AUDIO_FADE);
	move_source->audio_fade_to = (float)obs_data_get_double(settings, S_AUDIO_FADE_PERCENT) / 100.0f;
	if (move_source->move_filter.start_trigger == START_TRIGGER_LOAD) {
		move_source_start(move_source);
	}
}

void update_transform_text(struct move_source_info *move_source, obs_data_t *settings)
{
	obs_data_t *pos = obs_data_get_obj(settings, S_POS);
	obs_data_t *scale = obs_data_get_obj(settings, S_SCALE);
	obs_data_t *bounds = obs_data_get_obj(settings, S_BOUNDS);
	obs_data_t *crop = obs_data_get_obj(settings, S_CROP);

	char transform_text[500];
	if (move_source->scene_item) {
		if (obs_sceneitem_get_bounds_type(move_source->scene_item) == OBS_BOUNDS_NONE) {
			snprintf(transform_text, 500,
				 "pos: x%c%.1f y%c%.1f rot:%c%.1f scale: x%c%.3f y%c%.3f crop: l%c%d t%c%d r%c%d b%c%d",
				 obs_data_get_char(pos, "x_sign"), obs_data_get_double(pos, "x"), obs_data_get_char(pos, "y_sign"),
				 obs_data_get_double(pos, "y"), obs_data_get_char(settings, "rot_sign"),
				 obs_data_get_double(settings, S_ROT), obs_data_get_char(scale, "x_sign"),
				 obs_data_get_double(scale, "x"), obs_data_get_char(scale, "y_sign"),
				 obs_data_get_double(scale, "y"), obs_data_get_char(crop, "left_sign"),
				 (int)obs_data_get_int(crop, "left"), obs_data_get_char(crop, "top_sign"),
				 (int)obs_data_get_int(crop, "top"), obs_data_get_char(crop, "right_sign"),
				 (int)obs_data_get_int(crop, "right"), obs_data_get_char(crop, "bottom_sign"),
				 (int)obs_data_get_int(crop, "bottom"));
		} else {
			snprintf(transform_text, 500,
				 "pos: x%c%.1f y%c%.1f rot:%c%.1f bounds: x%c%.3f y%c%.3f crop: l%c%d t%c%d r%c%d b%c%d",
				 obs_data_get_char(pos, "x_sign"), obs_data_get_double(pos, "x"), obs_data_get_char(pos, "y_sign"),
				 obs_data_get_double(pos, "y"), obs_data_get_char(settings, "rot_sign"),
				 obs_data_get_double(settings, S_ROT), obs_data_get_char(bounds, "x_sign"),
				 obs_data_get_double(bounds, "x"), obs_data_get_char(bounds, "y_sign"),
				 obs_data_get_double(bounds, "y"), obs_data_get_char(crop, "left_sign"),
				 (int)obs_data_get_int(crop, "left"), obs_data_get_char(crop, "top_sign"),
				 (int)obs_data_get_int(crop, "top"), obs_data_get_char(crop, "right_sign"),
				 (int)obs_data_get_int(crop, "right"), obs_data_get_char(crop, "bottom_sign"),
				 (int)obs_data_get_int(crop, "bottom"));
		}
	} else {
		snprintf(
			transform_text, 500,
			"pos: x%c%.1f y%c%.1f rot:%c%.1f scale: x%c%.3f y%c%.3f bounds: x%c%.3f y%c%.3f crop: l%c%d t%c%d r%c%d b%c%d",
			obs_data_get_char(pos, "x_sign"), obs_data_get_double(pos, "x"), obs_data_get_char(pos, "y_sign"),
			obs_data_get_double(pos, "y"), obs_data_get_char(settings, "rot_sign"),
			obs_data_get_double(settings, S_ROT), obs_data_get_char(scale, "x_sign"), obs_data_get_double(scale, "x"),
			obs_data_get_char(scale, "y_sign"), obs_data_get_double(scale, "y"), obs_data_get_char(bounds, "x_sign"),
			obs_data_get_double(bounds, "x"), obs_data_get_char(bounds, "y_sign"), obs_data_get_double(bounds, "y"),
			obs_data_get_char(crop, "left_sign"), (int)obs_data_get_int(crop, "left"),
			obs_data_get_char(crop, "top_sign"), (int)obs_data_get_int(crop, "top"),
			obs_data_get_char(crop, "right_sign"), (int)obs_data_get_int(crop, "right"),
			obs_data_get_char(crop, "bottom_sign"), (int)obs_data_get_int(crop, "bottom"));
	}
	obs_data_set_string(settings, S_TRANSFORM_TEXT, transform_text);

	obs_data_release(pos);
	obs_data_release(scale);
	obs_data_release(bounds);
	obs_data_release(crop);
}

void move_source_load(void *data, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	move_source_update(move_source, settings);
	update_transform_text(move_source, settings);
}

void move_source_source_rename(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	const char *new_name = calldata_string(call_data, "new_name");
	const char *prev_name = calldata_string(call_data, "prev_name");
	obs_data_t *settings = obs_source_get_settings(move_source->move_filter.source);
	if (!settings || !new_name || !prev_name)
		return;
	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (source_name && strlen(source_name) && strcmp(source_name, prev_name) == 0) {
		obs_data_set_string(settings, S_SOURCE, new_name);
	}
	obs_data_release(settings);
}

obs_source_t *move_source_get_source(void *data, const char *name)
{
	struct move_source_info *move_source = data;
	obs_source_t *source = obs_sceneitem_get_source(move_source->scene_item);
	if (!source)
		return NULL;
	return obs_source_get_filter_by_name(source, name);
}

static void *move_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_source_info *move_source = bzalloc(sizeof(struct move_source_info));
	move_filter_init(&move_source->move_filter, source, (void (*)(void *))move_source_start);
	move_source->move_filter.get_alternative_filter = move_source_get_source;
	obs_source_update(source, settings);
	signal_handler_connect(obs_get_signal_handler(), "source_rename", move_source_source_rename, move_source);

	return move_source;
}

static void move_source_destroy(void *data)
{
	struct move_source_info *move_source = data;
	signal_handler_disconnect(obs_get_signal_handler(), "source_rename", move_source_source_rename, move_source);

	obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
	if (parent) {
		signal_handler_t *sh = obs_source_get_signal_handler(parent);
		signal_handler_disconnect(sh, "item_remove", move_source_item_remove, move_source);
		signal_handler_disconnect(sh, "remove", move_source_scene_remove, move_source);
		signal_handler_disconnect(sh, "destroy", move_source_scene_remove, move_source);
	}

	obs_source_t *source = NULL;
	if (move_source->scene_item) {
		source = obs_source_get_ref(obs_sceneitem_get_source(move_source->scene_item));
	}
	if (!source && move_source->source_name && strlen(move_source->source_name)) {
		source = obs_get_source_by_name(move_source->source_name);
	}
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "activate", move_source_source_activate, data);
			signal_handler_disconnect(sh, "deactivate", move_source_source_deactivate, data);
			signal_handler_disconnect(sh, "show", move_source_source_show, data);
			signal_handler_disconnect(sh, "hide", move_source_source_hide, data);
			signal_handler_disconnect(sh, "media_started", move_source_source_media_started, data);
			signal_handler_disconnect(sh, "media_ended", move_source_source_media_ended, data);
			signal_handler_disconnect(sh, "remove", move_source_source_remove, data);
		}
		obs_source_release(source);
	}
	move_source->scene_item = NULL;
	move_filter_destroy(&move_source->move_filter);
	bfree(move_source->source_name);
	bfree(move_source);
}

static void obs_data_set_vec2_sign(obs_data_t *data, const char *name, const struct vec2 *val, char x_sign, char y_sign)
{
	obs_data_t *obj = obs_data_create();
	obs_data_set_double(obj, "x", val->x);
	obs_data_set_char(obj, "x_sign", x_sign);
	obs_data_set_double(obj, "y", val->y);
	obs_data_set_char(obj, "y_sign", y_sign);
	obs_data_set_obj(data, name, obj);
	obs_data_release(obj);
}

static void obs_data_set_crop_sign(obs_data_t *settings, const char *name, struct obs_sceneitem_crop *crop, char crop_left_sign,
				   char crop_top_sign, char crop_right_sign, char crop_bottom_sign)
{
	obs_data_t *obj = obs_data_create();
	obs_data_set_double(obj, "left", crop->left);
	obs_data_set_char(obj, "left_sign", crop_left_sign);
	obs_data_set_double(obj, "top", crop->top);
	obs_data_set_char(obj, "top_sign", crop_top_sign);
	obs_data_set_double(obj, "right", crop->right);
	obs_data_set_char(obj, "right_sign", crop_right_sign);
	obs_data_set_double(obj, "bottom", crop->bottom);
	obs_data_set_char(obj, "bottom_sign", crop_bottom_sign);
	obs_data_set_obj(settings, name, obj);
	obs_data_release(obj);
}

bool move_source_get_transform(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_source_info *move_source = data;
	bool settings_changed = false;
	if (!move_source->scene_item && move_source->source_name && strlen(move_source->source_name)) {
		obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (!scene)
				scene = obs_group_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem, data);
		}
	}
	if (!move_source->scene_item)
		return settings_changed;
	settings_changed = true;
	obs_data_t *settings = obs_source_get_settings(move_source->move_filter.source);
	struct vec2 pos;
	obs_sceneitem_get_pos(move_source->scene_item, &pos);
	struct vec2 scale;
	obs_sceneitem_get_scale(move_source->scene_item, &scale);
	struct vec2 bounds;
	obs_sceneitem_get_bounds(move_source->scene_item, &bounds);
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(move_source->scene_item, &crop);
	obs_data_set_double(settings, S_ROT, obs_sceneitem_get_rot(move_source->scene_item));
	obs_data_set_char(settings, "rot_sign", ' ');
	obs_data_set_vec2_sign(settings, S_POS, &pos, ' ', ' ');
	obs_data_set_vec2_sign(settings, S_SCALE, &scale, ' ', ' ');
	obs_data_set_vec2_sign(settings, S_BOUNDS, &bounds, ' ', ' ');
	obs_data_set_crop_sign(settings, S_CROP, &crop, ' ', ' ', ' ', ' ');

	move_source_update(data, settings);
	update_transform_text(move_source, settings);
	obs_data_release(settings);

	return settings_changed;
}

bool move_source_relative(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_source_info *move_source = data;
	bool settings_changed = true;
	obs_data_t *settings = obs_source_get_settings(move_source->move_filter.source);
	struct vec2 pos;
	pos.x = 0.0f;
	pos.y = 0.0f;
	struct vec2 scale;
	scale.x = 1.0f;
	scale.y = 1.0f;
	struct vec2 bounds;
	bounds.x = 1.0f;
	bounds.y = 1.0f;
	struct obs_sceneitem_crop crop = {0, 0, 0, 0};

	obs_data_set_double(settings, S_ROT, 0.0);
	obs_data_set_char(settings, "rot_sign", '+');
	obs_data_set_vec2_sign(settings, S_POS, &pos, '+', '+');
	obs_data_set_vec2_sign(settings, S_SCALE, &scale, '*', '*');
	obs_data_set_vec2_sign(settings, S_BOUNDS, &bounds, '*', '*');
	obs_data_set_crop_sign(settings, S_CROP, &crop, '+', '+', '+', '+');
	update_transform_text(move_source, settings);
	move_source_update(data, settings);
	obs_data_release(settings);

	return settings_changed;
}

void prop_list_add_move_source_filter(obs_source_t *parent, obs_source_t *child, void *data)
{
	UNUSED_PARAMETER(parent);
	if (!is_move_filter(obs_source_get_unversioned_id(child)))
		return;
	obs_property_t *p = data;
	const char *name = obs_source_get_name(child);
	obs_property_list_add_string(p, name, name);
}

bool move_source_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	bool refresh = false;

	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (move_source->source_name && strcmp(move_source->source_name, source_name) == 0)
		return refresh;
	move_source_source_changed(move_source, source_name);
	obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
	obs_property_t *p = obs_properties_get(props, S_SIMULTANEOUS_MOVE);
	if (p) {
		obs_property_list_clear(p);
		obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
		obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
		obs_source_t *source = obs_sceneitem_get_source(move_source->scene_item);
		if (source)
			obs_source_enum_filters(source, prop_list_add_move_source_filter, p);
	}
	p = obs_properties_get(props, S_NEXT_MOVE);
	if (p) {
		obs_property_list_clear(p);
		obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
		obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
		obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
		obs_source_t *source = obs_sceneitem_get_source(move_source->scene_item);
		if (source)
			obs_source_enum_filters(source, prop_list_add_move_source_filter, p);
	}

	obs_source_t *source = obs_get_source_by_name(source_name);
	if (source) {
		uint32_t flags = obs_source_get_output_flags(source);
		const bool media = flags & OBS_SOURCE_CONTROLLABLE_MEDIA;
		p = obs_properties_get(props, S_MEDIA_ACTION);
		obs_property_set_visible(p, media);
		p = obs_properties_get(props, S_AUDIO_ACTION);
		const bool audio = flags & OBS_SOURCE_AUDIO;
		obs_property_set_visible(p, audio);
		obs_source_release(source);
	} else {
		p = obs_properties_get(props, S_MEDIA_ACTION);
		obs_property_set_visible(p, false);
		p = obs_properties_get(props, S_AUDIO_ACTION);
		obs_property_set_visible(p, false);
	}
	refresh = move_source_get_transform(props, property, data);
	return refresh;
}

bool prop_list_add_sceneitem(obs_scene_t *scene, obs_sceneitem_t *item, void *data);

bool move_source_transform_text_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_source_info *move_source = data;
	const char *transform_text = obs_data_get_string(settings, S_TRANSFORM_TEXT);
	struct vec2 pos;
	float rot;
	struct vec2 scale;
	struct vec2 bounds;
	struct obs_sceneitem_crop crop;
	char pos_x_sign, pos_y_sign, rot_sign, scale_x_sign, scale_y_sign, bounds_x_sign, bounds_y_sign, crop_left_sign,
		crop_top_sign, crop_right_sign, crop_bottom_sign;

	if (move_source->scene_item) {
		if (obs_sceneitem_get_bounds_type(move_source->scene_item) == OBS_BOUNDS_NONE) {
			if (sscanf(transform_text, "pos: x%c%f y%c%f rot:%c%f scale: x%c%f y%c%f crop: l%c%d t%c%d r%c%d b%c%d",
				   &pos_x_sign, &pos.x, &pos_y_sign, &pos.y, &rot_sign, &rot, &scale_x_sign, &scale.x,
				   &scale_y_sign, &scale.y, &crop_left_sign, &crop.left, &crop_top_sign, &crop.top,
				   &crop_right_sign, &crop.right, &crop_bottom_sign, &crop.bottom) != 18) {
				update_transform_text(move_source, settings);
				return true;
			}
			obs_data_set_vec2_sign(settings, S_SCALE, &scale, scale_x_sign, scale_y_sign);
		} else {
			if (sscanf(transform_text, "pos: x%c%f y%c%f rot:%c%f bounds: x%c%f y%c%f crop: l%c%d t%c%d r%c%d b%c%d",
				   &pos_x_sign, &pos.x, &pos_y_sign, &pos.y, &rot_sign, &rot, &bounds_x_sign, &bounds.x,
				   &bounds_y_sign, &bounds.y, &crop_left_sign, &crop.left, &crop_top_sign, &crop.top,
				   &crop_right_sign, &crop.right, &crop_bottom_sign, &crop.bottom) != 18) {
				update_transform_text(move_source, settings);
				return true;
			}
			obs_data_set_vec2_sign(settings, S_BOUNDS, &bounds, bounds_x_sign, bounds_y_sign);
		}
	} else {
		if (sscanf(transform_text,
			   "pos: x%c%f y%c%f rot:%c%f scale: x%c%f y%c%f bounds: x%c%f y%c%f crop: l%c%d t%c%d r%c%d b%c%d",
			   &pos_x_sign, &pos.x, &pos_y_sign, &pos.y, &rot_sign, &rot, &scale_x_sign, &scale.x, &scale_y_sign,
			   &scale.y, &bounds_x_sign, &bounds.x, &bounds_y_sign, &bounds.y, &crop_left_sign, &crop.left,
			   &crop_top_sign, &crop.top, &crop_right_sign, &crop.right, &crop_bottom_sign, &crop.bottom) != 22) {
			update_transform_text(move_source, settings);
			return true;
		}
		obs_data_set_vec2_sign(settings, S_SCALE, &scale, scale_x_sign, scale_y_sign);
		obs_data_set_vec2_sign(settings, S_BOUNDS, &bounds, bounds_x_sign, bounds_y_sign);
	}
	obs_data_set_vec2_sign(settings, S_POS, &pos, pos_x_sign, pos_y_sign);

	obs_data_set_double(settings, S_ROT, rot);
	obs_data_set_char(settings, "rot_sign", rot_sign);

	obs_data_set_crop_sign(settings, S_CROP, &crop, crop_left_sign, crop_top_sign, crop_right_sign, crop_bottom_sign);

	return false;
}

static void prop_list_add_media_actions(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("MediaAction.None"), MEDIA_ACTION_NONE);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Play"), MEDIA_ACTION_PLAY);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Pause"), MEDIA_ACTION_PAUSE);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Stop"), MEDIA_ACTION_STOP);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Restart"), MEDIA_ACTION_RESTART);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Next"), MEDIA_ACTION_NEXT);
	obs_property_list_add_int(p, obs_module_text("MediaAction.Previous"), MEDIA_ACTION_PREVIOUS);
	obs_property_list_add_int(p, obs_module_text("MediaAction.PlayFrom"), MEDIA_ACTION_PLAY_FROM);
	obs_property_list_add_int(p, obs_module_text("MediaAction.PauseAt"), MEDIA_ACTION_PAUSE_AT);
}

static obs_properties_t *move_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct move_source_info *move_source = data;
	obs_source_t *parent = obs_filter_get_parent(move_source->move_filter.source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene)
		scene = obs_group_from_source(parent);
	if (!scene) {
		obs_property_t *w = obs_properties_add_text(ppts, "warning", obs_module_text("ScenesOnlyFilter"), OBS_TEXT_INFO);
		obs_property_text_set_info_type(w, OBS_TEXT_INFO_WARNING);
		obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
		return ppts;
	}
	if (!move_source->scene_item && move_source->source_name && strlen(move_source->source_name)) {
		obs_scene_enum_items(scene, find_sceneitem, move_source);
	}
	obs_properties_t *group = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(group, S_SOURCE, obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_scene_enum_items(scene, prop_list_add_sceneitem, p);
	obs_property_set_modified_callback2(p, move_source_changed, data);

	p = obs_properties_add_int(group, S_START_DELAY, obs_module_text("StartDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");
	obs_properties_t *duration = obs_properties_create();
	p = obs_properties_add_int(duration, S_DURATION, "", 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");
	p = obs_properties_add_group(group, S_CUSTOM_DURATION, obs_module_text("CustomDuration"), OBS_GROUP_CHECKABLE, duration);

	p = obs_properties_add_int(group, S_END_DELAY, obs_module_text("EndDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(group, S_EASING_MATCH, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_MATCH, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_group(ppts, S_GENERAL, obs_module_text("General"), OBS_GROUP_NORMAL, group);

	group = obs_properties_create();

	p = obs_properties_add_text(group, S_TRANSFORM_TEXT, obs_module_text("Transform"), OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback2(p, move_source_transform_text_changed, data);
	obs_properties_add_button(group, "transform_get", obs_module_text("GetTransform"), move_source_get_transform);
	obs_properties_add_button(group, "switch_to_relative", obs_module_text("TransformRelative"), move_source_relative);

	obs_properties_add_float_slider(group, S_CURVE_MATCH, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	p = obs_properties_add_group(ppts, S_TRANSFORM, obs_module_text("Transform"), OBS_GROUP_CHECKABLE, group);

	group = obs_properties_create();

	p = obs_properties_add_list(group, S_CHANGE_VISIBILITY, obs_module_text("ChangeVisibility"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.No"), CHANGE_VISIBILITY_NONE);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ShowStart"), CHANGE_VISIBILITY_SHOW_START);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ShowEnd"), CHANGE_VISIBILITY_SHOW_END);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ShowStartEnd"), CHANGE_VISIBILITY_SHOW_START_END);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.HideStart"), CHANGE_VISIBILITY_HIDE_START);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.HideEnd"), CHANGE_VISIBILITY_HIDE_END);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.HideStartEnd"), CHANGE_VISIBILITY_HIDE_START_END);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.Toggle"), CHANGE_VISIBILITY_TOGGLE);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ToggleStart"), CHANGE_VISIBILITY_TOGGLE_START);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ToggleEnd"), CHANGE_VISIBILITY_TOGGLE_END);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ShowMidpoint"), CHANGE_VISIBILITY_SHOW_MIDPOINT);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.HideMidpoint"), CHANGE_VISIBILITY_HIDE_MIDPOINT);
	obs_property_list_add_int(p, obs_module_text("ChangeVisibility.ToggleMidpoint"), CHANGE_VISIBILITY_TOGGLE_MIDPOINT);

	p = obs_properties_add_list(group, S_CHANGE_ORDER, obs_module_text("ChangeOrder"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.No"), CHANGE_ORDER_NONE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.StartAbsolute"), CHANGE_ORDER_START | CHANGE_ORDER_ABSOLUTE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.MidpointAbsolute"),
				  CHANGE_ORDER_MIDPOINT | CHANGE_ORDER_ABSOLUTE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.EndAbsolute"), CHANGE_ORDER_END | CHANGE_ORDER_ABSOLUTE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.StartRelative"), CHANGE_ORDER_START | CHANGE_ORDER_RELATIVE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.MidpointRelative"),
				  CHANGE_ORDER_MIDPOINT | CHANGE_ORDER_RELATIVE);
	obs_property_list_add_int(p, obs_module_text("ChangeOrder.EndRelative"), CHANGE_ORDER_END | CHANGE_ORDER_RELATIVE);
	p = obs_properties_add_int(group, S_ORDER_POSITION, obs_module_text("OrderPosition"), -1000, 1000, 1);

	p = obs_properties_add_group(ppts, S_VISIBILITY_ORDER, obs_module_text("VisibilityOrder"), OBS_GROUP_NORMAL, group);

	obs_source_t *source = obs_sceneitem_get_source(move_source->scene_item);

	group = obs_properties_create();

	const uint32_t flags = source ? obs_source_get_output_flags(source) : 0;
	const bool media = flags & OBS_SOURCE_CONTROLLABLE_MEDIA;

	p = obs_properties_add_list(group, S_MEDIA_ACTION_START, obs_module_text("MediaAction.Start"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_media_actions(p);

	p = obs_properties_add_int(group, S_MEDIA_ACTION_START_TIME, obs_module_text("MediaAction.Time"), -1000000, 1000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(group, S_MEDIA_ACTION_END, obs_module_text("MediaAction.End"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_media_actions(p);

	p = obs_properties_add_int(group, S_MEDIA_ACTION_END_TIME, obs_module_text("MediaAction.Time"), -1000000, 1000000, 100);
	obs_property_int_set_suffix(p, "ms");
	p = obs_properties_add_group(ppts, S_MEDIA_ACTION, obs_module_text("MediaAction"), OBS_GROUP_NORMAL, group);
	obs_property_set_visible(p, media);

	const bool audio = flags & OBS_SOURCE_AUDIO;

	group = obs_properties_create();

	p = obs_properties_add_list(group, S_MUTE_ACTION, obs_module_text("MuteAction"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("MuteAction.None"), MUTE_ACTION_NONE);
	obs_property_list_add_int(p, obs_module_text("MuteAction.MuteStart"), MUTE_ACTION_MUTE_START);
	obs_property_list_add_int(p, obs_module_text("MuteAction.MuteMidpoint"), MUTE_ACTION_MUTE_MIDPOINT);
	obs_property_list_add_int(p, obs_module_text("MuteAction.MuteEnd"), MUTE_ACTION_MUTE_END);
	obs_property_list_add_int(p, obs_module_text("MuteAction.UnmuteStart"), MUTE_ACTION_UNMUTE_START);
	obs_property_list_add_int(p, obs_module_text("MuteAction.UnmuteMidpoint"), MUTE_ACTION_UNMUTE_MIDPOINT);
	obs_property_list_add_int(p, obs_module_text("MuteAction.UnmuteEnd"), MUTE_ACTION_UNMUTE_END);
	obs_property_list_add_int(p, obs_module_text("MuteAction.MuteDuring"), MUTE_ACTION_MUTE_DURING);
	obs_property_list_add_int(p, obs_module_text("MuteAction.UnmuteDuring"), MUTE_ACTION_UNMUTE_DURING);

	obs_properties_t *fade = obs_properties_create();
	p = obs_properties_add_float_slider(fade, S_AUDIO_FADE_PERCENT, "", 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, "%");
	p = obs_properties_add_group(group, S_AUDIO_FADE, obs_module_text("AudioFade"), OBS_GROUP_CHECKABLE, fade);
	p = obs_properties_add_group(ppts, S_AUDIO_ACTION, obs_module_text("AudioAction"), OBS_GROUP_NORMAL, group);
	obs_property_set_visible(p, audio);

	group = obs_properties_create();

	p = obs_properties_add_bool(group, S_ENABLED_MATCH_MOVING, obs_module_text("EnabledMatchMoving"));

	p = obs_properties_add_list(group, S_START_TRIGGER, obs_module_text("StartTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceActivate"), START_TRIGGER_SOURCE_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceDeactivate"), START_TRIGGER_SOURCE_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceShow"), START_TRIGGER_SOURCE_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceHide"), START_TRIGGER_SOURCE_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaStarted"), START_TRIGGER_MEDIA_STARTED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaEnded"), START_TRIGGER_MEDIA_ENDED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Load"), START_TRIGGER_LOAD);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);

	obs_property_set_modified_callback2(p, move_filter_start_trigger_changed, data);

	obs_properties_add_int(ppts, S_START_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_START_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(group, S_STOP_TRIGGER, obs_module_text("StopTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StopTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceActivate"), START_TRIGGER_SOURCE_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceDeactivate"), START_TRIGGER_SOURCE_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceShow"), START_TRIGGER_SOURCE_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceHide"), START_TRIGGER_SOURCE_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaStarted"), START_TRIGGER_MEDIA_STARTED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaEnded"), START_TRIGGER_MEDIA_ENDED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);
	obs_property_set_modified_callback2(p, move_filter_stop_trigger_changed, data);

	obs_properties_add_int(ppts, S_STOP_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_STOP_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(group, S_SIMULTANEOUS_MOVE, obs_module_text("SimultaneousMove"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
	obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
	if (source)
		obs_source_enum_filters(source, prop_list_add_move_source_filter, p);

	p = obs_properties_add_list(group, S_NEXT_MOVE, obs_module_text("NextMove"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
	obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
	if (source)
		obs_source_enum_filters(source, prop_list_add_move_source_filter, p);

	p = obs_properties_add_list(group, S_NEXT_MOVE_ON, obs_module_text("NextMoveOn"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"), NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"), NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(group, "move_source_start", obs_module_text("Start"), move_source_start_button);

	p = obs_properties_add_group(ppts, S_ACTIONS, obs_module_text("Actions"), OBS_GROUP_NORMAL, group);
	obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
	return ppts;
}

void move_source_defaults(obs_data_t *settings)
{
	move_filter_defaults(settings);
	obs_data_set_default_bool(settings, S_CUSTOM_DURATION, true);
	obs_data_set_default_bool(settings, S_TRANSFORM, true);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH, EASING_CUBIC);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
}

void move_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct move_source_info *filter = data;
	obs_source_skip_video_filter(filter->move_filter.source);
}

static const char *move_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveSourceFilter");
}

void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control, struct vec2 *end, const float t);

void move_source_ended(struct move_source_info *move_source)
{
	move_filter_ended(&move_source->move_filter);
	if (move_source->change_visibility == CHANGE_VISIBILITY_HIDE_END ||
	    move_source->change_visibility == CHANGE_VISIBILITY_SHOW_START_END) {
		obs_sceneitem_set_visible(move_source->scene_item, false);
	} else if (move_source->change_visibility == CHANGE_VISIBILITY_SHOW_END ||
		   move_source->change_visibility == CHANGE_VISIBILITY_HIDE_START_END) {
		obs_sceneitem_set_visible(move_source->scene_item, true);
	} else if (move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE_END) {
		obs_sceneitem_set_visible(move_source->scene_item, !obs_sceneitem_visible(move_source->scene_item));
	} else if (move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE && !move_source->visibility_toggled) {
		obs_sceneitem_set_visible(move_source->scene_item, false);
	}
	move_source_media_action(move_source, move_source->media_action_end, move_source->media_time_end);
	if ((move_source->mute_action == MUTE_ACTION_MUTE_END || move_source->mute_action == MUTE_ACTION_UNMUTE_DURING) &&
	    !obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
		obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), true);
	} else if ((move_source->mute_action == MUTE_ACTION_UNMUTE_END || move_source->mute_action == MUTE_ACTION_MUTE_DURING) &&
		   obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
		obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), false);
	}

	if ((move_source->change_order & CHANGE_ORDER_END) != 0) {
		if ((move_source->change_order & CHANGE_ORDER_RELATIVE) != 0 && move_source->order_position) {
			if (move_source->order_position > 0) {
				for (int i = 0; i < move_source->order_position; i++) {
					obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_UP);
				}
			} else if (move_source->order_position < 0) {
				for (int i = 0; i > move_source->order_position; i--) {
					obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_DOWN);
				}
			}
		} else if ((move_source->change_order & CHANGE_ORDER_ABSOLUTE) != 0) {
			obs_sceneitem_set_order_position(move_source->scene_item, (int)move_source->order_position);
		}
	}
}

void move_source_tick(void *data, float seconds)
{
	struct move_source_info *move_source = data;
	float t;
	if (!move_filter_tick(&move_source->move_filter, seconds, &t))
		return;

	if (!move_source->scene_item) {
		move_source->move_filter.moving = false;
		return;
	}

	if (!move_source->midpoint && t >= 0.5) {
		move_source->midpoint = true;
		if (move_source->change_visibility == CHANGE_VISIBILITY_SHOW_MIDPOINT) {
			obs_sceneitem_set_visible(move_source->scene_item, true);
		} else if (move_source->change_visibility == CHANGE_VISIBILITY_HIDE_MIDPOINT) {
			obs_sceneitem_set_visible(move_source->scene_item, false);
		} else if (move_source->change_visibility == CHANGE_VISIBILITY_TOGGLE_MIDPOINT) {
			obs_sceneitem_set_visible(move_source->scene_item, !obs_sceneitem_visible(move_source->scene_item));
		}
		if ((move_source->change_order & CHANGE_ORDER_MIDPOINT) != 0) {
			if ((move_source->change_order & CHANGE_ORDER_RELATIVE) != 0 && move_source->order_position) {
				if (move_source->order_position > 0) {
					for (int i = 0; i < move_source->order_position; i++) {
						obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_UP);
					}
				} else if (move_source->order_position < 0) {
					for (int i = 0; i > move_source->order_position; i--) {
						obs_sceneitem_set_order(move_source->scene_item, OBS_ORDER_MOVE_DOWN);
					}
				}
			} else if ((move_source->change_order & CHANGE_ORDER_ABSOLUTE) != 0) {
				obs_sceneitem_set_order_position(move_source->scene_item, (int)move_source->order_position);
			}
		}
		if (move_source->mute_action == MUTE_ACTION_MUTE_MIDPOINT &&
		    !obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
			obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), true);
		} else if (move_source->mute_action == MUTE_ACTION_UNMUTE_MIDPOINT &&
			   obs_source_muted(obs_sceneitem_get_source(move_source->scene_item))) {
			obs_source_set_muted(obs_sceneitem_get_source(move_source->scene_item), false);
		}
	}

	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	if (move_source->audio_fade) {
		obs_source_set_volume(obs_sceneitem_get_source(move_source->scene_item),
				      (1.0f - ot) * move_source->audio_fade_from + ot * move_source->audio_fade_to);
	}
	if (move_source->transform) {
		struct vec2 pos;
		if (move_source->curve != 0.0f) {
			const float diff_x = fabsf(move_source->pos_from.x - move_source->pos_to.x);
			const float diff_y = fabsf(move_source->pos_from.y - move_source->pos_to.y);
			struct vec2 control_pos;
			vec2_set(&control_pos, 0.5f * move_source->pos_from.x + 0.5f * move_source->pos_to.x,
				 0.5f * move_source->pos_from.y + 0.5f * move_source->pos_to.y);
			if (control_pos.x >= (move_source->canvas_width >> 1)) {
				control_pos.x += diff_y * move_source->curve;
			} else {
				control_pos.x -= diff_y * move_source->curve;
			}
			if (control_pos.y >= (move_source->canvas_height >> 1)) {
				control_pos.y += diff_x * move_source->curve;
			} else {
				control_pos.y -= diff_x * move_source->curve;
			}
			vec2_bezier(&pos, &move_source->pos_from, &control_pos, &move_source->pos_to, t);
		} else {
			vec2_set(&pos, (1.0f - t) * move_source->pos_from.x + t * move_source->pos_to.x,
				 (1.0f - t) * move_source->pos_from.y + t * move_source->pos_to.y);
		}
		obs_sceneitem_defer_update_begin(move_source->scene_item);
		obs_sceneitem_set_pos(move_source->scene_item, &pos);
		const float rot = (1.0f - t) * move_source->rot_from + t * move_source->rot_to;
		obs_sceneitem_set_rot(move_source->scene_item, rot);
		struct vec2 scale;
		vec2_set(&scale, (1.0f - t) * move_source->scale_from.x + t * move_source->scale_to.x,
			 (1.0f - t) * move_source->scale_from.y + t * move_source->scale_to.y);
		obs_sceneitem_set_scale(move_source->scene_item, &scale);
		struct vec2 bounds;
		vec2_set(&bounds, (1.0f - t) * move_source->bounds_from.x + t * move_source->bounds_to.x,
			 (1.0f - t) * move_source->bounds_from.y + t * move_source->bounds_to.y);
		obs_sceneitem_set_bounds(move_source->scene_item, &bounds);
		struct obs_sceneitem_crop crop;
		crop.left = (int)((float)(1.0f - ot) * (float)move_source->crop_from.left + ot * (float)move_source->crop_to.left);
		crop.top = (int)((float)(1.0f - ot) * (float)move_source->crop_from.top + ot * (float)move_source->crop_to.top);
		crop.right =
			(int)((float)(1.0f - ot) * (float)move_source->crop_from.right + ot * (float)move_source->crop_to.right);
		crop.bottom =
			(int)((float)(1.0f - ot) * (float)move_source->crop_from.bottom + ot * (float)move_source->crop_to.bottom);
		obs_sceneitem_set_crop(move_source->scene_item, &crop);
		obs_sceneitem_defer_update_end(move_source->scene_item);
	}
	if (!move_source->move_filter.moving) {
		move_source_ended(move_source);
	}
}

struct obs_source_info move_source_filter = {
	.id = MOVE_SOURCE_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = move_source_get_name,
	.create = move_source_create,
	.destroy = move_source_destroy,
	.get_properties = move_source_properties,
	.get_defaults = move_source_defaults,
	.video_render = move_source_video_render,
	.video_tick = move_source_tick,
	.update = move_source_update,
	.load = move_source_load,
	.activate = move_filter_activate,
	.deactivate = move_filter_deactivate,
	.show = move_filter_show,
	.hide = move_filter_hide,
};
