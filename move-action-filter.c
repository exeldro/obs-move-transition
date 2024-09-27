#include "move-transition.h"
#include "obs-frontend-api.h"

#define MOVE_ACTION_NONE 0
#define MOVE_ACTION_FRONTEND 1
#define MOVE_ACTION_SOURCE_HOTKEY 2
#define MOVE_ACTION_SOURCE_VISIBILITY 3
#define MOVE_ACTION_FILTER_ENABLE 4
#define MOVE_ACTION_FRONTEND_HOTKEY 5
#define MOVE_ACTION_SOURCE_MUTE 6
#define MOVE_ACTION_SOURCE_AUDIO_TRACK 7
#define MOVE_ACTION_SETTING 8

#define MOVE_ACTION_ENABLE 0
#define MOVE_ACTION_DISABLE 1
#define MOVE_ACTION_TOGGLE 2

#define DURATION_TYPE_CUSTOM 0
#define DURATION_TYPE_TRANSITION 1
#define DURATION_TYPE_INFINITE 2

struct move_action_action {
	bool *reverse;
	char *scene_name;
	char *sceneitem_name;
	char *source_name;
	char *filter_name;
	char *setting_name;
	char *hotkey_name;
	obs_hotkey_id hotkey_id;
	enum obs_property_type setting_type;

	long long action;

	long long frontend_action;
	long long enable;
	long long audio_track;
	char *value_string;
	long long value_int;
	double value_float;
};

struct move_action_info {
	struct move_filter move_filter;

	uint8_t duration_type;

	struct move_action_action start_action;
	struct move_action_action end_action;
	bool start;
};

bool move_action_load_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *key)
{
	struct move_action_action *move_action = data;
	if (strcmp(move_action->hotkey_name, obs_hotkey_get_name(key)) != 0)
		return true;
	obs_hotkey_registerer_t type = obs_hotkey_get_registerer_type(key);
	if (type == OBS_HOTKEY_REGISTERER_SOURCE && move_action->action == MOVE_ACTION_SOURCE_HOTKEY) {

		obs_weak_source_t *s = obs_hotkey_get_registerer(key);
		obs_source_t *source = obs_weak_source_get_source(s);
		if (source) {
			if (strcmp(obs_source_get_name(source), move_action->source_name) == 0) {
				move_action->hotkey_id = id;
				obs_source_release(source);
				return false;
			}
			obs_source_release(source);
		}
	} else if (type == OBS_HOTKEY_REGISTERER_FRONTEND && move_action->action == MOVE_ACTION_FRONTEND_HOTKEY) {
		move_action->hotkey_id = id;
		return false;
	}
	return true;
}

void move_action_update(void *data, obs_data_t *settings)
{
	struct move_action_info *move_action = data;
	move_action->duration_type = (uint8_t)obs_data_get_int(settings, "duration_type");
	obs_data_set_bool(settings, S_CUSTOM_DURATION, move_action->duration_type == DURATION_TYPE_CUSTOM);
	move_filter_update(&move_action->move_filter, settings);
	bool changed = false;

	move_action->start_action.frontend_action = obs_data_get_int(settings, "frontend_action");
	move_action->end_action.frontend_action = obs_data_get_int(settings, "end_frontend_action");
	move_action->start_action.audio_track = obs_data_get_int(settings, "audio_track");
	move_action->end_action.audio_track = obs_data_get_int(settings, "end_audio_track");
	const char *start_hotkey_name = obs_data_get_string(settings, "hotkey");
	const char *end_hotkey_name = obs_data_get_string(settings, "end_hotkey");
	long long start_action = obs_data_get_int(settings, "action");
	long long end_action = obs_data_get_int(settings, "end_action");
	if (start_action == MOVE_ACTION_NONE && move_action->start_action.frontend_action != 0) {
		start_action = MOVE_ACTION_FRONTEND;
		obs_data_set_int(settings, "action", start_action);
	}
	if (end_action == MOVE_ACTION_NONE && move_action->end_action.frontend_action != 0) {
		end_action = MOVE_ACTION_FRONTEND;
		obs_data_set_int(settings, "end_action", end_action);
	}
	if (start_action == MOVE_ACTION_NONE && strlen(start_hotkey_name)) {
		start_action = MOVE_ACTION_SOURCE_HOTKEY;
		obs_data_set_int(settings, "action", start_action);
	}

	if (end_action == MOVE_ACTION_NONE && strlen(end_hotkey_name)) {
		end_action = MOVE_ACTION_SOURCE_HOTKEY;
		obs_data_set_int(settings, "end_action", end_action);
	}

	if (move_action->start_action.action != start_action) {
		move_action->start_action.action = start_action;
		changed = true;
	}
	if (move_action->end_action.action != end_action) {
		move_action->end_action.action = end_action;
		changed = true;
	}

	if (start_action == MOVE_ACTION_SOURCE_HOTKEY || start_action == MOVE_ACTION_FILTER_ENABLE ||
	    start_action == MOVE_ACTION_SOURCE_MUTE || start_action == MOVE_ACTION_SOURCE_AUDIO_TRACK ||
	    start_action == MOVE_ACTION_SETTING) {
		const char *source_name = obs_data_get_string(settings, "source");
		if (!move_action->start_action.source_name || strcmp(source_name, move_action->start_action.source_name) != 0) {
			bfree(move_action->start_action.source_name);
			move_action->start_action.source_name = bstrdup(source_name);
			changed = true;
		}
	} else if (move_action->start_action.source_name) {
		bfree(move_action->start_action.source_name);
		move_action->start_action.source_name = NULL;
		changed = true;
	}

	if (end_action == MOVE_ACTION_SOURCE_HOTKEY || end_action == MOVE_ACTION_FILTER_ENABLE ||
	    end_action == MOVE_ACTION_SOURCE_MUTE || end_action == MOVE_ACTION_SOURCE_AUDIO_TRACK ||
	    end_action == MOVE_ACTION_SETTING) {
		const char *source_name = obs_data_get_string(settings, "end_source");
		if (!move_action->end_action.source_name || strcmp(source_name, move_action->end_action.source_name) != 0) {
			bfree(move_action->end_action.source_name);
			move_action->end_action.source_name = bstrdup(source_name);
			changed = true;
		}
	} else if (move_action->end_action.source_name) {
		bfree(move_action->end_action.source_name);
		move_action->end_action.source_name = NULL;
		changed = true;
	}

	if (start_action == MOVE_ACTION_SOURCE_HOTKEY || start_action == MOVE_ACTION_FRONTEND_HOTKEY) {
		if (!move_action->start_action.hotkey_name ||
		    strcmp(start_hotkey_name, move_action->start_action.hotkey_name) != 0) {
			bfree(move_action->start_action.hotkey_name);
			move_action->start_action.hotkey_name = bstrdup(start_hotkey_name);
			changed = true;
		}
	} else if (move_action->start_action.hotkey_name) {
		bfree(move_action->start_action.hotkey_name);
		move_action->start_action.hotkey_name = NULL;
		changed = true;
	}
	if (end_action == MOVE_ACTION_SOURCE_HOTKEY || end_action == MOVE_ACTION_FRONTEND_HOTKEY) {
		if (!move_action->end_action.hotkey_name || strcmp(end_hotkey_name, move_action->end_action.hotkey_name) != 0) {
			bfree(move_action->end_action.hotkey_name);
			move_action->end_action.hotkey_name = bstrdup(end_hotkey_name);
			changed = true;
		}
	} else if (move_action->end_action.hotkey_name) {
		bfree(move_action->end_action.hotkey_name);
		move_action->end_action.hotkey_name = NULL;
		changed = true;
	}
	if (changed) {
		move_action->start_action.hotkey_id = OBS_INVALID_HOTKEY_ID;
		if (move_action->start_action.hotkey_name && strlen(move_action->start_action.hotkey_name)) {
			if (move_action->start_action.action == MOVE_ACTION_SOURCE_HOTKEY &&
			    move_action->start_action.source_name && strlen(move_action->start_action.source_name))
				obs_enum_hotkeys(move_action_load_hotkey, &move_action->start_action);
			else if (start_action == MOVE_ACTION_FRONTEND_HOTKEY)
				obs_enum_hotkeys(move_action_load_hotkey, &move_action->start_action);
		}
		move_action->end_action.hotkey_id = OBS_INVALID_HOTKEY_ID;
		if (move_action->end_action.hotkey_name && strlen(move_action->end_action.hotkey_name)) {
			if (move_action->end_action.action == MOVE_ACTION_SOURCE_HOTKEY && move_action->end_action.source_name &&
			    strlen(move_action->end_action.source_name))
				obs_enum_hotkeys(move_action_load_hotkey, &move_action->end_action);
			else if (end_action == MOVE_ACTION_FRONTEND_HOTKEY)
				obs_enum_hotkeys(move_action_load_hotkey, &move_action->end_action);
		}
	}

	if (start_action == MOVE_ACTION_FILTER_ENABLE || start_action == MOVE_ACTION_SETTING) {
		const char *filter_name = obs_data_get_string(settings, "filter");
		if (!move_action->start_action.filter_name || strcmp(filter_name, move_action->start_action.filter_name) != 0) {
			bfree(move_action->start_action.filter_name);
			move_action->start_action.filter_name = bstrdup(filter_name);
		}
	} else if (move_action->start_action.filter_name) {
		bfree(move_action->start_action.filter_name);
		move_action->start_action.filter_name = NULL;
	}

	if (end_action == MOVE_ACTION_FILTER_ENABLE || end_action == MOVE_ACTION_SETTING) {
		const char *filter_name = obs_data_get_string(settings, "end_filter");
		if (!move_action->end_action.filter_name || strcmp(filter_name, move_action->end_action.filter_name) != 0) {
			bfree(move_action->end_action.filter_name);
			move_action->end_action.filter_name = bstrdup(filter_name);
		}
	} else if (move_action->end_action.filter_name) {
		bfree(move_action->end_action.filter_name);
		move_action->end_action.filter_name = NULL;
	}

	if (start_action == MOVE_ACTION_SETTING) {
		const char *setting_name = obs_data_get_string(settings, "setting");
		if (!move_action->start_action.setting_name || strcmp(setting_name, move_action->start_action.setting_name) != 0) {
			bfree(move_action->start_action.setting_name);
			move_action->start_action.setting_name = bstrdup(setting_name);
		}
		move_action->start_action.setting_type = obs_data_get_int(settings, "setting_type");
		if (move_action->start_action.setting_type == OBS_PROPERTY_COLOR ||
		    move_action->start_action.setting_type == OBS_PROPERTY_COLOR_ALPHA) {
			move_action->start_action.value_int = obs_data_get_int(settings, "value_color");
		} else {
			move_action->start_action.value_int = obs_data_get_int(settings, "value_int");
		}
		move_action->start_action.value_float = obs_data_get_double(settings, "value_double");
		if (move_action->start_action.setting_type == OBS_PROPERTY_TEXT ||
		    move_action->start_action.setting_type == OBS_PROPERTY_PATH) {
			move_action->start_action.value_string = bstrdup(obs_data_get_string(settings, "value_string"));
		} else {
			bfree(move_action->start_action.value_string);
			move_action->start_action.value_string = NULL;
		}
	} else if (move_action->start_action.setting_name) {
		bfree(move_action->start_action.setting_name);
		move_action->start_action.setting_name = NULL;
		bfree(move_action->start_action.value_string);
		move_action->start_action.value_string = NULL;
	}

	if (end_action == MOVE_ACTION_SETTING) {
		const char *setting_name = obs_data_get_string(settings, "end_setting");
		if (!move_action->end_action.setting_name || strcmp(setting_name, move_action->end_action.setting_name) != 0) {
			bfree(move_action->end_action.setting_name);
			move_action->end_action.setting_name = bstrdup(setting_name);
		}
		move_action->end_action.setting_type = obs_data_get_int(settings, "end_setting_type");
		if (move_action->end_action.setting_type == OBS_PROPERTY_COLOR ||
		    move_action->end_action.setting_type == OBS_PROPERTY_COLOR_ALPHA) {
			move_action->end_action.value_int = obs_data_get_int(settings, "end_value_color");
		} else {
			move_action->end_action.value_int = obs_data_get_int(settings, "end_value_int");
		}
		move_action->end_action.value_float = obs_data_get_double(settings, "end_value_double");
		if (move_action->end_action.setting_type == OBS_PROPERTY_TEXT ||
		    move_action->end_action.setting_type == OBS_PROPERTY_PATH) {
			move_action->end_action.value_string = bstrdup(obs_data_get_string(settings, "end_value_string"));
		} else {
			bfree(move_action->end_action.value_string);
			move_action->end_action.value_string = NULL;
		}
	} else if (move_action->end_action.setting_name) {
		bfree(move_action->end_action.setting_name);
		move_action->end_action.setting_name = NULL;
		bfree(move_action->end_action.value_string);
		move_action->end_action.value_string = NULL;
	}

	if (start_action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *scene_name = obs_data_get_string(settings, "scene");
		if (!move_action->start_action.scene_name || strcmp(scene_name, move_action->start_action.scene_name) != 0) {
			bfree(move_action->start_action.scene_name);
			move_action->start_action.scene_name = bstrdup(scene_name);
		}
	} else if (move_action->start_action.scene_name) {
		bfree(move_action->start_action.scene_name);
		move_action->start_action.scene_name = NULL;
	}

	if (end_action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *scene_name = obs_data_get_string(settings, "end_scene");
		if (!move_action->end_action.scene_name || strcmp(scene_name, move_action->end_action.scene_name) != 0) {
			bfree(move_action->end_action.scene_name);
			move_action->end_action.scene_name = bstrdup(scene_name);
		}
	} else if (move_action->end_action.scene_name) {
		bfree(move_action->end_action.scene_name);
		move_action->end_action.scene_name = NULL;
	}

	if (start_action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *sceneitem_name = obs_data_get_string(settings, "sceneitem");
		if (!move_action->start_action.sceneitem_name ||
		    strcmp(sceneitem_name, move_action->start_action.sceneitem_name) != 0) {
			bfree(move_action->start_action.sceneitem_name);
			move_action->start_action.sceneitem_name = bstrdup(sceneitem_name);
		}
	} else if (move_action->start_action.sceneitem_name) {
		bfree(move_action->start_action.sceneitem_name);
		move_action->start_action.sceneitem_name = NULL;
	}

	if (end_action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *sceneitem_name = obs_data_get_string(settings, "end_sceneitem");
		if (!move_action->end_action.sceneitem_name ||
		    strcmp(sceneitem_name, move_action->end_action.sceneitem_name) != 0) {
			bfree(move_action->end_action.sceneitem_name);
			move_action->end_action.sceneitem_name = bstrdup(sceneitem_name);
		}
	} else if (move_action->end_action.sceneitem_name) {
		bfree(move_action->end_action.sceneitem_name);
		move_action->end_action.sceneitem_name = NULL;
	}

	move_action->start_action.enable = obs_data_get_int(settings, "enable");
	move_action->end_action.enable = obs_data_get_int(settings, "end_enable");
}

static const char *move_action_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveActionFilter");
}

void move_action_start(void *data)
{
	struct move_action_info *move_action = data;

	if (!move_filter_start_internal(&move_action->move_filter))
		return;

	move_action->start = true;
}

static void move_action_source_rename(void *data, calldata_t *call_data)
{
	struct move_action_info *move_action = data;
	const char *new_name = calldata_string(call_data, "new_name");
	const char *prev_name = calldata_string(call_data, "prev_name");
	obs_data_t *settings = obs_source_get_settings(move_action->move_filter.source);
	if (!settings || !new_name || !prev_name)
		return;
	if (move_action->start_action.scene_name && strcmp(move_action->start_action.scene_name, prev_name) == 0) {
		bfree(move_action->start_action.scene_name);
		move_action->start_action.scene_name = bstrdup(new_name);
		obs_data_set_string(settings, "scene", new_name);
	}
	if (move_action->start_action.sceneitem_name && strcmp(move_action->start_action.sceneitem_name, prev_name) == 0) {
		bfree(move_action->start_action.sceneitem_name);
		move_action->start_action.sceneitem_name = bstrdup(new_name);
		obs_data_set_string(settings, "sceneitem", new_name);
	}
	if (move_action->start_action.source_name && strcmp(move_action->start_action.source_name, prev_name) == 0) {
		bfree(move_action->start_action.source_name);
		move_action->start_action.source_name = bstrdup(new_name);
		obs_data_set_string(settings, "source", new_name);
	}
	if (move_action->end_action.scene_name && strcmp(move_action->end_action.scene_name, prev_name) == 0) {
		bfree(move_action->end_action.scene_name);
		move_action->end_action.scene_name = bstrdup(new_name);
		obs_data_set_string(settings, "end_scene", new_name);
	}
	if (move_action->end_action.sceneitem_name && strcmp(move_action->end_action.sceneitem_name, prev_name) == 0) {
		bfree(move_action->end_action.sceneitem_name);
		move_action->end_action.sceneitem_name = bstrdup(new_name);
		obs_data_set_string(settings, "end_sceneitem", new_name);
	}
	if (move_action->end_action.source_name && strcmp(move_action->end_action.source_name, prev_name) == 0) {
		bfree(move_action->end_action.source_name);
		move_action->end_action.source_name = bstrdup(new_name);
		obs_data_set_string(settings, "end_source", new_name);
	}
	obs_data_release(settings);
}

static void *move_action_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_action_info *move_action = bzalloc(sizeof(struct move_action_info));
	move_filter_init(&move_action->move_filter, source, move_action_start);
	move_action->start_action.hotkey_id = OBS_INVALID_HOTKEY_ID;
	move_action->end_action.hotkey_id = OBS_INVALID_HOTKEY_ID;
	move_action->start_action.reverse = &move_action->move_filter.reverse;
	move_action->end_action.reverse = &move_action->move_filter.reverse;
	if ((obs_get_source_output_flags(obs_source_get_id(source)) & OBS_SOURCE_VIDEO) == 0) {
		move_action_update(move_action, settings);
	} else {
		obs_source_update(source, settings);
	}
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_rename", move_action_source_rename, move_action);
	return move_action;
}

static void move_action_actual_destroy(void *data)
{
	struct move_action_info *move_action = data;
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", move_action_source_rename, move_action);
	move_filter_destroy(&move_action->move_filter);
	bfree(move_action->start_action.source_name);
	bfree(move_action->start_action.hotkey_name);
	bfree(move_action->start_action.scene_name);
	bfree(move_action->start_action.sceneitem_name);
	bfree(move_action->start_action.filter_name);
	bfree(move_action->start_action.setting_name);
	bfree(move_action->start_action.value_string);
	bfree(move_action->end_action.source_name);
	bfree(move_action->end_action.hotkey_name);
	bfree(move_action->end_action.scene_name);
	bfree(move_action->end_action.sceneitem_name);
	bfree(move_action->end_action.filter_name);
	bfree(move_action->end_action.setting_name);
	bfree(move_action->end_action.value_string);

	bfree(move_action);
}

static void move_action_destroy(void *data)
{
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", move_action_source_rename, data);
	obs_queue_task(OBS_TASK_UI, move_action_actual_destroy, data, false);
}

struct hotkey_enum_add_data {
	obs_weak_source_t *source;
	obs_property_t *hotkey_prop;
};

static bool add_source_hotkeys(void *data, obs_hotkey_id id, obs_hotkey_t *key)
{
	UNUSED_PARAMETER(id);
	struct hotkey_enum_add_data *hd = data;
	obs_hotkey_registerer_t type = obs_hotkey_get_registerer_type(key);
	/*if (type == OBS_HOTKEY_REGISTERER_FRONTEND && !hd->source) {
		obs_property_list_add_string(hd->hotkey_prop,
					     obs_hotkey_get_description(key),
					     obs_hotkey_get_name(key));
	} else*/
	if (type == OBS_HOTKEY_REGISTERER_SOURCE) {
		obs_weak_source_t *s = obs_hotkey_get_registerer(key);
		if (s == hd->source) {
			obs_property_list_add_string(hd->hotkey_prop, obs_hotkey_get_description(key), obs_hotkey_get_name(key));
		}
	}
	return true;
}

static bool add_global_hotkeys(void *data, obs_hotkey_id id, obs_hotkey_t *key)
{
	UNUSED_PARAMETER(id);
	obs_property_t *prop = data;
	obs_hotkey_registerer_t type = obs_hotkey_get_registerer_type(key);
	if (type == OBS_HOTKEY_REGISTERER_FRONTEND) {
		obs_property_list_add_string(prop, obs_hotkey_get_description(key), obs_hotkey_get_name(key));
	}
	return true;
}

static void add_filter_to_prop_list(obs_source_t *parent, obs_source_t *child, void *data)
{
	UNUSED_PARAMETER(parent);
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(child);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
}

static bool move_action_source_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *source_name = obs_data_get_string(settings, "source");
	obs_property_t *filter = obs_properties_get(props, "filter");
	obs_property_list_clear(filter);
	obs_property_t *setting = obs_properties_get(props, "setting");
	obs_property_list_clear(setting);
	obs_source_t *source = obs_get_source_by_name(source_name);
	long long action = obs_data_get_int(settings, "action");
	if (action == MOVE_ACTION_SETTING) {
		obs_source_t *f = obs_source_get_filter_by_name(source, obs_data_get_string(settings, "filter"));
		obs_source_release(f);
		if (!f)
			f = source;
		obs_property_list_add_string(filter, "", "");
		obs_properties_t *ps = obs_source_properties(f);
		obs_property_t *p = obs_properties_first(ps);
		for (; p != NULL; obs_property_next(&p)) {
			obs_property_list_add_string(setting, obs_property_description(p), obs_property_name(p));
		}
		obs_properties_destroy(ps);
	}
	obs_source_enum_filters(source, add_filter_to_prop_list, filter);
	if (action == MOVE_ACTION_SOURCE_HOTKEY) {
		obs_property_t *hotkey = obs_properties_get(props, "hotkey");
		obs_property_list_clear(hotkey);
		obs_property_list_add_string(hotkey, "", "");
		struct hotkey_enum_add_data hd;
		hd.hotkey_prop = hotkey;
		hd.source = obs_source_get_weak_source(source);
		obs_enum_hotkeys(add_source_hotkeys, &hd);
		obs_weak_source_release(hd.source);
	}
	obs_source_release(source);
	return true;
}

static bool move_action_end_source_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *source_name = obs_data_get_string(settings, "end_source");
	obs_property_t *filter = obs_properties_get(props, "end_filter");
	obs_property_list_clear(filter);
	obs_property_t *setting = obs_properties_get(props, "end_setting");
	obs_property_list_clear(setting);
	obs_source_t *source = obs_get_source_by_name(source_name);
	long long action = obs_data_get_int(settings, "end_action");
	if (action == MOVE_ACTION_SETTING) {
		obs_source_t *f = obs_source_get_filter_by_name(source, obs_data_get_string(settings, "end_filter"));
		obs_source_release(f);
		if (!f)
			f = source;
		obs_property_list_add_string(filter, "", "");
		obs_properties_t *ps = obs_source_properties(f);
		obs_property_t *p = obs_properties_first(ps);
		for (; p != NULL; obs_property_next(&p)) {
			obs_property_list_add_string(setting, obs_property_description(p), obs_property_name(p));
		}
		obs_properties_destroy(ps);
	}
	obs_source_enum_filters(source, add_filter_to_prop_list, filter);
	if (action == MOVE_ACTION_SOURCE_HOTKEY) {
		obs_property_t *hotkey = obs_properties_get(props, "end_hotkey");
		obs_property_list_clear(hotkey);
		obs_property_list_add_string(hotkey, "", "");
		struct hotkey_enum_add_data hd;
		hd.hotkey_prop = hotkey;
		hd.source = obs_source_get_weak_source(source);
		obs_enum_hotkeys(add_source_hotkeys, &hd);
		obs_weak_source_release(hd.source);
	}
	obs_source_release(source);
	return true;
}

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(31, 0, 0)
#define OBS_COMBO_FORMAT_BOOL 4
#endif

static bool move_action_setting_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	obs_property_t *enable = obs_properties_get(props, "enable");
	obs_property_t *value_double = obs_properties_get(props, "value_double");
	obs_property_t *value_int = obs_properties_get(props, "value_int");
	obs_property_t *value_color = obs_properties_get(props, "value_color");
	obs_property_t *value_string = obs_properties_get(props, "value_string");
	long long action = obs_data_get_int(settings, "action");
	if (action != MOVE_ACTION_SETTING) {
		obs_property_set_visible(value_double, false);
		obs_property_set_visible(value_int, false);
		obs_property_set_visible(value_color, false);
		obs_property_set_visible(value_string, false);
		return false;
	}
	const char *source_name = obs_data_get_string(settings, "source");
	obs_source_t *source = obs_get_source_by_name(source_name);
	obs_source_t *f = obs_source_get_filter_by_name(source, obs_data_get_string(settings, "filter"));
	obs_source_release(f);
	if (!f)
		f = source;
	long long setting_type = obs_data_get_int(settings, "setting_type");
	obs_properties_t *ps = obs_source_properties(f);
	obs_property_t *p = obs_properties_get(ps, obs_data_get_string(settings, "setting"));
	if (p) {
		setting_type = obs_property_get_type(p);
		if (setting_type == OBS_PROPERTY_LIST) {
			int list_format = obs_property_list_format(p);
			if (list_format == OBS_COMBO_FORMAT_INT) {
				setting_type = OBS_PROPERTY_INT;
			} else if (list_format == OBS_COMBO_FORMAT_FLOAT) {
				setting_type = OBS_PROPERTY_FLOAT;
			} else if (list_format == OBS_COMBO_FORMAT_STRING) {
				setting_type = OBS_PROPERTY_TEXT;
			} else if (list_format == OBS_COMBO_FORMAT_BOOL) {
				setting_type = OBS_PROPERTY_BOOL;
			}
		}
		obs_data_set_int(settings, "setting_type", setting_type);
		if (setting_type == OBS_PROPERTY_FLOAT) {
			obs_property_float_set_limits(value_double, obs_property_float_min(p), obs_property_float_max(p),
						      obs_property_float_step(p));
		} else if (setting_type == OBS_PROPERTY_INT) {
			obs_property_int_set_limits(value_int, obs_property_int_min(p), obs_property_int_max(p),
						      obs_property_int_step(p));
		}
	}
	obs_properties_destroy(ps);
	obs_source_release(source);

	obs_property_set_visible(enable, setting_type == OBS_PROPERTY_BOOL);
	obs_property_set_visible(value_double, setting_type == OBS_PROPERTY_FLOAT);
	obs_property_set_visible(value_int, setting_type == OBS_PROPERTY_INT);
	obs_property_set_visible(value_color, setting_type == OBS_PROPERTY_COLOR || setting_type == OBS_PROPERTY_COLOR_ALPHA);
	obs_property_set_visible(value_string, setting_type == OBS_PROPERTY_TEXT || setting_type == OBS_PROPERTY_PATH);
	return true;
}

static bool move_action_end_setting_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	obs_property_t *enable = obs_properties_get(props, "end_enable");
	obs_property_t *value_double = obs_properties_get(props, "end_value_double");
	obs_property_t *value_int = obs_properties_get(props, "end_value_int");
	obs_property_t *value_color = obs_properties_get(props, "end_value_color");
	obs_property_t *value_string = obs_properties_get(props, "end_value_string");
	long long action = obs_data_get_int(settings, "end_action");
	if (action != MOVE_ACTION_SETTING) {
		obs_property_set_visible(value_double, false);
		obs_property_set_visible(value_int, false);
		obs_property_set_visible(value_color, false);
		obs_property_set_visible(value_string, false);
		return false;
	}
	const char *source_name = obs_data_get_string(settings, "end_source");
	obs_source_t *source = obs_get_source_by_name(source_name);
	obs_source_t *f = obs_source_get_filter_by_name(source, obs_data_get_string(settings, "end_filter"));
	obs_source_release(f);
	if (!f)
		f = source;
	long long setting_type = obs_data_get_int(settings, "end_setting_type");
	obs_properties_t *ps = obs_source_properties(f);
	obs_property_t *p = obs_properties_get(ps, obs_data_get_string(settings, "end_setting"));
	if (p) {
		setting_type = obs_property_get_type(p);
		if (setting_type == OBS_PROPERTY_LIST) {
			int list_format = obs_property_list_format(p);
			if (list_format == OBS_COMBO_FORMAT_INT) {
				setting_type = OBS_PROPERTY_INT;
			} else if (list_format == OBS_COMBO_FORMAT_FLOAT) {
				setting_type = OBS_PROPERTY_FLOAT;
			} else if (list_format == OBS_COMBO_FORMAT_STRING) {
				setting_type = OBS_PROPERTY_TEXT;
			} else if (list_format == OBS_COMBO_FORMAT_BOOL) {
				setting_type = OBS_PROPERTY_BOOL;
			}
		}
		obs_data_set_int(settings, "end_setting_type", setting_type);
		if (setting_type == OBS_PROPERTY_FLOAT) {
			obs_property_float_set_limits(value_double, obs_property_float_min(p), obs_property_float_max(p),
						      obs_property_float_step(p));
		} else if (setting_type == OBS_PROPERTY_INT) {
			obs_property_int_set_limits(value_int, obs_property_int_min(p), obs_property_int_max(p),
						    obs_property_int_step(p));
		}
	}
	obs_properties_destroy(ps);
	obs_source_release(source);

	obs_property_set_visible(enable, setting_type == OBS_PROPERTY_BOOL);
	obs_property_set_visible(value_double, setting_type == OBS_PROPERTY_FLOAT);
	obs_property_set_visible(value_int, setting_type == OBS_PROPERTY_INT);
	obs_property_set_visible(value_color, setting_type == OBS_PROPERTY_COLOR || setting_type == OBS_PROPERTY_COLOR_ALPHA);
	obs_property_set_visible(value_string, setting_type == OBS_PROPERTY_TEXT || setting_type == OBS_PROPERTY_PATH);
	return true;
}

static bool move_action_duration_type_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	obs_property_t *duration = obs_properties_get(props, S_DURATION);
	long long duration_type = obs_data_get_int(settings, "duration_type");
	const bool custom_duration = duration_type == DURATION_TYPE_CUSTOM;
	if (obs_property_enabled(duration) == custom_duration)
		return false;
	obs_property_set_enabled(duration, duration_type == DURATION_TYPE_CUSTOM);
	return true;
}

static bool add_source_to_prop_list(void *data, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(source);
	if (!name || !strlen(name))
		return true;
	size_t count = obs_property_list_item_count(p);
	size_t idx = 0;
	while (idx < count && strcmp(name, obs_property_list_item_string(p, idx)) > 0)
		idx++;
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

static bool add_group_to_prop_list(void *data, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)data;
	if (!obs_source_is_group(source))
		return true;
	const char *name = obs_source_get_name(source);
	if (!name || !strlen(name))
		return true;
	size_t count = obs_property_list_item_count(p);
	size_t idx = 0;
	while (idx < count && strcmp(name, obs_property_list_item_string(p, idx)) > 0)
		idx++;
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

static bool move_action_action_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	long long action = obs_data_get_int(settings, "action");
	obs_property_t *scene = obs_properties_get(props, "scene");
	obs_property_t *sceneitem = obs_properties_get(props, "sceneitem");
	if (action == MOVE_ACTION_SOURCE_VISIBILITY) {
		obs_property_list_clear(scene);
		obs_enum_scenes(add_source_to_prop_list, scene);
		obs_enum_sources(add_group_to_prop_list, scene);
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
	} else {
		obs_property_set_visible(scene, false);
		obs_property_set_visible(sceneitem, false);
	}
	obs_property_t *source = obs_properties_get(props, "source");
	obs_property_t *filter = obs_properties_get(props, "filter");
	obs_property_t *setting = obs_properties_get(props, "setting");
	obs_property_t *hotkey = obs_properties_get(props, "hotkey");
	if (action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SOURCE_HOTKEY || action == MOVE_ACTION_SOURCE_MUTE ||
	    action == MOVE_ACTION_SOURCE_AUDIO_TRACK || action == MOVE_ACTION_SETTING) {
		obs_property_list_clear(source);
		obs_enum_sources(add_source_to_prop_list, source);
		obs_enum_scenes(add_source_to_prop_list, source);
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SETTING);
		obs_property_set_visible(hotkey, action == MOVE_ACTION_SOURCE_HOTKEY);
		obs_property_set_visible(setting, action == MOVE_ACTION_SETTING);
	} else {
		obs_property_set_visible(source, false);
		obs_property_set_visible(filter, false);
		obs_property_set_visible(hotkey, action == MOVE_ACTION_FRONTEND_HOTKEY);
		obs_property_set_visible(setting, false);
	}
	obs_property_t *audio_track = obs_properties_get(props, "audio_track");
	obs_property_set_visible(audio_track, action == MOVE_ACTION_SOURCE_AUDIO_TRACK);
	if (action == MOVE_ACTION_FRONTEND_HOTKEY) {
		obs_property_list_clear(hotkey);
		obs_property_list_add_string(hotkey, "", "");
		obs_enum_hotkeys(add_global_hotkeys, hotkey);
	}
	obs_property_t *frontend_action = obs_properties_get(props, "frontend_action");
	obs_property_set_visible(frontend_action, action == MOVE_ACTION_FRONTEND);

	obs_property_t *enable = obs_properties_get(props, "enable");
	obs_property_set_visible(enable, action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SOURCE_VISIBILITY ||
						 action == MOVE_ACTION_SOURCE_MUTE || action == MOVE_ACTION_SOURCE_AUDIO_TRACK ||
						 (action == MOVE_ACTION_SETTING &&
						  obs_data_get_int(settings, "setting_type") == OBS_PROPERTY_BOOL));

	return true;
}

static bool move_action_end_action_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	long long action = obs_data_get_int(settings, "end_action");
	obs_property_t *scene = obs_properties_get(props, "end_scene");
	obs_property_t *sceneitem = obs_properties_get(props, "end_sceneitem");
	if (action == MOVE_ACTION_SOURCE_VISIBILITY) {
		obs_property_list_clear(scene);
		obs_enum_scenes(add_source_to_prop_list, scene);
		obs_enum_sources(add_group_to_prop_list, scene);
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
	} else {
		obs_property_set_visible(scene, false);
		obs_property_set_visible(sceneitem, false);
	}
	obs_property_t *source = obs_properties_get(props, "end_source");
	obs_property_t *filter = obs_properties_get(props, "end_filter");
	obs_property_t *setting = obs_properties_get(props, "end_setting");
	obs_property_t *hotkey = obs_properties_get(props, "end_hotkey");
	if (action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SOURCE_HOTKEY || action == MOVE_ACTION_SOURCE_MUTE ||
	    action == MOVE_ACTION_SOURCE_AUDIO_TRACK || action == MOVE_ACTION_SETTING) {
		obs_property_list_clear(source);
		obs_enum_sources(add_source_to_prop_list, source);
		obs_enum_scenes(add_source_to_prop_list, source);
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SETTING);
		obs_property_set_visible(hotkey, action == MOVE_ACTION_SOURCE_HOTKEY);
		obs_property_set_visible(setting, action == MOVE_ACTION_SETTING);
	} else {
		obs_property_set_visible(source, false);
		obs_property_set_visible(filter, false);
		obs_property_set_visible(hotkey, action == MOVE_ACTION_FRONTEND_HOTKEY);
		obs_property_set_visible(setting, false);
	}
	obs_property_t *audio_track = obs_properties_get(props, "end_audio_track");
	obs_property_set_visible(audio_track, action == MOVE_ACTION_SOURCE_AUDIO_TRACK);
	if (action == MOVE_ACTION_FRONTEND_HOTKEY) {
		obs_property_list_clear(hotkey);
		obs_property_list_add_string(hotkey, "", "");
		obs_enum_hotkeys(add_global_hotkeys, hotkey);
	}
	obs_property_t *frontend_action = obs_properties_get(props, "end_frontend_action");
	obs_property_set_visible(frontend_action, action == MOVE_ACTION_FRONTEND);

	obs_property_t *enable = obs_properties_get(props, "end_enable");
	obs_property_set_visible(enable, action == MOVE_ACTION_FILTER_ENABLE || action == MOVE_ACTION_SOURCE_VISIBILITY ||
						 action == MOVE_ACTION_SOURCE_MUTE || action == MOVE_ACTION_SOURCE_AUDIO_TRACK ||
						 (action == MOVE_ACTION_SETTING &&
						  obs_data_get_int(settings, "end_setting_type") == OBS_PROPERTY_BOOL));

	return true;
}

static bool add_sceneitem_to_prop_list(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	obs_property_t *p = (obs_property_t *)data;
	const obs_source_t *source = obs_sceneitem_get_source(item);
	const char *name = obs_source_get_name(source);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
	return true;
}

static bool move_action_scene_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *scene_name = obs_data_get_string(settings, "scene");
	obs_property_t *sceneitem = obs_properties_get(props, "sceneitem");
	obs_property_list_clear(sceneitem);
	obs_source_t *source = obs_get_source_by_name(scene_name);
	obs_source_release(source);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);
	if (!scene)
		return true;
	obs_scene_enum_items(scene, add_sceneitem_to_prop_list, sceneitem);
	return true;
}

static bool move_action_end_scene_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *scene_name = obs_data_get_string(settings, "end_scene");
	obs_property_t *sceneitem = obs_properties_get(props, "end_sceneitem");
	obs_property_list_clear(sceneitem);
	obs_source_t *source = obs_get_source_by_name(scene_name);
	obs_source_release(source);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);
	if (!scene)
		return true;
	obs_scene_enum_items(scene, add_sceneitem_to_prop_list, sceneitem);
	return true;
}

void prop_list_add_move_filter(obs_source_t *parent, obs_source_t *child, void *data);
bool move_filter_start_button(obs_properties_t *props, obs_property_t *property, void *data);

static obs_properties_t *move_action_properties(void *data)
{
	struct move_action_info *move_action = data;
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_t *start_action = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(start_action, "action", obs_module_text("Action"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NoneAction"), MOVE_ACTION_NONE);
	obs_property_list_add_int(p, obs_module_text("FrontendAction"), MOVE_ACTION_FRONTEND);
	obs_property_list_add_int(p, obs_module_text("SourceVisibility"), MOVE_ACTION_SOURCE_VISIBILITY);
	obs_property_list_add_int(p, obs_module_text("SourceMute"), MOVE_ACTION_SOURCE_MUTE);
	obs_property_list_add_int(p, obs_module_text("SourceAudioTrack"), MOVE_ACTION_SOURCE_AUDIO_TRACK);
	obs_property_list_add_int(p, obs_module_text("SourceHotkey"), MOVE_ACTION_SOURCE_HOTKEY);
	obs_property_list_add_int(p, obs_module_text("FilterEnable"), MOVE_ACTION_FILTER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("FrontendHotkey"), MOVE_ACTION_FRONTEND_HOTKEY);
	obs_property_list_add_int(p, obs_module_text("Setting"), MOVE_ACTION_SETTING);
	obs_property_set_modified_callback(p, move_action_action_changed);

	p = obs_properties_add_list(start_action, "scene", obs_module_text("Scene"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(p, move_action_scene_changed, data);

	p = obs_properties_add_list(start_action, "sceneitem", obs_module_text("Source"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(start_action, "source", obs_module_text("Source"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_source_changed, data);

	p = obs_properties_add_list(start_action, "filter", obs_module_text("Filter"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_source_changed, data);

	p = obs_properties_add_list(start_action, "setting", obs_module_text("Setting"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_setting_changed, data);

	obs_properties_add_float(start_action, "value_double", obs_module_text("Value"), 0.0, 0.0, 1.0);
	obs_properties_add_int(start_action, "value_int", obs_module_text("Value"), 0, 0, 1);
	obs_properties_add_color_alpha(start_action, "value_color", obs_module_text("Value"));
	obs_properties_add_text(start_action, "value_string", obs_module_text("Value"), OBS_TEXT_MULTILINE); //OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(start_action, "audio_track", obs_module_text("AudioTrack"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.None"), 0);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.All"), -1);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track1"), 1);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track2"), 2);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track3"), 3);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track4"), 4);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track5"), 5);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track6"), 6);

	p = obs_properties_add_list(start_action, "enable", obs_module_text("Enable"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Enable"), MOVE_ACTION_ENABLE);
	obs_property_list_add_int(p, obs_module_text("Disable"), MOVE_ACTION_DISABLE);
	obs_property_list_add_int(p, obs_module_text("Toggle"), MOVE_ACTION_TOGGLE);

	p = obs_properties_add_list(start_action, "hotkey", obs_module_text("Hotkey"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(start_action, "frontend_action", obs_module_text("FrontendAction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.None"), 0);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StreamingStart"), 1);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StreamingStop"), 2);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingStart"), 3);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingStop"), 4);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingPause"), 5);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingUnpause"), 6);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.VirtualCamStart"), 7);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.VirtualCamStop"), 8);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferStart"), 9);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferStop"), 10);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferSave"), 11);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StudioModeEnable"), 12);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StudioModeDisable"), 13);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.TakeScreenshot"), 14);

	p = obs_properties_add_group(ppts, "start_action", obs_module_text("StartAction"), OBS_GROUP_NORMAL, start_action);

	//obs_frontend_preview_program_trigger_transition

	//obs_frontend_take_source_screenshot
	//obs_frontend_open_filters
	//obs_frontend_open_properties

	//move_filter_properties(data, ppts);

	obs_properties_t *duration = obs_properties_create();

	p = obs_properties_add_list(duration, "duration_type", obs_module_text("Duration"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("CustomDuration"), DURATION_TYPE_CUSTOM);
	obs_property_list_add_int(p, obs_module_text("TransitionDuration"), DURATION_TYPE_TRANSITION);
	obs_property_list_add_int(p, obs_module_text("InfiniteDuration"), DURATION_TYPE_INFINITE);

	obs_property_set_modified_callback2(p, move_action_duration_type_changed, data);

	p = obs_properties_add_int(duration, S_DURATION, obs_module_text("CustomDuration"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_group(ppts, S_CUSTOM_DURATION, obs_module_text("Duration"), OBS_GROUP_NORMAL, duration);

	obs_properties_t *end_action = obs_properties_create();

	p = obs_properties_add_list(end_action, "end_action", obs_module_text("Action"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NoneAction"), MOVE_ACTION_NONE);
	obs_property_list_add_int(p, obs_module_text("FrontendAction"), MOVE_ACTION_FRONTEND);
	obs_property_list_add_int(p, obs_module_text("SourceVisibility"), MOVE_ACTION_SOURCE_VISIBILITY);
	obs_property_list_add_int(p, obs_module_text("SourceMute"), MOVE_ACTION_SOURCE_MUTE);
	obs_property_list_add_int(p, obs_module_text("SourceAudioTrack"), MOVE_ACTION_SOURCE_AUDIO_TRACK);
	obs_property_list_add_int(p, obs_module_text("SourceHotkey"), MOVE_ACTION_SOURCE_HOTKEY);
	obs_property_list_add_int(p, obs_module_text("FilterEnable"), MOVE_ACTION_FILTER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("FrontendHotkey"), MOVE_ACTION_FRONTEND_HOTKEY);
	obs_property_list_add_int(p, obs_module_text("Setting"), MOVE_ACTION_SETTING);
	obs_property_set_modified_callback(p, move_action_end_action_changed);

	p = obs_properties_add_list(end_action, "end_scene", obs_module_text("Scene"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(p, move_action_end_scene_changed, data);

	p = obs_properties_add_list(end_action, "end_sceneitem", obs_module_text("Source"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(end_action, "end_source", obs_module_text("Source"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_end_source_changed, data);

	p = obs_properties_add_list(end_action, "end_filter", obs_module_text("Filter"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_end_source_changed, data);

	p = obs_properties_add_list(end_action, "end_setting", obs_module_text("Setting"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_end_setting_changed, data);

	obs_properties_add_float(end_action, "end_value_double", obs_module_text("Value"), 0.0, 0.0, 1.0);
	obs_properties_add_int(end_action, "end_value_int", obs_module_text("Value"), 0, 0, 1);
	obs_properties_add_color_alpha(end_action, "end_value_color", obs_module_text("Value"));
	obs_properties_add_text(end_action, "end_value_string", obs_module_text("Value"), OBS_TEXT_MULTILINE); //OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(end_action, "end_audio_track", obs_module_text("AudioTrack"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.None"), 0);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.All"), -1);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track1"), 1);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track2"), 2);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track3"), 3);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track4"), 4);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track5"), 5);
	obs_property_list_add_int(p, obs_module_text("AudioTrack.Track6"), 6);

	p = obs_properties_add_list(end_action, "end_enable", obs_module_text("Enable"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Enable"), MOVE_ACTION_ENABLE);
	obs_property_list_add_int(p, obs_module_text("Disable"), MOVE_ACTION_DISABLE);
	obs_property_list_add_int(p, obs_module_text("Toggle"), MOVE_ACTION_TOGGLE);

	p = obs_properties_add_list(end_action, "end_hotkey", obs_module_text("Hotkey"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(end_action, "end_frontend_action", obs_module_text("FrontendAction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.None"), 0);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StreamingStart"), 1);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StreamingStop"), 2);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingStart"), 3);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingStop"), 4);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingPause"), 5);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.RecordingUnpause"), 6);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.VirtualCamStart"), 7);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.VirtualCamStop"), 8);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferStart"), 9);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferStop"), 10);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.ReplayBufferSave"), 11);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StudioModeEnable"), 12);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.StudioModeDisable"), 13);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.TakeScreenshot"), 14);

	p = obs_properties_add_group(ppts, "end_action", obs_module_text("EndAction"), OBS_GROUP_NORMAL, end_action);

	p = obs_properties_add_bool(ppts, S_ENABLED_MATCH_MOVING, obs_module_text("EnabledMatchMoving"));

	p = obs_properties_add_list(ppts, S_START_TRIGGER, obs_module_text("StartTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Load"), START_TRIGGER_LOAD);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);

	p = obs_properties_add_list(ppts, S_STOP_TRIGGER, obs_module_text("StopTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StopTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);

	obs_source_t *parent = obs_filter_get_parent(move_action->move_filter.source);

	p = obs_properties_add_list(ppts, S_SIMULTANEOUS_MOVE, obs_module_text("SimultaneousMove"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
	if (parent)
		obs_source_enum_filters(parent, prop_list_add_move_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE, obs_module_text("NextMove"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
	if (parent)
		obs_source_enum_filters(parent, prop_list_add_move_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE_ON, obs_module_text("NextMoveOn"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"), NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"), NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(ppts, "move_filter_start", obs_module_text("Start"), move_filter_start_button);
	obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
	return ppts;
}

void move_action_execute(void *data)
{
	struct move_action_action *move_action = data;
	if (move_action->action == MOVE_ACTION_SOURCE_HOTKEY) {
		if (move_action->hotkey_id == OBS_INVALID_HOTKEY_ID && move_action->hotkey_name &&
		    strlen(move_action->hotkey_name) && move_action->source_name && strlen(move_action->source_name)) {
			obs_enum_hotkeys(move_action_load_hotkey, move_action);
		}
		if (move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_trigger_routed_callback(move_action->hotkey_id, !*move_action->reverse);
	} else if (move_action->action == MOVE_ACTION_FRONTEND_HOTKEY) {
		if (move_action->hotkey_id == OBS_INVALID_HOTKEY_ID && move_action->hotkey_name &&
		    strlen(move_action->hotkey_name)) {
			obs_enum_hotkeys(move_action_load_hotkey, move_action);
		}
		if (move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_trigger_routed_callback(move_action->hotkey_id, !*move_action->reverse);
	} else if (move_action->action == MOVE_ACTION_FRONTEND) {
		if (move_action->frontend_action == 1) {
			obs_frontend_streaming_start();
		} else if (move_action->frontend_action == 2) {
			obs_frontend_streaming_stop();
		} else if (move_action->frontend_action == 3) {
			obs_frontend_recording_start();
		} else if (move_action->frontend_action == 4) {
			obs_frontend_recording_stop();
		} else if (move_action->frontend_action == 5) {
			obs_frontend_recording_pause(true);
		} else if (move_action->frontend_action == 6) {
			obs_frontend_recording_pause(false);
		} else if (move_action->frontend_action == 7) {
			obs_frontend_start_virtualcam();
		} else if (move_action->frontend_action == 8) {
			obs_frontend_stop_virtualcam();
		} else if (move_action->frontend_action == 9) {
			obs_frontend_replay_buffer_start();
		} else if (move_action->frontend_action == 10) {
			obs_frontend_replay_buffer_stop();
		} else if (move_action->frontend_action == 11) {
			obs_frontend_replay_buffer_save();
		} else if (move_action->frontend_action == 12) {
			obs_frontend_set_preview_program_mode(true);
		} else if (move_action->frontend_action == 13) {
			obs_frontend_set_preview_program_mode(false);
		} else if (move_action->frontend_action == 14) {
			obs_frontend_take_screenshot();
		}
	} else if (move_action->action == MOVE_ACTION_SOURCE_VISIBILITY) {
		if (move_action->scene_name && move_action->sceneitem_name && strlen(move_action->scene_name) &&
		    strlen(move_action->sceneitem_name)) {
			obs_source_t *scene_source = obs_get_source_by_name(move_action->scene_name);
			obs_scene_t *scene = obs_scene_from_source(scene_source);
			if (!scene)
				scene = obs_group_from_source(scene_source);
			obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, move_action->sceneitem_name);
			if (item) {

				if (move_action->enable == MOVE_ACTION_TOGGLE) {
					obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
				} else if (move_action->enable == MOVE_ACTION_ENABLE) {
					if (!obs_sceneitem_visible(item))
						obs_sceneitem_set_visible(item, true);
				} else if (move_action->enable == MOVE_ACTION_DISABLE) {
					if (obs_sceneitem_visible(item))
						obs_sceneitem_set_visible(item, false);
				}
			}
			obs_source_release(scene_source);
		}
	} else if (move_action->action == MOVE_ACTION_FILTER_ENABLE) {
		if (move_action->source_name && move_action->filter_name && strlen(move_action->source_name) &&
		    strlen(move_action->filter_name)) {
			obs_source_t *source = obs_get_source_by_name(move_action->source_name);
			obs_source_t *filter = source ? obs_source_get_filter_by_name(source, move_action->filter_name) : NULL;
			if (filter) {

				if (move_action->enable == MOVE_ACTION_TOGGLE) {
					obs_source_set_enabled(filter, !obs_source_enabled(filter));
				} else if (move_action->enable == MOVE_ACTION_ENABLE) {
					if (!obs_source_enabled(filter))
						obs_source_set_enabled(filter, true);
				} else if (move_action->enable == MOVE_ACTION_DISABLE) {
					if (obs_source_enabled(filter))
						obs_source_set_enabled(filter, false);
				}
				obs_source_release(filter);
			}
			obs_source_release(source);
		}
	} else if (move_action->action == MOVE_ACTION_SOURCE_MUTE) {
		if (move_action->source_name && strlen(move_action->source_name)) {
			obs_source_t *source = obs_get_source_by_name(move_action->source_name);
			if (source) {
				if (move_action->enable == MOVE_ACTION_TOGGLE) {
					obs_source_set_muted(source, !obs_source_muted(source));
				} else if (move_action->enable == MOVE_ACTION_ENABLE) {
					if (!obs_source_muted(source))
						obs_source_set_muted(source, true);
				} else if (move_action->enable == MOVE_ACTION_DISABLE) {
					if (obs_source_muted(source))
						obs_source_set_muted(source, false);
				}
				obs_source_release(source);
			}
		}
	} else if (move_action->action == MOVE_ACTION_SOURCE_AUDIO_TRACK) {
		if (move_action->audio_track != 0 && move_action->source_name && strlen(move_action->source_name)) {
			obs_source_t *source = obs_get_source_by_name(move_action->source_name);
			if (source) {
				uint32_t mixers = obs_source_get_audio_mixers(source);
				if (move_action->audio_track < 0) {
					uint32_t map = 0x3f;
					if (move_action->enable == MOVE_ACTION_TOGGLE) {
						mixers ^= map;
						obs_source_set_audio_mixers(source, mixers);
					} else if (move_action->enable == MOVE_ACTION_ENABLE) {
						if (mixers != map) {
							mixers = map;
							obs_source_set_audio_mixers(source, mixers);
						}
					} else if (move_action->enable == MOVE_ACTION_DISABLE) {
						if (mixers) {
							mixers = 0;
							obs_source_set_audio_mixers(source, mixers);
						}
					}
				} else {
					uint32_t map = (1 << (move_action->audio_track - 1));
					if (move_action->enable == MOVE_ACTION_TOGGLE) {
						if (mixers & map) {
							mixers -= map;
						} else {
							mixers += map;
						}
						obs_source_set_audio_mixers(source, mixers);
					} else if (move_action->enable == MOVE_ACTION_ENABLE) {
						if (!(mixers & map)) {
							mixers += map;
							obs_source_set_audio_mixers(source, mixers);
						}
					} else if (move_action->enable == MOVE_ACTION_DISABLE) {
						if (mixers & map) {
							mixers -= map;
							obs_source_set_audio_mixers(source, mixers);
						}
					}
				}
				obs_source_release(source);
			}
		}
	} else if (move_action->action == MOVE_ACTION_SETTING) {
		if (move_action->source_name && strlen(move_action->source_name)) {
			obs_source_t *source = obs_get_source_by_name(move_action->source_name);
			obs_source_t *target = source && move_action->filter_name && strlen(move_action->filter_name)
						       ? obs_source_get_filter_by_name(source, move_action->filter_name)
						       : NULL;
			obs_source_release(target);
			if (!target)
				target = source;
			if (target) {
				obs_data_t *settings = obs_source_get_settings(target);
				if (move_action->setting_type == OBS_PROPERTY_BOOL) {
					if (move_action->enable == MOVE_ACTION_TOGGLE) {
						obs_data_set_bool(settings, move_action->setting_name,
								  !obs_data_get_bool(settings, move_action->setting_name));
						obs_source_update(target, settings);
					} else if (move_action->enable == MOVE_ACTION_ENABLE) {
						if (!obs_data_get_bool(settings, move_action->setting_name)) {
							obs_data_set_bool(settings, move_action->setting_name, true);
							obs_source_update(target, settings);
						}
					} else if (move_action->enable == MOVE_ACTION_DISABLE) {
						if (obs_data_get_bool(settings, move_action->setting_name)) {
							obs_data_set_bool(settings, move_action->setting_name, false);
							obs_source_update(target, settings);
						}
					}
				} else if (move_action->setting_type == OBS_PROPERTY_TEXT ||
					   move_action->setting_type == OBS_PROPERTY_PATH) {
					if (move_action->value_string &&
					    strcmp(obs_data_get_string(settings, move_action->setting_name),
						   move_action->value_string) != 0) {
						obs_data_set_string(settings, move_action->setting_name, move_action->value_string);
						obs_source_update(target, settings);
					}
				} else if (move_action->setting_type == OBS_PROPERTY_INT ||
					   move_action->setting_type == OBS_PROPERTY_COLOR ||
					   move_action->setting_type == OBS_PROPERTY_COLOR_ALPHA) {
					if (obs_data_get_int(settings, move_action->setting_name) != move_action->value_int) {
						obs_data_set_int(settings, move_action->setting_name, move_action->value_int);
						obs_source_update(target, settings);
					}
				} else if (move_action->setting_type == OBS_PROPERTY_FLOAT) {
					if (obs_data_get_double(settings, move_action->setting_name) != move_action->value_float) {
						obs_data_set_double(settings, move_action->setting_name, move_action->value_float);
						obs_source_update(target, settings);
					}
				} else if (move_action->setting_type == OBS_PROPERTY_BUTTON) {
					obs_properties_t *props = obs_source_properties(target);
					obs_property_t *p = obs_properties_get(props, move_action->setting_name);
					if (p)
						obs_property_button_clicked(p, target);
					obs_properties_destroy(props);
				}
				obs_data_release(settings);
			}
			obs_source_release(source);
		}
	}
}

void move_action_hotkey_end(void *data)
{
	struct move_action_info *move_action = data;
	obs_hotkey_trigger_routed_callback(move_action->start_action.hotkey_id, move_action->move_filter.reverse);
}

void move_action_tick(void *data, float seconds)
{
	struct move_action_info *move_action = data;
	float t;
	if (move_action->duration_type == DURATION_TYPE_INFINITE) {
		const bool enabled = obs_source_enabled(move_action->move_filter.source);
		if (move_action->move_filter.enabled != enabled) {
			if (enabled && (move_action->move_filter.start_trigger == START_TRIGGER_ENABLE ||
					(move_action->move_filter.enabled_match_moving && !move_action->move_filter.moving)))
				move_filter_start(&move_action->move_filter);
			if (enabled && move_action->move_filter.stop_trigger == START_TRIGGER_ENABLE) {
				move_filter_stop(&move_action->move_filter);
				obs_queue_task(OBS_TASK_UI, move_action_execute, &move_action->end_action, false);
				move_filter_ended(&move_action->move_filter);
			}

			move_action->move_filter.enabled = enabled;
		}
		if (move_action->move_filter.enabled_match_moving && enabled != move_action->move_filter.moving) {
			if (enabled) {
				move_filter_start(&move_action->move_filter);
			} else {
				move_filter_stop(&move_action->move_filter);
				obs_queue_task(OBS_TASK_UI, move_action_execute, &move_action->end_action, false);
				move_filter_ended(&move_action->move_filter);
			}
		}
		move_action->move_filter.running_duration =
			(float)(move_action->move_filter.start_delay + move_action->move_filter.duration / 2) / 1000.0f;
		seconds = 0.0f;
	}
	if (!move_filter_tick(&move_action->move_filter, seconds, &t))
		return;
	if (move_action->start) {
		move_action->start = false;
		obs_queue_task(OBS_TASK_UI, move_action_execute, &move_action->start_action, false);
	}

	if (!move_action->move_filter.moving) {
		if ((move_action->start_action.action == MOVE_ACTION_SOURCE_HOTKEY ||
		     move_action->start_action.action == MOVE_ACTION_FRONTEND_HOTKEY) &&
		    move_action->start_action.hotkey_id != OBS_INVALID_HOTKEY_ID)
			obs_queue_task(OBS_TASK_UI, move_action_hotkey_end, data, false);
		obs_queue_task(OBS_TASK_UI, move_action_execute, &move_action->end_action, false);
		move_filter_ended(&move_action->move_filter);
	}
}

struct obs_source_info move_action_filter = {
	.id = MOVE_ACTION_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = move_action_get_name,
	.create = move_action_create,
	.destroy = move_action_destroy,
	.get_properties = move_action_properties,
	.get_defaults = move_filter_defaults,
	.update = move_action_update,
	.load = move_action_update,
	.video_tick = move_action_tick,
	.activate = move_filter_activate,
	.deactivate = move_filter_deactivate,
	.show = move_filter_show,
	.hide = move_filter_hide,
};

struct obs_source_info move_audio_action_filter = {
	.id = MOVE_AUDIO_ACTION_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = move_action_get_name,
	.create = move_action_create,
	.destroy = move_action_destroy,
	.get_properties = move_action_properties,
	.get_defaults = move_filter_defaults,
	.update = move_action_update,
	.load = move_action_update,
	.video_tick = move_action_tick,
	.activate = move_filter_activate,
	.deactivate = move_filter_deactivate,
	.show = move_filter_show,
	.hide = move_filter_hide,
};
