#include "move-transition.h"
#include "obs-frontend-api.h"

#define MOVE_ACTION_NONE 0
#define MOVE_ACTION_FRONTEND 1
#define MOVE_ACTION_SOURCE_HOTKEY 2
#define MOVE_ACTION_SOURCE_VISIBILITY 3
#define MOVE_ACTION_FILTER_ENABLE 4

#define MOVE_ACTION_ENABLE 0
#define MOVE_ACTION_DISABLE 1
#define MOVE_ACTION_TOGGLE 2

struct move_action_info {
	struct move_filter move_filter;

	char *scene_name;
	char *sceneitem_name;
	char *source_name;
	char *filter_name;
	char *hotkey_name;
	obs_hotkey_id hotkey_id;

	long long action;

	long long frontend_action;
	long long enable;

	bool start;
};

bool move_action_load_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *key)
{
	struct move_action_info *move_action = data;
	if (strcmp(move_action->hotkey_name, obs_hotkey_get_name(key)) != 0)
		return true;
	obs_hotkey_registerer_t type = obs_hotkey_get_registerer_type(key);
	if (type == OBS_HOTKEY_REGISTERER_SOURCE) {

		obs_weak_source_t *s = obs_hotkey_get_registerer(key);
		obs_source_t *source = obs_weak_source_get_source(s);
		if (source) {
			if (strcmp(obs_source_get_name(source),
				   move_action->source_name) == 0) {
				move_action->hotkey_id = id;
				obs_source_release(source);
				return false;
			}
			obs_source_release(source);
		}
	}
	return true;
}

void move_action_start_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey,
			     bool pressed)
{
	if (!pressed)
		return;
	struct move_action_info *move_action = data;

	move_filter_start_hotkey(&move_action->move_filter);

	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}


void move_action_update(void *data, obs_data_t *settings)
{
	struct move_action_info *move_action = data;
		move_filter_update(&move_action->move_filter, settings);
	obs_source_t *parent =
		obs_filter_get_parent(move_action->move_filter.source);
	if (parent &&
	    move_action->move_filter.move_start_hotkey ==
		    OBS_INVALID_HOTKEY_ID &&
	    move_action->move_filter.filter_name) {
		move_action->move_filter.move_start_hotkey =
			obs_hotkey_register_source(
				parent, move_action->move_filter.filter_name,
				move_action->move_filter.filter_name,
				move_action_start_hotkey, data);
	}
	bool changed = false;

	move_action->frontend_action =
		obs_data_get_int(settings, "frontend_action");
	const char *hotkey_name = obs_data_get_string(settings, "hotkey");
	long long action = obs_data_get_int(settings, "action");
	if (action == MOVE_ACTION_NONE && move_action->frontend_action != 0) {
		action = MOVE_ACTION_FRONTEND;
		obs_data_set_int(settings, "action", action);
	}
	if (action == MOVE_ACTION_NONE && strlen(hotkey_name)) {
		action = MOVE_ACTION_SOURCE_HOTKEY;
		obs_data_set_int(settings, "action", action);
	}

	if (move_action->action != action) {
		move_action->action = action;
		changed = true;
	}

	if (action == MOVE_ACTION_SOURCE_HOTKEY ||
	    action == MOVE_ACTION_FILTER_ENABLE) {
		const char *source_name =
			obs_data_get_string(settings, "source");
		if (!move_action->source_name ||
		    strcmp(source_name, move_action->source_name) != 0) {
			bfree(move_action->source_name);
			move_action->source_name = bstrdup(source_name);
			changed = true;
		}
	} else if (move_action->source_name) {
		bfree(move_action->source_name);
		move_action->source_name = NULL;
		changed = true;
	}

	if (action == MOVE_ACTION_SOURCE_HOTKEY) {
		if (!move_action->hotkey_name ||
		    strcmp(hotkey_name, move_action->hotkey_name) != 0) {
			bfree(move_action->hotkey_name);
			move_action->hotkey_name = bstrdup(hotkey_name);
			changed = true;
		}
	} else if (move_action->hotkey_name) {
		bfree(move_action->hotkey_name);
		move_action->hotkey_name = NULL;
		changed = true;
	}
	if (changed) {
		move_action->hotkey_id = OBS_INVALID_HOTKEY_ID;
		if (move_action->action == MOVE_ACTION_SOURCE_HOTKEY &&
		    move_action->hotkey_name &&
		    strlen(move_action->hotkey_name) &&
		    move_action->source_name &&
		    strlen(move_action->source_name))
			obs_enum_hotkeys(move_action_load_hotkey, move_action);
	}

	if (action == MOVE_ACTION_FILTER_ENABLE) {
		const char *filter_name =
			obs_data_get_string(settings, "filter");
		if (!move_action->filter_name ||
		    strcmp(filter_name, move_action->filter_name) != 0) {
			bfree(move_action->filter_name);
			move_action->filter_name = bstrdup(filter_name);
		}
	} else if (move_action->filter_name) {
		bfree(move_action->filter_name);
		move_action->filter_name = NULL;
	}

	if (action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *scene_name = obs_data_get_string(settings, "scene");
		if (!move_action->scene_name ||
		    strcmp(scene_name, move_action->scene_name) != 0) {
			bfree(move_action->scene_name);
			move_action->scene_name = bstrdup(scene_name);
		}
	} else if (move_action->scene_name) {
		bfree(move_action->scene_name);
		move_action->scene_name = NULL;
	}

	if (action == MOVE_ACTION_SOURCE_VISIBILITY) {
		const char *sceneitem_name =
			obs_data_get_string(settings, "sceneitem");
		if (!move_action->sceneitem_name ||
		    strcmp(sceneitem_name, move_action->sceneitem_name) != 0) {
			bfree(move_action->sceneitem_name);
			move_action->sceneitem_name = bstrdup(sceneitem_name);
		}
	} else if (move_action->sceneitem_name) {
		bfree(move_action->sceneitem_name);
		move_action->sceneitem_name = NULL;
	}

	move_action->enable = obs_data_get_int(settings, "enable");
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

static void *move_action_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_action_info *move_action =
		bzalloc(sizeof(struct move_action_info));
	move_filter_init(&move_action->move_filter, source, move_action_start);
	move_action->hotkey_id = OBS_INVALID_HOTKEY_ID;
	move_action_update(move_action, settings);
	return move_action;
}

static void move_action_destroy(void *data)
{
	struct move_action_info *move_action = data;
	move_filter_destroy(&move_action->move_filter);
	bfree(move_action->source_name);
	bfree(move_action->hotkey_name);
	bfree(move_action->scene_name);
	bfree(move_action->sceneitem_name);
	bfree(move_action->filter_name);
	bfree(move_action);
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
			obs_property_list_add_string(
				hd->hotkey_prop,
				obs_hotkey_get_description(key),
				obs_hotkey_get_name(key));
		}
	}
	return true;
}

static void add_filter_to_prop_list(obs_source_t *parent, obs_source_t *child,
				    void *data)
{
	UNUSED_PARAMETER(parent);
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(child);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
}

static bool move_action_source_changed(void *data, obs_properties_t *props,
				       obs_property_t *property,
				       obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *source_name = obs_data_get_string(settings, "source");
	obs_property_t *filter = obs_properties_get(props, "filter");
	obs_property_list_clear(filter);
	obs_source_t *source = obs_get_source_by_name(source_name);
	obs_source_enum_filters(source, add_filter_to_prop_list, filter);

	obs_property_t *hotkey = obs_properties_get(props, "hotkey");
	obs_property_list_clear(hotkey);
	obs_property_list_add_string(hotkey, "", "");
	struct hotkey_enum_add_data hd;
	hd.hotkey_prop = hotkey;
	hd.source = obs_source_get_weak_source(source);
	obs_source_release(source);
	obs_enum_hotkeys(add_source_hotkeys, &hd);
	obs_weak_source_release(hd.source);
	return true;
}

static bool add_source_to_prop_list(void *data, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(source);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
	return true;
}

static bool move_action_action_changed(obs_properties_t *props,
				       obs_property_t *property,
				       obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	long long action = obs_data_get_int(settings, "action");
	obs_property_t *scene = obs_properties_get(props, "scene");
	obs_property_t *sceneitem = obs_properties_get(props, "sceneitem");
	if (action == MOVE_ACTION_SOURCE_VISIBILITY) {
		obs_property_list_clear(scene);
		obs_enum_scenes(add_source_to_prop_list, scene);
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
	} else {
		obs_property_set_visible(scene, false);
		obs_property_set_visible(sceneitem, false);
	}
	obs_property_t *source = obs_properties_get(props, "source");
	obs_property_t *filter = obs_properties_get(props, "filter");
	obs_property_t *hotkey = obs_properties_get(props, "hotkey");
	if (action == MOVE_ACTION_FILTER_ENABLE ||
	    action == MOVE_ACTION_SOURCE_HOTKEY) {
		obs_property_list_clear(source);
		obs_enum_sources(add_source_to_prop_list, source);
		obs_enum_scenes(add_source_to_prop_list, source);
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter,
					 action == MOVE_ACTION_FILTER_ENABLE);
		obs_property_set_visible(hotkey,
					 action == MOVE_ACTION_SOURCE_HOTKEY);

	} else {
		obs_property_set_visible(source, false);
		obs_property_set_visible(filter, false);
		obs_property_set_visible(hotkey, false);
	}
	obs_property_t *frontend_action =
		obs_properties_get(props, "frontend_action");
	obs_property_set_visible(frontend_action,
				 action == MOVE_ACTION_FRONTEND);

	obs_property_t *enable = obs_properties_get(props, "enable");
	obs_property_set_visible(
		enable, action == MOVE_ACTION_FILTER_ENABLE ||
				action == MOVE_ACTION_SOURCE_VISIBILITY);
	return true;
}

static bool add_sceneitem_to_prop_list(obs_scene_t *scene,
				       obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	obs_property_t *p = (obs_property_t *)data;
	const obs_source_t *source = obs_sceneitem_get_source(item);
	const char *name = obs_source_get_name(source);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
	return true;
}

static bool move_action_scene_changed(void *data, obs_properties_t *props,
				      obs_property_t *property,
				      obs_data_t *settings)
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
		return true;
	obs_scene_enum_items(scene, add_sceneitem_to_prop_list, sceneitem);
	return true;
}

static obs_properties_t *move_action_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(ppts, "action",
						    obs_module_text("Action"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("FrontendAction"),
				  MOVE_ACTION_FRONTEND);

	obs_property_list_add_int(p, obs_module_text("SourceVisibility"),
				  MOVE_ACTION_SOURCE_VISIBILITY);
	obs_property_list_add_int(p, obs_module_text("SourceHotkey"),
				  MOVE_ACTION_SOURCE_HOTKEY);
	obs_property_list_add_int(p, obs_module_text("FilterEnable"),
				  MOVE_ACTION_FILTER_ENABLE);
	obs_property_set_modified_callback(p, move_action_action_changed);

	p = obs_properties_add_list(ppts, "scene", obs_module_text("Scene"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(p, move_action_scene_changed, data);

	p = obs_properties_add_list(ppts, "sceneitem",
				    obs_module_text("Source"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(ppts, "source", obs_module_text("Source"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_source_changed,
					    data);

	p = obs_properties_add_list(ppts, "filter", obs_module_text("Filter"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, move_action_source_changed,
					    data);

	p = obs_properties_add_list(ppts, "enable", obs_module_text("Enable"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Enable"),
				  MOVE_ACTION_ENABLE);
	obs_property_list_add_int(p, obs_module_text("Disable"),
				  MOVE_ACTION_DISABLE);
	obs_property_list_add_int(p, obs_module_text("Toggle"),
				  MOVE_ACTION_TOGGLE);

	p = obs_properties_add_list(ppts, "hotkey", obs_module_text("Hotkey"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(ppts, "frontend_action",
				    obs_module_text("FrontendAction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("FrontendAction.None"), 0);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.StreamingStart"), 1);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.StreamingStop"), 2);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.RecordingStart"), 3);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.RecordingStop"), 4);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.RecordingPause"), 5);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.RecordingUnpause"), 6);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.VirtualCamStart"), 7);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.VirtualCamStop"), 8);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.ReplayBufferStart"), 9);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.ReplayBufferStop"), 10);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.ReplayBufferSave"), 11);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.StudioModeEnable"), 12);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.StudioModeDisable"), 13);
	obs_property_list_add_int(
		p, obs_module_text("FrontendAction.TakeScreenshot"), 14);

	//obs_frontend_preview_program_trigger_transition

	//obs_frontend_take_source_screenshot
	//obs_frontend_open_filters
	//obs_frontend_open_properties

	move_filter_properties(data, ppts);
	return ppts;
}

void move_action_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, S_ENABLED_MATCH_MOVING, true);
	obs_data_set_default_int(settings, S_DURATION, 300);
}

void move_action_tick(void *data, float seconds)
{
	struct move_action_info *move_action = data;
	if (move_action->move_filter.filter_name &&
	    move_action->move_filter.move_start_hotkey ==
		    OBS_INVALID_HOTKEY_ID) {
		obs_source_t *parent =
			obs_filter_get_parent(move_action->move_filter.source);
		if (parent)
			move_action->move_filter.move_start_hotkey =
				obs_hotkey_register_source(
					parent,
					move_action->move_filter.filter_name,
					move_action->move_filter.filter_name,
					move_action_start_hotkey, data);
	}

	float t;
	if (!move_filter_tick(&move_action->move_filter, seconds, &t))
		return;
	if (move_action->start) {
		move_action->start = false;
		if (move_action->action == MOVE_ACTION_SOURCE_HOTKEY) {
			if (move_action->hotkey_id == OBS_INVALID_HOTKEY_ID &&
			    move_action->hotkey_name &&
			    strlen(move_action->hotkey_name) &&
			    move_action->source_name &&
			    strlen(move_action->source_name)) {
				obs_enum_hotkeys(move_action_load_hotkey,
						 move_action);
			}
			if (move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
				obs_hotkey_trigger_routed_callback(
					move_action->hotkey_id,
					!move_action->move_filter.reverse);
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
		} else if (move_action->action ==
			   MOVE_ACTION_SOURCE_VISIBILITY) {
			if (move_action->scene_name &&
			    move_action->sceneitem_name &&
			    strlen(move_action->scene_name) &&
			    strlen(move_action->sceneitem_name)) {
				obs_source_t *scene_source =
					obs_get_source_by_name(
						move_action->scene_name);
				obs_scene_t *scene =
					obs_scene_from_source(scene_source);
				obs_sceneitem_t *item = obs_scene_find_source(
					scene, move_action->sceneitem_name);
				if (item) {

					if (move_action->enable ==
					    MOVE_ACTION_TOGGLE) {
						obs_sceneitem_set_visible(
							item,
							!obs_sceneitem_visible(
								item));
					} else if (move_action->enable ==
						   MOVE_ACTION_ENABLE) {
						if (!obs_sceneitem_visible(
							    item))
							obs_sceneitem_set_visible(
								item, true);
					} else if (move_action->enable ==
						   MOVE_ACTION_DISABLE) {
						if (obs_sceneitem_visible(item))
							obs_sceneitem_set_visible(
								item, false);
					}
					obs_sceneitem_release(item);
				}
				obs_source_release(scene_source);
			}
		} else if (move_action->action == MOVE_ACTION_FILTER_ENABLE) {
			if (move_action->source_name &&
			    move_action->filter_name &&
			    strlen(move_action->source_name) &&
			    strlen(move_action->filter_name)) {
				obs_source_t *source = obs_get_source_by_name(
					move_action->source_name);
				obs_source_t *filter =
					source ? obs_source_get_filter_by_name(
							 source,
							 move_action
								 ->filter_name)
					       : NULL;
				if (filter) {

					if (move_action->enable ==
					    MOVE_ACTION_TOGGLE) {
						obs_source_set_enabled(
							filter,
							!obs_source_enabled(
								filter));
					} else if (move_action->enable ==
						   MOVE_ACTION_ENABLE) {
						if (!obs_source_enabled(filter))
							obs_source_set_enabled(
								filter, true);
					} else if (move_action->enable ==
						   MOVE_ACTION_DISABLE) {
						if (obs_source_enabled(filter))
							obs_source_set_enabled(
								filter, false);
					}
					obs_source_release(filter);
				}
				obs_source_release(source);
			}
		}
	}

	if (!move_action->move_filter.moving) {
		if (move_action->action == MOVE_ACTION_SOURCE_HOTKEY &&
		    move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_trigger_routed_callback(
				move_action->hotkey_id,
				move_action->move_filter.reverse);
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
	.get_defaults = move_action_defaults,
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
	.get_defaults = move_action_defaults,
	.update = move_action_update,
	.load = move_action_update,
	.video_tick = move_action_tick,
	.activate = move_filter_activate,
	.deactivate = move_filter_deactivate,
	.show = move_filter_show,
	.hide = move_filter_hide,
};
