#include "move-transition.h"
#include "obs-frontend-api.h"

struct move_action_info {
	struct move_filter move_filter;

	char *source_name;
	char *hotkey_name;
	obs_hotkey_id hotkey_id;

	long long frontend_action;
};

bool load_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *key)
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

void move_action_update(void *data, obs_data_t *settings)
{
	struct move_action_info *move_action = data;
	move_filter_update(&move_action->move_filter, settings);
	bool changed = false;
	const char *source_name = obs_data_get_string(settings, "source");
	if (!move_action->source_name ||
	    strcmp(source_name, move_action->source_name) != 0) {
		bfree(move_action->source_name);
		move_action->source_name = bstrdup(source_name);
		changed = true;
	}

	const char *hotkey_name = obs_data_get_string(settings, "hotkey");
	if (!move_action->hotkey_name ||
	    strcmp(hotkey_name, move_action->hotkey_name) != 0) {
		bfree(move_action->hotkey_name);
		move_action->hotkey_name = bstrdup(hotkey_name);
		changed = true;
	}
	if (changed) {
		move_action->hotkey_id = OBS_INVALID_HOTKEY_ID;
		if (move_action->hotkey_name &&
		    strlen(move_action->hotkey_name) &&
		    move_action->source_name &&
		    strlen(move_action->source_name))
			obs_enum_hotkeys(load_hotkey, move_action);
	}

	move_action->frontend_action =
		obs_data_get_int(settings, "frontend_action");
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

	if (move_action->hotkey_id == OBS_INVALID_HOTKEY_ID &&
	    move_action->hotkey_name && strlen(move_action->hotkey_name) &&
	    move_action->source_name && strlen(move_action->source_name)) {
		obs_enum_hotkeys(load_hotkey, move_action);
	}
	if (move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_trigger_routed_callback(
			move_action->hotkey_id,
			!move_action->move_filter.reverse);

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
	bfree(move_action);
}
struct hotkey_enum_add_data {
	obs_source_t *source;
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
		if (obs_weak_source_references_source(s, hd->source)) {
			obs_property_list_add_string(
				hd->hotkey_prop,
				obs_hotkey_get_description(key),
				obs_hotkey_get_name(key));
		}
	}
	return true;
}

static bool move_action_source_changed(void *data, obs_properties_t *props,
				       obs_property_t *property,
				       obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(property);
	const char *source_name = obs_data_get_string(settings, "source");
	obs_property_t *hotkey = obs_properties_get(props, "hotkey");
	obs_property_list_clear(hotkey);
	obs_property_list_add_string(hotkey, "", "");
	obs_source_t *source = obs_get_source_by_name(source_name);
	obs_source_release(source);
	struct hotkey_enum_add_data hd;
	hd.hotkey_prop = hotkey;
	hd.source = source;
	obs_enum_hotkeys(add_source_hotkeys, &hd);
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

static obs_properties_t *move_action_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_properties_t *hg = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(hg, "source",
						    obs_module_text("Source"),
						    OBS_COMBO_TYPE_EDITABLE,
						    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "", "");
	obs_enum_sources(add_source_to_prop_list, p);

	obs_property_set_modified_callback2(p, move_action_source_changed,
					    data);
	p = obs_properties_add_list(hg, "hotkey", obs_module_text("Hotkey"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_properties_add_group(ppts, "hotkey_group",
				 obs_module_text("TriggerHotkey"),
				 OBS_GROUP_NORMAL, hg);

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
}

void move_action_tick(void *data, float seconds)
{
	struct move_action_info *move_action = data;

	float t;
	if (!move_filter_tick(&move_action->move_filter, seconds, &t))
		return;

	if (!move_action->move_filter.moving) {
		if (move_action->hotkey_id != OBS_INVALID_HOTKEY_ID)
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
