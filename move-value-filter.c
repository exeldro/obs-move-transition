#include "move-transition.h"
#include <obs-module.h>
#include <util/dstr.h>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

static void load_properties(obs_properties_t *props_from,
			    obs_data_array_t *array, obs_data_t *settings_to,
			    obs_data_t *settings_from)
{
	obs_property_t *prop_from = obs_properties_first(props_from);
	for (; prop_from != NULL; obs_property_next(&prop_from)) {
		const char *name = obs_property_name(prop_from);
		if (!obs_property_visible(prop_from))
			continue;

		obs_data_t *setting = NULL;
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item2 = obs_data_array_item(array, i);
			const char *setting_name2 =
				obs_data_get_string(item2, S_SETTING_NAME);
			if (strcmp(setting_name2, name) == 0) {
				setting = item2;
			}
		}

		const enum obs_property_type prop_type =
			obs_property_get_type(prop_from);
		if (prop_type == OBS_PROPERTY_GROUP) {
			load_properties(obs_property_group_content(prop_from),
					array, settings_to, settings_from);
		} else if (prop_type == OBS_PROPERTY_INT) {
			if (!setting) {
				setting = obs_data_create();
				obs_data_set_string(setting, S_SETTING_NAME,
						    name);
				obs_data_array_push_back(array, setting);
			}
			obs_data_set_int(setting, S_VALUE_TYPE, MOVE_VALUE_INT);
			const long long to =
				obs_data_get_int(settings_to, name);
			obs_data_set_int(setting, S_SETTING_TO, to);
			const long long from =
				obs_data_get_int(settings_from, name);
			obs_data_set_int(setting, S_SETTING_FROM, from);
		} else if (prop_type == OBS_PROPERTY_FLOAT) {
			if (!setting) {
				setting = obs_data_create();
				obs_data_set_string(setting, S_SETTING_NAME,
						    name);
				obs_data_array_push_back(array, setting);
			}
			obs_data_set_int(setting, S_VALUE_TYPE,
					 MOVE_VALUE_FLOAT);
			const double to =
				obs_data_get_double(settings_to, name);
			obs_data_set_double(setting, S_SETTING_TO, to);
			const double from =
				obs_data_get_double(settings_from, name);
			obs_data_set_double(setting, S_SETTING_FROM, from);
		} else if (prop_type == OBS_PROPERTY_COLOR) {
			if (!setting) {
				setting = obs_data_create();
				obs_data_set_string(setting, S_SETTING_NAME,
						    name);
				obs_data_array_push_back(array, setting);
			}
			obs_data_set_int(setting, S_VALUE_TYPE,
					 MOVE_VALUE_COLOR);
			obs_data_set_int(setting, S_SETTING_TO,
					 obs_data_get_int(settings_to, name));
			const long long from =
				obs_data_get_int(settings_from, name);
			obs_data_set_int(setting, S_SETTING_FROM, from);
		}
	}
}

void move_values_load_properties(struct move_value_info *move_value,
				 obs_source_t *source, obs_data_t *settings)
{
	if (source && source != move_value->source) {
		obs_properties_t *sps = obs_source_properties(source);
		size_t index = 0;
		while (index < obs_data_array_count(move_value->settings)) {
			obs_data_t *item = obs_data_array_item(
				move_value->settings, index);
			const char *setting_name =
				obs_data_get_string(item, S_SETTING_NAME);
			if (obs_properties_get(sps, setting_name) == NULL) {
				obs_data_array_erase(move_value->settings,
						     index);
			} else {
				index++;
			}
		}
		obs_data_t *data_from = obs_source_get_settings(source);
		load_properties(sps, move_value->settings, settings, data_from);
		obs_data_release(data_from);
		obs_properties_destroy(sps);
	} else {
		while (obs_data_array_count(move_value->settings)) {
			obs_data_array_erase(move_value->settings, 0);
		}
	}
}

void move_value_start(struct move_value_info *move_value)
{
	if (!move_value->filter && move_value->setting_filter_name &&
	    strlen(move_value->setting_filter_name)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_value->source);
		if (parent) {
			move_value->filter = obs_source_get_filter_by_name(
				parent, move_value->setting_filter_name);
		} else {
			return;
		}
	}
	if (move_value->reverse) {
		move_value->running_duration = 0.0f;
		move_value->moving = true;
		return;
	}
	if (!move_value->custom_duration)
		move_value->duration = obs_frontend_get_transition_duration();
	if (move_value->moving && obs_source_enabled(move_value->source)) {
		if (move_value->next_move_on == NEXT_MOVE_ON_HOTKEY &&
		    move_value->next_move_name &&
		    strcmp(move_value->next_move_name, NEXT_MOVE_REVERSE) ==
			    0) {
			move_value->reverse = !move_value->reverse;
			move_value->running_duration =
				(float)(move_value->duration +
					move_value->start_delay +
					move_value->end_delay) /
					1000.0f -
				move_value->running_duration;
		}
		return;
	}
	if (!move_value->setting_filter_name) {
		obs_source_update(move_value->source, NULL);
	}
	obs_source_t *source =
		move_value->setting_filter_name &&
				strlen(move_value->setting_filter_name)
			? move_value->filter
			: obs_filter_get_parent(move_value->source);
	obs_data_t *ss = obs_source_get_settings(source);
	if (move_value->settings) {
		obs_data_t *settings =
			obs_source_get_settings(move_value->source);
		move_values_load_properties(move_value, source, settings);
		obs_data_release(settings);
		move_value->running_duration = 0.0f;
		move_value->moving = true;
	} else if (move_value->value_type == MOVE_VALUE_INT) {
		move_value->int_from =
			obs_data_get_int(ss, move_value->setting_name);
		if (move_value->int_from != move_value->int_to) {
			move_value->running_duration = 0.0f;
			move_value->moving = true;
		}
	} else if (move_value->value_type == MOVE_VALUE_FLOAT) {
		move_value->double_from =
			obs_data_get_double(ss, move_value->setting_name);
		if (move_value->double_from != move_value->double_to) {
			move_value->running_duration = 0.0f;
			move_value->moving = true;
		}
	} else if (move_value->value_type == MOVE_VALUE_COLOR) {
		vec4_from_rgba(&move_value->color_from,
			       (uint32_t)obs_data_get_int(
				       ss, move_value->setting_name));
		if (move_value->color_from.x != move_value->color_to.x ||
		    move_value->color_from.y != move_value->color_to.y ||
		    move_value->color_from.z != move_value->color_to.z ||
		    move_value->color_from.w != move_value->color_to.w) {
			move_value->running_duration = 0.0f;
			move_value->moving = true;
		}
	} else {
		move_value->int_from =
			obs_data_get_int(ss, move_value->setting_name);
		move_value->double_from =
			obs_data_get_double(ss, move_value->setting_name);
		if (move_value->int_from != move_value->int_to ||
		    move_value->double_from != move_value->double_to) {
			move_value->running_duration = 0.0f;
			move_value->moving = true;
		}
	}
	if (move_value->enabled_match_moving &&
	    obs_source_enabled(move_value->source) != move_value->moving) {
		move_value->enabled = move_value->moving;
		obs_source_set_enabled(move_value->source, move_value->moving);
	}
	obs_data_release(ss);
	if (move_value->moving && move_value->simultaneous_move_name &&
	    strlen(move_value->simultaneous_move_name) &&
	    (!move_value->filter_name ||
	     strcmp(move_value->filter_name,
		    move_value->simultaneous_move_name) != 0)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_value->source);
		if (parent) {
			obs_source_t *filter = obs_source_get_filter_by_name(
				parent, move_value->simultaneous_move_name);
			if (filter &&
			    (strcmp(obs_source_get_unversioned_id(filter),
				    MOVE_VALUE_FILTER_ID) == 0 ||
			     strcmp(obs_source_get_unversioned_id(filter),
				    MOVE_AUDIO_VALUE_FILTER_ID) == 0)) {
				struct move_value_info *filter_data =
					obs_obj_get_data(filter);
				if (!filter_data->moving) {
					move_value_start(filter_data);
				}
			}
		}
	}
}

bool move_value_start_button(obs_properties_t *props, obs_property_t *property,
			     void *data)
{
	struct move_value_info *move_value = data;
	move_value_start(move_value);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

void move_value_start_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey,
			     bool pressed)
{
	if (!pressed)
		return;
	struct move_value_info *move_value = data;
	if (move_value->next_move_on != NEXT_MOVE_ON_HOTKEY ||
	    !move_value->next_move_name ||
	    !strlen(move_value->next_move_name)) {
		move_value_start(move_value);
		return;
	}
	if (!move_value->filters_done.num) {
		move_value_start(move_value);
		da_push_back(move_value->filters_done, &move_value->source);
		return;
	}
	obs_source_t *parent = obs_filter_get_parent(move_value->source);
	if (!parent)
		return;

	struct move_value_info *filter_data = move_value;
	size_t i = 0;
	while (i < move_value->filters_done.num) {
		if (!filter_data->next_move_name ||
		    !strlen(filter_data->next_move_name)) {
			move_value_start(move_value);
			move_value->filters_done.num = 0;
			da_push_back(move_value->filters_done,
				     &move_value->source);
			return;
		}
		obs_source_t *filter = obs_source_get_filter_by_name(
			parent, filter_data->next_move_name);
		if (!filter || (strcmp(obs_source_get_unversioned_id(filter),
				       MOVE_VALUE_FILTER_ID) != 0 &&
				strcmp(obs_source_get_unversioned_id(filter),
				       MOVE_AUDIO_VALUE_FILTER_ID) != 0)) {
			obs_source_release(filter);
			move_value_start(move_value);
			move_value->filters_done.num = 0;
			da_push_back(move_value->filters_done,
				     &move_value->source);
			return;
		}
		if (filter_data->moving &&
		    obs_source_enabled(filter_data->source) &&
		    (filter_data->reverse || !filter_data->next_move_name ||
		     strcmp(filter_data->next_move_name, NEXT_MOVE_REVERSE) !=
			     0)) {
			filter_data->moving = false;
			if (filter_data->enabled_match_moving)
				obs_source_set_enabled(filter_data->source,
						       false);
		}
		if (filter_data->next_move_on != NEXT_MOVE_ON_HOTKEY) {
			filter_data = obs_obj_get_data(filter);
			da_push_back(move_value->filters_done,
				     &filter_data->source);

		} else {
			filter_data = obs_obj_get_data(filter);
		}
		obs_source_release(filter);
		i++;
	}
	for (i = 0; i < move_value->filters_done.num; i++) {
		if (move_value->filters_done.array[i] == filter_data->source) {
			move_value_start(move_value);
			move_value->filters_done.num = 0;
			da_push_back(move_value->filters_done,
				     &move_value->source);
			return;
		}
	}
	move_value_start(filter_data);
	da_push_back(move_value->filters_done, &filter_data->source);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}

void move_value_update(void *data, obs_data_t *settings)
{
	struct move_value_info *move_value = data;
	obs_source_t *parent = obs_filter_get_parent(move_value->source);

	const char *filter_name = obs_source_get_name(move_value->source);
	if (!move_value->filter_name ||
	    strcmp(move_value->filter_name, filter_name) != 0) {
		bfree(move_value->filter_name);
		move_value->filter_name = NULL;
		if (move_value->move_start_hotkey != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(move_value->move_start_hotkey);
			move_value->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
		}
		if (parent) {
			move_value->filter_name = bstrdup(filter_name);
			move_value->move_start_hotkey =
				obs_hotkey_register_source(
					parent, filter_name, filter_name,
					move_value_start_hotkey, data);
		}
	}

	const char *setting_filter_name =
		obs_data_get_string(settings, S_FILTER);
	if (!move_value->setting_filter_name ||
	    strcmp(move_value->setting_filter_name, setting_filter_name) != 0) {
		obs_source_release(move_value->filter);
		move_value->filter = NULL;
		if (parent) {
			bfree(move_value->setting_filter_name);
			move_value->setting_filter_name =
				bstrdup(setting_filter_name);
			move_value->filter = obs_source_get_filter_by_name(
				parent, setting_filter_name);
		}
	}

	const char *setting_name =
		obs_data_get_string(settings, S_SETTING_NAME);
	if (!move_value->setting_name ||
	    strcmp(move_value->setting_name, setting_name) != 0) {
		bfree(move_value->setting_name);

		move_value->setting_name = bstrdup(setting_name);
	}
	if (obs_data_get_bool(settings, S_SINGLE_SETTING)) {
		obs_data_array_release(move_value->settings);
		move_value->settings = NULL;
	} else {
		if (!move_value->settings)
			move_value->settings = obs_data_array_create();
		obs_source_t *source =
			move_value->setting_filter_name &&
					strlen(move_value->setting_filter_name)
				? move_value->filter
				: obs_filter_get_parent(move_value->source);
		move_values_load_properties(move_value, source, settings);
	}

	move_value->value_type = obs_data_get_int(settings, S_VALUE_TYPE);
	move_value->int_to = obs_data_get_int(settings, S_SETTING_INT);
	move_value->double_to = obs_data_get_double(settings, S_SETTING_FLOAT);
	vec4_from_rgba(&move_value->color_to,
		       (uint32_t)obs_data_get_int(settings, S_SETTING_COLOR));

	move_value->custom_duration =
		obs_data_get_bool(settings, S_CUSTOM_DURATION);
	if (move_value->custom_duration)
		move_value->duration = obs_data_get_int(settings, S_DURATION);
	move_value->start_delay = obs_data_get_int(settings, S_START_DELAY);
	move_value->end_delay = obs_data_get_int(settings, S_END_DELAY);
	move_value->easing = obs_data_get_int(settings, S_EASING_MATCH);
	move_value->easing_function =
		obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move_value->enabled_match_moving =
		obs_data_get_bool(settings, S_ENABLED_MATCH_MOVING);
	if (move_value->enabled_match_moving && !move_value->moving &&
	    obs_source_enabled(move_value->source))
		move_value_start(move_value);
	move_value->start_trigger =
		(uint32_t)obs_data_get_int(settings, S_START_TRIGGER);
	move_value->stop_trigger =
		(uint32_t)obs_data_get_int(settings, S_STOP_TRIGGER);

	const char *simultaneous_move_name =
		obs_data_get_string(settings, S_SIMULTANEOUS_MOVE);
	if (!move_value->simultaneous_move_name ||
	    strcmp(move_value->simultaneous_move_name,
		   simultaneous_move_name) != 0) {
		bfree(move_value->simultaneous_move_name);
		move_value->simultaneous_move_name =
			bstrdup(simultaneous_move_name);
	}

	const char *next_move_name = obs_data_get_string(settings, S_NEXT_MOVE);
	if (!move_value->next_move_name ||
	    strcmp(move_value->next_move_name, next_move_name) != 0) {
		bfree(move_value->next_move_name);
		move_value->next_move_name = bstrdup(next_move_name);
		move_value->reverse = false;
	}
	move_value->next_move_on = obs_data_get_int(settings, S_NEXT_MOVE_ON);
	if (move_value->start_trigger == START_TRIGGER_LOAD) {
		move_value_start(move_value);
	}
}

static void *move_value_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_value_info *move_value =
		bzalloc(sizeof(struct move_value_info));
	move_value->source = source;
	move_value->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
	move_value_update(move_value, settings);
	return move_value;
}

static void move_value_destroy(void *data)
{
	struct move_value_info *move_value = data;
	obs_source_release(move_value->filter);
	move_value->filter = NULL;
	if (move_value->move_start_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(move_value->move_start_hotkey);
	bfree(move_value->filter_name);
	bfree(move_value->setting_filter_name);
	bfree(move_value->setting_name);
	bfree(move_value->simultaneous_move_name);
	bfree(move_value->next_move_name);
	obs_data_array_release(move_value->settings);
	da_free(move_value->filters_done);
	bfree(move_value);
}

void prop_list_add_easings(obs_property_t *p);
void prop_list_add_easing_functions(obs_property_t *p);

void prop_list_add_filter(obs_source_t *parent, obs_source_t *child, void *data)
{
	UNUSED_PARAMETER(parent);
	obs_property_t *p = data;
	const char *name = obs_source_get_name(child);
	obs_property_list_add_string(p, name, name);
}

void prop_list_add_move_value_filter(obs_source_t *parent, obs_source_t *child,
				     void *data)
{
	UNUSED_PARAMETER(parent);
	if (strcmp(obs_source_get_unversioned_id(child),
		   MOVE_VALUE_FILTER_ID) != 0 &&
	    strcmp(obs_source_get_unversioned_id(child),
		   MOVE_AUDIO_VALUE_FILTER_ID) != 0)
		return;
	obs_property_t *p = data;
	const char *name = obs_source_get_name(child);
	obs_property_list_add_string(p, name, name);
}

bool move_value_get_value(obs_properties_t *props, obs_property_t *property,
			  void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct move_value_info *move_value = data;
	bool settings_changed = false;
	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	if (source == NULL || source == move_value->source)
		return settings_changed;
	obs_properties_t *sps = obs_source_properties(source);
	obs_property_t *sp = obs_properties_get(sps, move_value->setting_name);

	obs_data_t *ss = obs_source_get_settings(source);

	const enum obs_property_type prop_type = obs_property_get_type(sp);
	obs_data_t *settings = obs_source_get_settings(move_value->source);
	if (prop_type == OBS_PROPERTY_INT) {
		const long long value =
			obs_data_get_int(ss, move_value->setting_name);
		obs_data_set_int(settings, S_SETTING_INT, value);
		settings_changed = true;
	} else if (prop_type == OBS_PROPERTY_FLOAT) {
		const double value =
			obs_data_get_double(ss, move_value->setting_name);
		obs_data_set_double(settings, S_SETTING_FLOAT, value);
		settings_changed = true;
	} else if (prop_type == OBS_PROPERTY_COLOR) {
		const long long color =
			obs_data_get_int(ss, move_value->setting_name);
		obs_data_set_int(settings, S_SETTING_COLOR, color);
		settings_changed = true;
	}
	obs_data_release(settings);
	obs_properties_destroy(sps);
	return settings_changed;
}

bool move_value_get_values(obs_properties_t *props, obs_property_t *property,
			   void *data)
{
	struct move_value_info *move_value = data;
	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	if (source == NULL || source == move_value->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(move_value->source);
	obs_data_t *ss = obs_source_get_settings(source);

	const size_t count = obs_data_array_count(move_value->settings);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(move_value->settings, i);
		const char *name = obs_data_get_string(item, S_SETTING_NAME);
		const long long value_type =
			obs_data_get_int(item, S_VALUE_TYPE);
		if (value_type == MOVE_VALUE_INT) {
			const long long value = obs_data_get_int(ss, name);
			obs_data_set_int(settings, name, value);
		} else if (value_type == MOVE_VALUE_FLOAT) {
			const double value = obs_data_get_double(ss, name);
			obs_data_set_double(settings, name, value);
		} else if (value_type == MOVE_VALUE_COLOR) {
			const long long color = obs_data_get_int(ss, name);
			obs_data_set_int(settings, name, color);
		}
	}

	if (count > 0) {
		obs_properties_t *sps = obs_source_properties(source);
		load_properties(sps, move_value->settings, settings, ss);
		obs_properties_destroy(sps);
	}
	obs_data_release(ss);
	obs_data_release(settings);
	return count > 0;
}

void copy_properties(obs_properties_t *props_from, obs_properties_t *props_to,
		     obs_data_t *data_from, obs_data_t *data_to,
		     obs_property_t *setting_list)
{
	obs_property_t *prop_from = obs_properties_first(props_from);
	for (; prop_from != NULL; obs_property_next(&prop_from)) {
		const char *name = obs_property_name(prop_from);
		const char *description = obs_property_description(prop_from);
		if (!obs_property_visible(prop_from))
			continue;
		obs_property_t *prop_to = NULL;
		const enum obs_property_type prop_type =
			obs_property_get_type(prop_from);
		if (prop_type == OBS_PROPERTY_GROUP) {
			obs_properties_t *group_to = obs_properties_create();
			copy_properties(obs_property_group_content(prop_from),
					group_to, data_from, data_to,
					setting_list);
			if (obs_properties_first(group_to) == NULL) {
				obs_properties_destroy(group_to);
			} else {
				prop_to = obs_properties_add_group(
					props_to, name, description,
					obs_property_group_type(prop_from),
					group_to);
			}

		} else if (prop_type == OBS_PROPERTY_INT) {
			obs_property_list_add_string(setting_list, description,
						     name);
			if (obs_property_int_type(prop_from) ==
			    OBS_NUMBER_SLIDER) {
				prop_to = obs_properties_add_int_slider(
					props_to, name, description,
					obs_property_int_min(prop_from),
					obs_property_int_max(prop_from),
					obs_property_int_step(prop_from));
			} else {
				prop_to = obs_properties_add_int(
					props_to, name, description,
					obs_property_int_min(prop_from),
					obs_property_int_max(prop_from),
					obs_property_int_step(prop_from));
			}
			if (obs_data_has_default_value(data_from, name))
				obs_data_set_default_int(
					data_to, name,
					obs_data_get_default_int(data_from,
								 name));
			obs_property_int_set_suffix(
				prop_to, obs_property_int_suffix(prop_from));

		} else if (prop_type == OBS_PROPERTY_FLOAT) {
			obs_property_list_add_string(setting_list, description,
						     name);
			if (obs_property_float_type(prop_from) ==
			    OBS_NUMBER_SLIDER) {
				prop_to = obs_properties_add_float_slider(
					props_to, name, description,
					obs_property_float_min(prop_from),
					obs_property_float_max(prop_from),
					obs_property_float_step(prop_from));
			} else {
				prop_to = obs_properties_add_float(
					props_to, name, description,
					obs_property_float_min(prop_from),
					obs_property_float_max(prop_from),
					obs_property_float_step(prop_from));
			}
			if (obs_data_has_default_value(data_from, name))
				obs_data_set_default_double(
					data_to, name,
					obs_data_get_default_double(data_from,
								    name));
			obs_property_float_set_suffix(
				prop_to, obs_property_float_suffix(prop_from));
		} else if (prop_type == OBS_PROPERTY_COLOR) {
			obs_property_list_add_string(setting_list, description,
						     name);
			prop_to = obs_properties_add_color(props_to, name,
							   description);
			if (obs_data_has_default_value(data_from, name))
				obs_data_set_default_int(
					data_to, name,
					obs_data_get_default_int(data_from,
								 name));
		}
	}
}

bool move_value_filter_changed(void *data, obs_properties_t *props,
			       obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	struct move_value_info *move_value = data;
	bool refresh = false;

	obs_source_t *parent = obs_filter_get_parent(move_value->source);

	const char *filter_name = obs_data_get_string(settings, S_FILTER);
	if (!move_value->setting_filter_name ||
	    strcmp(move_value->setting_filter_name, filter_name) != 0 ||
	    (!move_value->filter && strlen(filter_name))) {
		bfree(move_value->setting_filter_name);
		move_value->setting_filter_name = bstrdup(filter_name);
		obs_source_release(move_value->filter);
		move_value->filter =
			obs_source_get_filter_by_name(parent, filter_name);
	}

	refresh = true;
	obs_property_t *p = obs_properties_get(props, S_SETTING_NAME);
	obs_property_list_clear(p);
	obs_property_list_add_string(p, obs_module_text("Setting.None"), "");

	obs_property_t *ps = obs_properties_get(props, S_SETTINGS);
	obs_properties_t *g = obs_property_group_content(ps);
	obs_property_t *i = obs_properties_first(g);
	while (i != NULL) {
		const char *name = obs_property_name(i);
		obs_property_next(&i);
		if (strcmp(name, "values_get") == 0)
			continue;
		obs_properties_remove_by_name(g, name);
	}

	obs_source_t *source = move_value->filter ? move_value->filter : parent;
	obs_data_t *s = obs_source_get_settings(source);
	if (!s || source == move_value->source)
		return refresh;

	obs_properties_t *sps = obs_source_properties(source);
	copy_properties(sps, g, s, settings, p);
	obs_properties_destroy(sps);

	obs_data_release(s);
	return refresh;
}

bool move_value_single_setting_changed(void *data, obs_properties_t *props,
				       obs_property_t *property,
				       obs_data_t *settings)
{
	bool refresh = false;
	const bool single_setting =
		obs_data_get_bool(settings, S_SINGLE_SETTING);
	obs_property_t *p = obs_properties_get(props, S_SETTING_VALUE);
	if (obs_property_visible(p) != single_setting) {
		obs_property_set_visible(p, single_setting);
		refresh = true;
	}
	p = obs_properties_get(props, S_SETTINGS);
	if (obs_property_visible(p) == single_setting) {
		obs_property_set_visible(p, !single_setting);
		refresh = true;
	}
	return refresh;
}

bool move_value_setting_changed(void *data, obs_properties_t *props,
				obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	struct move_value_info *move_value = data;
	bool refresh = false;

	const char *setting_name =
		obs_data_get_string(settings, S_SETTING_NAME);
	if (!move_value->setting_name ||
	    strcmp(move_value->setting_name, setting_name) != 0) {
		refresh = true;

		bfree(move_value->setting_name);
		move_value->setting_name = bstrdup(setting_name);
	}

	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	if (source == move_value->source)
		return refresh;
	obs_properties_t *sps = obs_source_properties(source);
	obs_property_t *sp = obs_properties_get(sps, setting_name);

	obs_data_t *ss = obs_source_get_settings(source);

	obs_property_t *prop_int = obs_properties_get(props, S_SETTING_INT);
	obs_property_t *prop_float = obs_properties_get(props, S_SETTING_FLOAT);
	obs_property_t *prop_color = obs_properties_get(props, S_SETTING_COLOR);
	obs_property_set_visible(prop_int, false);
	obs_property_set_visible(prop_float, false);
	obs_property_set_visible(prop_color, false);
	enum obs_property_type prop_type = obs_property_get_type(sp);
	if (prop_type == OBS_PROPERTY_INT) {
		obs_property_set_visible(prop_int, true);
		obs_property_int_set_limits(prop_int, obs_property_int_min(sp),
					    obs_property_int_max(sp),
					    obs_property_int_step(sp));
		obs_property_int_set_suffix(prop_int,
					    obs_property_int_suffix(sp));
		if (refresh)
			obs_data_set_int(settings, S_SETTING_INT,
					 obs_data_get_int(ss, setting_name));
		obs_data_set_int(settings, S_VALUE_TYPE, MOVE_VALUE_INT);
	} else if (prop_type == OBS_PROPERTY_FLOAT) {
		obs_property_set_visible(prop_float, true);
		obs_property_float_set_limits(prop_float,
					      obs_property_float_min(sp),
					      obs_property_float_max(sp),
					      obs_property_float_step(sp));
		obs_property_float_set_suffix(prop_float,
					      obs_property_float_suffix(sp));
		if (refresh)
			obs_data_set_double(settings, S_SETTING_FLOAT,
					    obs_data_get_double(ss,
								setting_name));
		obs_data_set_int(settings, S_VALUE_TYPE, MOVE_VALUE_FLOAT);
	} else if (prop_type == OBS_PROPERTY_COLOR) {
		obs_property_set_visible(prop_color, true);
		if (refresh)
			obs_data_set_int(settings, S_SETTING_COLOR,
					 obs_data_get_int(ss, setting_name));
		obs_data_set_int(settings, S_VALUE_TYPE, MOVE_VALUE_COLOR);
	} else {
		obs_data_set_int(settings, S_VALUE_TYPE, MOVE_VALUE_UNKNOWN);
	}
	obs_data_release(ss);
	obs_properties_destroy(sps);
	return refresh;
}

static obs_properties_t *move_value_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct move_value_info *move_value = data;
	obs_source_t *parent = obs_filter_get_parent(move_value->source);
	obs_property_t *p = obs_properties_add_list(ppts, S_FILTER,
						    obs_module_text("Filter"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("Filter.None"), "");
	obs_source_enum_filters(parent, prop_list_add_filter, p);

	obs_property_set_modified_callback2(p, move_value_filter_changed, data);

	p = obs_properties_add_bool(ppts, S_SINGLE_SETTING,
				    obs_module_text("SingleSetting"));

	obs_property_set_modified_callback2(
		p, move_value_single_setting_changed, data);

	obs_properties_t *setting_value = obs_properties_create();

	p = obs_properties_add_list(setting_value, S_SETTING_NAME,
				    obs_module_text("Setting"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p, obs_module_text("Setting.None"), "");

	obs_property_set_modified_callback2(p, move_value_setting_changed,
					    data);

	p = obs_properties_add_int(setting_value, S_SETTING_INT,
				   obs_module_text("Value"), 0, 0, 0);
	obs_property_set_visible(p, false);
	p = obs_properties_add_float(setting_value, S_SETTING_FLOAT,
				     obs_module_text("Value"), 0, 0, 0);
	obs_property_set_visible(p, false);
	p = obs_properties_add_color(setting_value, S_SETTING_COLOR,
				     obs_module_text("Value"));
	obs_property_set_visible(p, false);

	obs_properties_add_button(setting_value, "value_get",
				  obs_module_text("GetValue"),
				  move_value_get_value);

	obs_properties_add_group(ppts, S_SETTING_VALUE,
				 obs_module_text("Setting"), OBS_GROUP_NORMAL,
				 setting_value);

	obs_properties_t *settings = obs_properties_create();
	obs_properties_add_button(settings, "values_get",
				  obs_module_text("GetValues"),
				  move_value_get_values);
	obs_properties_add_group(ppts, S_SETTINGS, obs_module_text("Settings"),
				 OBS_GROUP_NORMAL, settings);

	p = obs_properties_add_int(ppts, S_START_DELAY,
				   obs_module_text("StartDelay"), 0, 10000000,
				   100);
	obs_property_int_set_suffix(p, "ms");

	obs_properties_t *duration = obs_properties_create();

	p = obs_properties_add_int(duration, S_DURATION, "", 10, 100000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_group(ppts, S_CUSTOM_DURATION,
				     obs_module_text("CustomDuration"),
				     OBS_GROUP_CHECKABLE, duration);

	p = obs_properties_add_int(ppts, S_END_DELAY,
				   obs_module_text("EndDelay"), 0, 10000000,
				   100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(ppts, S_EASING_MATCH,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(ppts, S_EASING_FUNCTION_MATCH,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_bool(ppts, S_ENABLED_MATCH_MOVING,
				    obs_module_text("EnabledMatchMoving"));

	p = obs_properties_add_list(ppts, S_START_TRIGGER,
				    obs_module_text("StartTrigger"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"),
				  START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"),
				  START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"),
				  START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"),
				  START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"),
				  START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"),
				  START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Load"),
				  START_TRIGGER_LOAD);

	p = obs_properties_add_list(ppts, S_STOP_TRIGGER,
				    obs_module_text("StopTrigger"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StopTrigger.None"),
				  START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"),
				  START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"),
				  START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"),
				  START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"),
				  START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"),
				  START_TRIGGER_ENABLE);

	p = obs_properties_add_list(ppts, S_SIMULTANEOUS_MOVE,
				    obs_module_text("SimultaneousMove"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(
		p, obs_module_text("SimultaneousMove.None"), "");
	obs_source_enum_filters(parent, prop_list_add_move_value_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE,
				    obs_module_text("NextMove"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"),
				     NEXT_MOVE_REVERSE);
	obs_source_enum_filters(parent, prop_list_add_move_value_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE_ON,
				    obs_module_text("NextMoveOn"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"),
				  NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"),
				  NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(ppts, "move_value_start",
				  obs_module_text("Start"),
				  move_value_start_button);

	return ppts;
}
void move_value_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, S_SINGLE_SETTING, true);
	obs_data_set_default_bool(settings, S_CUSTOM_DURATION, true);
	obs_data_set_default_int(settings, S_DURATION, 300);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH,
				 EASING_CUBIC);
	obs_data_set_default_bool(settings, S_ENABLED_MATCH_MOVING, true);
}

void move_value_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct move_value_info *filter = data;
	obs_source_skip_video_filter(filter->source);
}

static const char *move_value_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveValueFilter");
}

float get_eased(float f, long long easing, long long easing_function);
void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control,
		 struct vec2 *end, const float t);

void move_value_stop(struct move_value_info *move_value)
{
	move_value->moving = false;
	if (move_value->enabled_match_moving &&
	    obs_source_enabled(move_value->source)) {
		obs_source_set_enabled(move_value->source, false);
	}
}

void move_value_tick(void *data, float seconds)
{
	struct move_value_info *move_value = data;

	if (move_value->move_start_hotkey == OBS_INVALID_HOTKEY_ID &&
	    move_value->filter_name && strlen(move_value->filter_name)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_value->source);
		if (parent)
			move_value->move_start_hotkey =
				obs_hotkey_register_source(
					parent, move_value->filter_name,
					move_value->filter_name,
					move_value_start_hotkey, data);
	}
	const bool enabled = obs_source_enabled(move_value->source);
	if (move_value->enabled != enabled) {
		if (enabled &&
		    (move_value->start_trigger == START_TRIGGER_ENABLE ||
		     (move_value->enabled_match_moving && !move_value->moving)))
			move_value_start(move_value);
		if (enabled && move_value->stop_trigger == START_TRIGGER_ENABLE)
			move_value_stop(move_value);
		move_value->enabled = enabled;
	}
	if (!move_value->moving || !enabled)
		return;

	if (!move_value->duration) {
		move_value->moving = false;
		return;
	}
	move_value->running_duration += seconds;
	if (move_value->running_duration * 1000.0f <
	    (move_value->reverse ? move_value->end_delay
				 : move_value->start_delay)) {
		if (move_value->reverse)
			return;
		obs_source_t *source =
			move_value->filter
				? move_value->filter
				: obs_filter_get_parent(move_value->source);
		obs_data_t *ss = obs_source_get_settings(source);
		move_value->int_from =
			obs_data_get_int(ss, move_value->setting_name);
		move_value->double_from =
			obs_data_get_double(ss, move_value->setting_name);
		obs_data_release(ss);
		return;
	}
	if (move_value->running_duration * 1000.0f >=
	    (float)(move_value->start_delay + move_value->duration +
		    move_value->end_delay)) {
		move_value->moving = false;
	}
	float t = (move_value->running_duration * 1000.0f -
		   (float)(move_value->reverse ? move_value->end_delay
					       : move_value->start_delay)) /
		  (float)move_value->duration;
	if (t >= 1.0f) {
		t = 1.0f;
	}
	if (move_value->reverse) {
		t = 1.0f - t;
	}
	t = get_eased(t, move_value->easing, move_value->easing_function);

	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	obs_data_t *ss = obs_source_get_settings(source);
	if (move_value->settings) {
		const size_t count = obs_data_array_count(move_value->settings);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item =
				obs_data_array_item(move_value->settings, i);
			const char *setting_name =
				obs_data_get_string(item, S_SETTING_NAME);
			const long long value_type =
				obs_data_get_int(item, S_VALUE_TYPE);
			if (value_type == MOVE_VALUE_INT) {
				const long long int_from =
					obs_data_get_int(item, S_SETTING_FROM);
				const long long int_to =
					obs_data_get_int(item, S_SETTING_TO);
				const long long value_int =
					(long long)((1.0 - t) *
							    (double)int_from +
						    t * (double)int_to);
				obs_data_set_int(ss, setting_name, value_int);
			} else if (value_type == MOVE_VALUE_FLOAT) {
				const double double_from = obs_data_get_double(
					item, S_SETTING_FROM);
				const double double_to =
					obs_data_get_double(item, S_SETTING_TO);
				const double value_double =
					((1.0 - t) * double_from +
					 t * double_to);
				obs_data_set_double(ss, setting_name,
						    value_double);
			} else if (value_type == MOVE_VALUE_COLOR) {
				struct vec4 color_from;
				vec4_from_rgba(&color_from,
					       (uint32_t)obs_data_get_int(
						       item, S_SETTING_FROM));
				struct vec4 color_to;
				vec4_from_rgba(&color_to,
					       (uint32_t)obs_data_get_int(
						       item, S_SETTING_TO));
				struct vec4 color;
				color.w = (1.0f - t) * color_from.w +
					  t * color_to.w;
				color.x = (1.0f - t) * color_from.x +
					  t * color_to.x;
				color.y = (1.0f - t) * color_from.y +
					  t * color_to.y;
				color.z = (1.0f - t) * color_from.z +
					  t * color_to.z;
				const long long value_int =
					vec4_to_rgba(&color);
				obs_data_set_int(ss, setting_name, value_int);
			}
		}
	} else if (move_value->value_type == MOVE_VALUE_INT) {
		const long long value_int =
			(long long)((1.0 - t) * (double)move_value->int_from +
				    t * (double)move_value->int_to);
		obs_data_set_int(ss, move_value->setting_name, value_int);
	} else if (move_value->value_type == MOVE_VALUE_FLOAT) {
		const double value_double =
			(1.0 - t) * move_value->double_from +
			t * move_value->double_to;
		obs_data_set_double(ss, move_value->setting_name, value_double);
	} else if (move_value->value_type == MOVE_VALUE_COLOR) {
		struct vec4 color;
		color.w = (1.0f - t) * move_value->color_from.w +
			  t * move_value->color_to.w;
		color.x = (1.0f - t) * move_value->color_from.x +
			  t * move_value->color_to.x;
		color.y = (1.0f - t) * move_value->color_from.y +
			  t * move_value->color_to.y;
		color.z = (1.0f - t) * move_value->color_from.z +
			  t * move_value->color_to.z;
		const long long value_int = vec4_to_rgba(&color);
		obs_data_set_int(ss, move_value->setting_name, value_int);
	} else {
		obs_data_item_t *item =
			obs_data_item_byname(ss, move_value->setting_name);
		const enum obs_data_number_type item_type =
			obs_data_item_numtype(item);
		if (item_type == OBS_DATA_NUM_INT) {
			const long long value_int =
				(long long)((1.0 -
					     t) * (double)move_value->int_from +
					    t * (double)move_value->int_to);
			obs_data_set_int(ss, move_value->setting_name,
					 value_int);
		} else if (item_type == OBS_DATA_NUM_DOUBLE) {
			const double value_double =
				(1.0 - t) * move_value->double_from +
				t * move_value->double_to;
			obs_data_set_double(ss, move_value->setting_name,
					    value_double);
		}
		obs_data_item_release(&item);
	}
	obs_data_release(ss);
	obs_source_update(source, NULL);
	if (!move_value->moving) {
		if (move_value->enabled_match_moving &&
		    (move_value->reverse || !move_value->next_move_name ||
		     strcmp(move_value->next_move_name, NEXT_MOVE_REVERSE) !=
			     0)) {
			obs_source_set_enabled(move_value->source, false);
		}
		if (move_value->next_move_on == NEXT_MOVE_ON_END &&
		    move_value->next_move_name &&
		    strlen(move_value->next_move_name) &&
		    (!move_value->filter_name ||
		     strcmp(move_value->filter_name,
			    move_value->next_move_name) != 0)) {
			if (strcmp(move_value->next_move_name,
				   NEXT_MOVE_REVERSE) == 0) {
				move_value->reverse = !move_value->reverse;
				if (move_value->reverse)
					move_value_start(move_value);
			} else {
				obs_source_t *parent = obs_filter_get_parent(
					move_value->source);
				if (parent) {
					obs_source_t *filter =
						obs_source_get_filter_by_name(
							parent,
							move_value
								->next_move_name);
					if (filter &&
					    (strcmp(obs_source_get_unversioned_id(
							    filter),
						    MOVE_VALUE_FILTER_ID) ==
						     0 ||
					     strcmp(obs_source_get_unversioned_id(
							    filter),
						    MOVE_AUDIO_VALUE_FILTER_ID) ==
						     0)) {
						struct move_value_info
							*filter_data =
								obs_obj_get_data(
									filter);
						move_value_start(filter_data);
					}
				}
			}
		} else if (move_value->next_move_on == NEXT_MOVE_ON_HOTKEY &&
			   move_value->next_move_name &&
			   strcmp(move_value->next_move_name,
				  NEXT_MOVE_REVERSE) == 0) {
			move_value->reverse = !move_value->reverse;
		}
	}
}

void move_value_activate(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_ACTIVATE)
		move_value_start(move_value);
	if (move_value->stop_trigger == START_TRIGGER_ACTIVATE)
		move_value_stop(move_value);
}

void move_value_deactivate(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_DEACTIVATE)
		move_value_start(move_value);
	if (move_value->stop_trigger == START_TRIGGER_DEACTIVATE)
		move_value_stop(move_value);
}

void move_value_show(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_SHOW)
		move_value_start(move_value);
	if (move_value->stop_trigger == START_TRIGGER_SHOW)
		move_value_stop(move_value);
}

void move_value_hide(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_HIDE)
		move_value_start(move_value);
	if (move_value->stop_trigger == START_TRIGGER_HIDE)
		move_value_stop(move_value);
}

struct obs_source_info move_value_filter = {
	.id = MOVE_VALUE_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = move_value_get_name,
	.create = move_value_create,
	.destroy = move_value_destroy,
	.get_properties = move_value_properties,
	.get_defaults = move_value_defaults,
	.video_render = move_value_video_render,
	.video_tick = move_value_tick,
	.update = move_value_update,
	.load = move_value_update,
	.activate = move_value_activate,
	.deactivate = move_value_deactivate,
	.show = move_value_show,
	.hide = move_value_hide,
};

struct obs_source_info move_audio_value_filter = {
	.id = MOVE_AUDIO_VALUE_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = move_value_get_name,
	.create = move_value_create,
	.destroy = move_value_destroy,
	.get_properties = move_value_properties,
	.get_defaults = move_value_defaults,
	.video_render = move_value_video_render,
	.video_tick = move_value_tick,
	.update = move_value_update,
	.load = move_value_update,
	.activate = move_value_activate,
	.deactivate = move_value_deactivate,
	.show = move_value_show,
	.hide = move_value_hide,
};
