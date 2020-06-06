#include "move-transition.h"
#include <obs-module.h>
#include <util/dstr.h>

struct move_value_info {
	obs_source_t *source;
	char *filter_name;
	obs_source_t *filter;
	char *setting_name;

	obs_hotkey_id move_start_hotkey;

	uint64_t duration;
	uint64_t start_delay;
	uint32_t start_trigger;
	bool moving;
	float running_duration;
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

	long long value_type;
};

void move_value_start(struct move_value_info *move_value)
{
	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	obs_data_t *ss = obs_source_get_settings(source);
	if (move_value->value_type == MOVE_VALUE_INT) {
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
			       obs_data_get_int(ss, move_value->setting_name));
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
	obs_data_release(ss);
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
	move_value_start(move_value);
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

		move_value->filter_name = bstrdup(filter_name);
		obs_hotkey_unregister(move_value->move_start_hotkey);
		move_value->move_start_hotkey = obs_hotkey_register_source(
			parent, move_value->filter_name,
			move_value->filter_name, move_value_start_hotkey, data);
	}

	const char *setting_name =
		obs_data_get_string(settings, S_SETTING_NAME);
	;
	if (!move_value->setting_name ||
	    strcmp(move_value->setting_name, setting_name) != 0) {
		bfree(move_value->setting_name);

		move_value->setting_name = bstrdup(setting_name);
	}

	move_value->value_type = obs_data_get_int(settings, S_VALUE_TYPE);
	move_value->int_to = obs_data_get_int(settings, S_SETTING_INT);
	move_value->double_to = obs_data_get_double(settings, S_SETTING_FLOAT);
	vec4_from_rgba(&move_value->color_to,
		       obs_data_get_int(settings, S_SETTING_COLOR));

	move_value->duration = obs_data_get_int(settings, S_DURATION);
	move_value->start_delay = obs_data_get_int(settings, S_START_DELAY);
	move_value->easing = obs_data_get_int(settings, S_EASING_MATCH);
	move_value->easing_function =
		obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move_value->start_trigger = obs_data_get_int(settings, S_START_TRIGGER);

	const char *next_move_name = obs_data_get_string(settings, S_NEXT_MOVE);
	if (!move_value->next_move_name ||
	    strcmp(move_value->next_move_name, next_move_name) != 0) {
		bfree(move_value->next_move_name);
		move_value->next_move_name = bstrdup(next_move_name);
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
	bfree(move_value->filter_name);
	bfree(move_value->next_move_name);
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
	if (strcmp(obs_source_get_id(child), "move_value_filter") != 0)
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
	return settings_changed;
}
bool move_value_filter_changed(void *data, obs_properties_t *props,
			       obs_property_t *property, obs_data_t *settings)
{
	struct move_value_info *move_value = data;
	bool refresh = false;

	obs_source_t *parent = obs_filter_get_parent(move_value->source);
	obs_property_t *p = obs_properties_get(props, S_SETTING_NAME);

	const char *filter_name = obs_data_get_string(settings, S_FILTER);
	refresh = true;

	obs_source_release(move_value->filter);

	if (filter_name && strlen(filter_name)) {
		move_value->filter =
			obs_source_get_filter_by_name(parent, filter_name);
	} else {
		move_value->filter = NULL;
	}

	obs_property_list_clear(p);
	obs_property_list_add_string(p, obs_module_text("Setting.None"), "");

	obs_source_t *source = move_value->filter ? move_value->filter : parent;
	obs_data_t *s = obs_source_get_settings(source);
	if (!s)
		return refresh;

	obs_properties_t *sps = obs_source_properties(source);

	obs_data_item_t *item = obs_data_first(s);
	for (; item != NULL; obs_data_item_next(&item)) {
		if (obs_data_item_gettype(item) != OBS_DATA_NUMBER)
			continue;
		const char *name = obs_data_item_get_name(item);
		const char *description =
			obs_property_description(obs_properties_get(sps, name));
		obs_property_list_add_string(
			p, description ? description : name, name);
	}

	obs_data_release(s);
	return refresh;
}

bool move_value_setting_changed(void *data, obs_properties_t *props,
				obs_property_t *property, obs_data_t *settings)
{

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

	p = obs_properties_add_list(ppts, S_SETTING_NAME,
				    obs_module_text("Setting"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p, obs_module_text("Setting.None"), "");

	obs_property_set_modified_callback2(p, move_value_setting_changed,
					    data);

	p = obs_properties_add_int(ppts, S_SETTING_INT,
				   obs_module_text("Value"), 0, 0, 0);
	obs_property_set_visible(p, false);
	p = obs_properties_add_float(ppts, S_SETTING_FLOAT,
				     obs_module_text("Value"), 0, 0, 0);
	obs_property_set_visible(p, false);
	p = obs_properties_add_color(ppts, S_SETTING_COLOR,
				     obs_module_text("Value"));
	obs_property_set_visible(p, false);

	obs_properties_add_button(ppts, "value_get",
				  obs_module_text("GetValue"),
				  move_value_get_value);

	p = obs_properties_add_int(ppts, S_START_DELAY,
				   obs_module_text("StartDelay"), 0, 10000000,
				   100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_int(
		ppts, S_DURATION, obs_module_text("Duration"), 10, 100000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(ppts, S_EASING_MATCH,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(ppts, S_EASING_FUNCTION_MATCH,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

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

	p = obs_properties_add_list(ppts, S_NEXT_MOVE,
				    obs_module_text("NextMove"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_source_enum_filters(parent, prop_list_add_move_value_filter, p);

	obs_properties_add_button(ppts, "move_value_start",
				  obs_module_text("Start"),
				  move_value_start_button);
	return ppts;
}
void move_value_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_DURATION, 300);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH,
				 EASING_CUBIC);
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

void move_value_tick(void *data, float seconds)
{
	struct move_value_info *move_value = data;

	if (move_value->move_start_hotkey == OBS_INVALID_HOTKEY_ID &&
	    move_value->filter_name) {
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
		    move_value->start_trigger == START_TRIGGER_ENABLE)
			move_value_start(move_value);
		move_value->enabled = enabled;
	}
	if (!move_value->moving || !enabled)
		return;

	if (!move_value->duration) {
		move_value->moving = false;
		return;
	}
	move_value->running_duration += seconds;
	if (move_value->running_duration * 1000.0f < move_value->start_delay) {
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

	float t = (move_value->running_duration * 1000.0f -
		   (float)move_value->start_delay) /
		  (float)move_value->duration;
	if (t >= 1.0f) {
		t = 1.0f;
		move_value->moving = false;
	}
	t = get_eased(t, move_value->easing, move_value->easing_function);

	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	obs_source_t *source =
		move_value->filter ? move_value->filter
				   : obs_filter_get_parent(move_value->source);
	obs_data_t *ss = obs_source_get_settings(source);
	obs_data_item_t *item =
		obs_data_item_byname(ss, move_value->setting_name);

	obs_data_item_release(&item);
	if (move_value->value_type == MOVE_VALUE_INT) {
		const long long value_int =
			(1.0 - t) * (double)move_value->int_from +
			t * (double)move_value->int_to;
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
		enum obs_data_number_type item_type =
			obs_data_item_numtype(item);
		if (item_type == OBS_DATA_NUM_INT) {
			const long long value_int =
				(1.0 - t) * (double)move_value->int_from +
				t * (double)move_value->int_to;
			obs_data_set_int(ss, move_value->setting_name,
					 value_int);
		} else if (item_type == OBS_DATA_NUM_DOUBLE) {
			const double value_double =
				(1.0 - t) * move_value->double_from +
				t * move_value->double_to;
			obs_data_set_double(ss, move_value->setting_name,
					    value_double);
		}
	}
	obs_data_release(ss);
	obs_source_update(source, NULL);

	if (!move_value->moving && move_value->next_move_name &&
	    strlen(move_value->next_move_name) &&
	    (!move_value->filter_name ||
	     strcmp(move_value->filter_name, move_value->next_move_name) !=
		     0)) {
		obs_source_t *parent =
			obs_filter_get_parent(move_value->source);
		if (parent) {
			obs_source_t *filter = obs_source_get_filter_by_name(
				parent, move_value->next_move_name);
			if (filter && strcmp(obs_source_get_id(filter),
					     "move_value_filter") == 0) {
				move_value_start(obs_obj_get_data(filter));
			}
		}
	}
}

void move_value_activate(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_ACTIVATE)
		move_value_start(move_value);
}

void move_value_deactivate(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_DEACTIVATE)
		move_value_start(move_value);
}

void move_value_show(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_SHOW)
		move_value_start(move_value);
}

void move_value_hide(void *data)
{
	struct move_value_info *move_value = data;
	if (move_value->start_trigger == START_TRIGGER_HIDE)
		move_value_start(move_value);
}

struct obs_source_info move_value_filter = {
	.id = "move_value_filter",
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
	.activate = move_value_activate,
	.deactivate = move_value_deactivate,
	.show = move_value_show,
	.hide = move_value_hide,
};
