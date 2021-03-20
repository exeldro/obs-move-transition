#include "move-transition.h"

#include <float.h>
#include <obs-module.h>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

#define METER_TYPE_MAGNITUDE 0
#define METER_TYPE_PEAK_SAMPLE 1
#define METER_TYPE_PEAK_TRUE 2
#define METER_TYPE_INPUT_PEAK_SAMPLE 3
#define METER_TYPE_INPUT_PEAK_TRUE 4

#define VALUE_ACTION_TRANSFORM 0
#define VALUE_ACTION_SETTING 1
#define VALUE_ACTION_SOURCE_VISIBILITY 2
#define VALUE_ACTION_FILTER_ENABLE 3

#define THRESHOLD_NONE 0
#define THRESHOLD_ENABLE_OVER 1
#define THRESHOLD_ENABLE_UNDER 2
#define THRESHOLD_DISABLE_OVER 3
#define THRESHOLD_DISABLE_UNDER 4
#define THRESHOLD_ENABLE_OVER_DISABLE_UNDER 5
#define THRESHOLD_ENABLE_UNDER_DISABLE_OVER 6

#define TRANSFORM_NONE 0
#define TRANSFORM_POS_X 1
#define TRANSFORM_POS_Y 2
#define TRANSFORM_ROT 3
#define TRANSFORM_SCALE 4
#define TRANSFORM_SCALE_X 4
#define TRANSFORM_SCALE_Y 5
#define TRANSFORM_BOUNDS_X 6
#define TRANSFORM_BOUNDS_Y 7
#define TRANSFORM_CROP_LEFT 8
#define TRANSFORM_CROP_TOP 9
#define TRANSFORM_CROP_RIGHT 10
#define TRANSFORM_CROP_BOTTOM 11
#define TRANSFORM_CROP_HORIZONTAL 12
#define TRANSFORM_CROP_VERTICAL 13

struct audio_move_info {
	obs_source_t *source;
	double easing;
	double audio_value;
	double base_value;
	double factor;
	long long action;
	long long threshold_action;
	double threshold;

	obs_sceneitem_t *sceneitem;
	obs_source_t *target_source;
	char *setting_name;

	obs_volmeter_t *volmeter;
	long long meter_type;
	long long transform;
};

static const char *audio_move_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("AudioMoveFilter");
}

void audio_move_volmeter_updated(void *data,
				 const float magnitude[MAX_AUDIO_CHANNELS],
				 const float peak[MAX_AUDIO_CHANNELS],
				 const float input_peak[MAX_AUDIO_CHANNELS])
{
	struct audio_move_info *audio_move = data;
	float v = 0.0f;
	if (audio_move->meter_type == METER_TYPE_MAGNITUDE) {
		v = magnitude[0];
	} else if (audio_move->meter_type == METER_TYPE_INPUT_PEAK_SAMPLE ||
		   audio_move->meter_type == METER_TYPE_INPUT_PEAK_TRUE) {
		v = input_peak[0];
	} else if (audio_move->meter_type == METER_TYPE_PEAK_SAMPLE ||
		   audio_move->meter_type == METER_TYPE_PEAK_TRUE) {
		v = peak[0];
	}
	v = obs_db_to_mul(v);
	audio_move->audio_value = audio_move->easing * audio_move->audio_value +
				  (1.0 - audio_move->easing) * v;
}

void audio_move_item_remove(void *data, calldata_t *call_data)
{
	struct audio_move_info *audio_move = data;
	obs_scene_t *scene = NULL;
	calldata_get_ptr(call_data, "scene", &scene);
	obs_sceneitem_t *item = NULL;
	calldata_get_ptr(call_data, "item", &item);
	if (item == audio_move->sceneitem) {
		audio_move->sceneitem = NULL;
		obs_source_t *parent = obs_scene_get_source(scene);
		if (parent) {
			signal_handler_t *sh =
				obs_source_get_signal_handler(parent);
			if (sh)
				signal_handler_disconnect(
					sh, "item_remove",
					audio_move_item_remove, audio_move);
		}
	}
}

void audio_move_source_destroy(void *data, calldata_t *call_data)
{
	struct audio_move_info *audio_move = data;
	audio_move->target_source = NULL;
	audio_move->sceneitem = NULL;
}

void audio_move_source_remove(void *data, calldata_t *call_data)
{
	struct audio_move_info *audio_move = data;
	if (audio_move->target_source) {
		signal_handler_t *sh = obs_source_get_signal_handler(
			audio_move->target_source);
		signal_handler_disconnect(sh, "remove",
					  audio_move_source_remove, audio_move);
		signal_handler_disconnect(
			sh, "destroy", audio_move_source_destroy, audio_move);
		obs_source_release(audio_move->target_source);
	}
	audio_move->target_source = NULL;
	if (audio_move->sceneitem) {
		obs_scene_t *scene =
			obs_sceneitem_get_scene(audio_move->sceneitem);
		signal_handler_t *sh = obs_source_get_signal_handler(
			obs_scene_get_source(scene));
		if (sh)
			signal_handler_disconnect(sh, "item_remove",
						  audio_move_item_remove,
						  audio_move);
		obs_source_t *item_source =
			obs_sceneitem_get_source(audio_move->sceneitem);
		if (item_source) {
			sh = obs_source_get_signal_handler(item_source);
			signal_handler_disconnect(sh, "remove",
						  audio_move_source_remove,
						  audio_move);
			signal_handler_disconnect(sh, "destroy",
						  audio_move_source_destroy,
						  audio_move);
		}
	}
	audio_move->sceneitem = NULL;
}

void audio_move_update(void *data, obs_data_t *settings)
{
	struct audio_move_info *audio_move = data;

	obs_source_t *parent = obs_filter_get_parent(audio_move->source);
	if (parent)
		obs_volmeter_attach_source(audio_move->volmeter, parent);

	const long long meter_type = obs_data_get_int(settings, "meter_type");
	if (meter_type != audio_move->meter_type) {
		audio_move->meter_type = meter_type;
		if (meter_type == METER_TYPE_INPUT_PEAK_SAMPLE ||
		    meter_type == METER_TYPE_PEAK_SAMPLE) {
			obs_volmeter_set_peak_meter_type(audio_move->volmeter,
							 SAMPLE_PEAK_METER);
		} else if (meter_type == METER_TYPE_INPUT_PEAK_TRUE ||
			   meter_type == METER_TYPE_PEAK_TRUE) {
			obs_volmeter_set_peak_meter_type(audio_move->volmeter,
							 TRUE_PEAK_METER);
		}
	}
	audio_move->easing = obs_data_get_double(settings, "easing") / 100.0;
	audio_move->action = obs_data_get_int(settings, "value_action");
	audio_move->transform = obs_data_get_int(settings, "transform");
	audio_move->base_value = obs_data_get_double(settings, "base_value");
	audio_move->factor = obs_data_get_double(settings, "factor");

	const char *scene_name = obs_data_get_string(settings, "scene");
	const char *sceneitem_name = obs_data_get_string(settings, "sceneitem");
	obs_source_t *source = obs_get_source_by_name(scene_name);
	obs_source_release(source);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (audio_move->sceneitem) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh)
			signal_handler_disconnect(sh, "item_remove",
						  audio_move_item_remove,
						  audio_move);
		obs_source_t *item_source =
			obs_sceneitem_get_source(audio_move->sceneitem);
		if (item_source) {
			sh = obs_source_get_signal_handler(item_source);
			signal_handler_disconnect(sh, "remove",
						  audio_move_source_remove,
						  audio_move);
			signal_handler_disconnect(sh, "destroy",
						  audio_move_source_destroy,
						  audio_move);
		}
	}
	audio_move->sceneitem =
		scene ? obs_scene_find_source(scene, sceneitem_name) : NULL;
	if (audio_move->sceneitem &&
	    obs_source_removed(
		    obs_sceneitem_get_source(audio_move->sceneitem))) {
		audio_move->sceneitem = NULL;
	}

	if (audio_move->sceneitem && source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh)
			signal_handler_connect(sh, "item_remove",
					       audio_move_item_remove,
					       audio_move);
		obs_source_t *item_source =
			obs_sceneitem_get_source(audio_move->sceneitem);
		if (item_source) {
			sh = obs_source_get_signal_handler(item_source);
			signal_handler_connect(sh, "remove",
					       audio_move_source_remove,
					       audio_move);
			signal_handler_connect(sh, "destroy",
					       audio_move_source_destroy,
					       audio_move);
		}
	}

	if (audio_move->target_source) {
		signal_handler_t *sh = obs_source_get_signal_handler(
			audio_move->target_source);
		signal_handler_disconnect(sh, "remove",
					  audio_move_source_remove, audio_move);
		signal_handler_disconnect(
			sh, "destroy", audio_move_source_destroy, audio_move);
		obs_source_release(audio_move->target_source);
	}
	audio_move->target_source = NULL;
	if (audio_move->action == VALUE_ACTION_FILTER_ENABLE) {
		source = obs_get_source_by_name(
			obs_data_get_string(settings, "source"));
		if (source) {
			obs_source_t *filter = obs_source_get_filter_by_name(
				source,
				obs_data_get_string(settings, "filter"));
			if (filter) {
				audio_move->target_source = filter;
			}
			obs_source_release(source);
		}
	} else if (audio_move->action == VALUE_ACTION_SETTING) {
		source = obs_get_source_by_name(
			obs_data_get_string(settings, "source"));
		if (source) {
			const char *filter_name =
				obs_data_get_string(settings, "filter");

			obs_source_t *filter =
				filter_name && strlen(filter_name)
					? obs_source_get_filter_by_name(
						  source, filter_name)
					: NULL;
			if (filter) {
				audio_move->target_source = filter;
				obs_source_release(source);
			} else {
				audio_move->target_source = source;
			}
		}
	}
	if (audio_move->target_source &&
	    obs_source_removed(audio_move->target_source)) {
		audio_move->target_source = NULL;
	}
	if (audio_move->target_source) {

		signal_handler_t *sh = obs_source_get_signal_handler(
			audio_move->target_source);
		signal_handler_connect(sh, "remove", audio_move_source_remove,
				       audio_move);
		signal_handler_connect(sh, "destroy", audio_move_source_destroy,
				       audio_move);
	}
	audio_move->threshold_action =
		obs_data_get_int(settings, "threshold_action");
	audio_move->threshold =
		obs_data_get_double(settings, "threshold") / 100.0;
	const char *setting_name = obs_data_get_string(settings, "setting");
	if (!audio_move->setting_name ||
	    strcmp(audio_move->setting_name, setting_name) != 0) {
		bfree(audio_move->setting_name);
		audio_move->setting_name = bstrdup(setting_name);
	}
}

static void *audio_move_create(obs_data_t *settings, obs_source_t *source)
{
	struct audio_move_info *audio_move =
		bzalloc(sizeof(struct audio_move_info));
	audio_move->source = source;
	audio_move->volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_add_callback(audio_move->volmeter,
				  audio_move_volmeter_updated, audio_move);
	audio_move_update(audio_move, settings);

	return audio_move;
}

static void audio_move_destroy(void *data)
{
	struct audio_move_info *audio_move = data;
	obs_volmeter_detach_source(audio_move->volmeter);
	obs_volmeter_remove_callback(audio_move->volmeter,
				     audio_move_volmeter_updated, audio_move);
	obs_volmeter_destroy(audio_move->volmeter);
	audio_move->volmeter = NULL;
	if (audio_move->target_source) {
		signal_handler_t *sh = obs_source_get_signal_handler(
			audio_move->target_source);
		signal_handler_disconnect(sh, "remove",
					  audio_move_source_remove, audio_move);
		signal_handler_disconnect(
			sh, "destroy", audio_move_source_destroy, audio_move);
		obs_source_release(audio_move->target_source);
	}
	audio_move->target_source = NULL;
	if (audio_move->sceneitem) {
		obs_scene_t *scene =
			obs_sceneitem_get_scene(audio_move->sceneitem);
		signal_handler_t *sh = obs_source_get_signal_handler(
			obs_scene_get_source(scene));
		if (sh)
			signal_handler_disconnect(sh, "item_remove",
						  audio_move_item_remove,
						  audio_move);
		obs_source_t *item_source =
			obs_sceneitem_get_source(audio_move->sceneitem);
		if (item_source) {
			sh = obs_source_get_signal_handler(item_source);
			signal_handler_disconnect(sh, "remove",
						  audio_move_source_remove,
						  audio_move);
			signal_handler_disconnect(sh, "destroy",
						  audio_move_source_destroy,
						  audio_move);
		}
	}
	audio_move->sceneitem = NULL;
	bfree(audio_move->setting_name);
	bfree(audio_move);
}

static bool add_source_to_prop_list(void *data, obs_source_t *source)
{
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(source);
	if (name && strlen(name))
		obs_property_list_add_string(p, name, name);
	return true;
}

static bool audio_move_action_changed(obs_properties_t *props,
				      obs_property_t *property,
				      obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	long long action = obs_data_get_int(settings, "value_action");
	obs_property_t *scene = obs_properties_get(props, "scene");
	obs_property_t *sceneitem = obs_properties_get(props, "sceneitem");
	if (action == VALUE_ACTION_TRANSFORM ||
	    action == VALUE_ACTION_SOURCE_VISIBILITY) {
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
	if (action == VALUE_ACTION_SETTING ||
	    action == VALUE_ACTION_FILTER_ENABLE) {
		obs_property_list_clear(source);
		obs_enum_sources(add_source_to_prop_list, source);
		obs_enum_scenes(add_source_to_prop_list, source);
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, true);
	} else {
		obs_property_set_visible(source, false);
		obs_property_set_visible(filter, false);
	}
	obs_property_t *base_value = obs_properties_get(props, "base_value");
	obs_property_t *factor = obs_properties_get(props, "factor");
	if (action == VALUE_ACTION_SETTING ||
	    action == VALUE_ACTION_TRANSFORM) {
		obs_property_set_visible(base_value, true);
		obs_property_set_visible(factor, true);
	} else {
		obs_property_set_visible(base_value, false);
		obs_property_set_visible(factor, false);
	}
	obs_property_t *threshold_action =
		obs_properties_get(props, "threshold_action");
	obs_property_t *threshold = obs_properties_get(props, "threshold");
	if (action == VALUE_ACTION_SOURCE_VISIBILITY ||
	    action == VALUE_ACTION_FILTER_ENABLE) {
		obs_property_set_visible(threshold_action, true);
		obs_property_set_visible(threshold, true);
	} else {
		obs_property_set_visible(threshold_action, false);
		obs_property_set_visible(threshold, false);
	}
	obs_property_t *transform = obs_properties_get(props, "transform");
	if (action == VALUE_ACTION_TRANSFORM) {
		obs_property_set_visible(transform, true);
	} else {
		obs_property_set_visible(transform, false);
	}
	obs_property_t *setting = obs_properties_get(props, "setting");
	if (action == VALUE_ACTION_SETTING) {
		obs_property_set_visible(setting, true);
	} else {
		obs_property_set_visible(setting, false);
	}
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

static bool audio_move_scene_changed(void *data, obs_properties_t *props,
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

static void add_filter_to_prop_list(obs_source_t *parent, obs_source_t *child,
				    void *data)
{
	UNUSED_PARAMETER(parent);
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(child);
	const char *src_id = obs_source_get_id(child);

	if (name && strlen(name) && strcmp(src_id, AUDIO_MOVE_FILTER_ID) != 0)
		obs_property_list_add_string(p, name, name);
}

static void load_properties(obs_properties_t *props_from,
			    obs_property_t *setting_list)
{
	obs_property_t *prop_from = obs_properties_first(props_from);
	for (; prop_from != NULL; obs_property_next(&prop_from)) {
		const char *name = obs_property_name(prop_from);
		const char *description = obs_property_description(prop_from);
		if (!obs_property_visible(prop_from))
			continue;
		const enum obs_property_type prop_type =
			obs_property_get_type(prop_from);
		if (prop_type == OBS_PROPERTY_GROUP) {
			load_properties(obs_property_group_content(prop_from),
					setting_list);
		} else if (prop_type == OBS_PROPERTY_FLOAT ||
			   prop_type == OBS_PROPERTY_INT) {
			obs_property_list_add_string(setting_list, description,
						     name);
		}
	}
}

static bool audio_move_source_changed(void *data, obs_properties_t *props,
				      obs_property_t *property,
				      obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);
	const char *source_name = obs_data_get_string(settings, "source");
	const char *filter_name = obs_data_get_string(settings, "filter");
	obs_property_t *filter = obs_properties_get(props, "filter");
	obs_property_list_clear(filter);
	obs_source_t *source = obs_get_source_by_name(source_name);
	obs_source_release(source);
	obs_source_enum_filters(source, add_filter_to_prop_list, filter);

	obs_property_t *setting = obs_properties_get(props, "setting");
	obs_property_list_clear(setting);
	obs_properties_t *properties = NULL;
	if (filter_name && strlen(filter_name)) {
		obs_source_t *f =
			obs_source_get_filter_by_name(source, filter_name);
		if (f) {
			properties = obs_source_properties(f);
		}
	} else {
		properties = obs_source_properties(source);
	}
	if (properties) {
		load_properties(properties, setting);
		obs_properties_destroy(properties);
	}
	return true;
}

static obs_properties_t *audio_move_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(
		ppts, "meter_type", obs_module_text("MeterType"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("MeterType.Magnitude"),
				  METER_TYPE_MAGNITUDE);
	obs_property_list_add_int(p, obs_module_text("MeterType.PeakSample"),
				  METER_TYPE_PEAK_SAMPLE);
	obs_property_list_add_int(p, obs_module_text("MeterType.PeakTrue"),
				  METER_TYPE_PEAK_TRUE);
	obs_property_list_add_int(p,
				  obs_module_text("MeterType.InputPeakSample"),
				  METER_TYPE_INPUT_PEAK_SAMPLE);
	obs_property_list_add_int(p, obs_module_text("MeterType.InputPeakTrue"),
				  METER_TYPE_INPUT_PEAK_TRUE);

	p = obs_properties_add_float_slider(
		ppts, "easing", obs_module_text("Easing"), 0.0, 99.99, 0.01);

	p = obs_properties_add_list(ppts, "value_action",
				    obs_module_text("ValueAction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ValueAction.Transform"),
				  VALUE_ACTION_TRANSFORM);
	obs_property_list_add_int(p, obs_module_text("ValueAction.Setting"),
				  VALUE_ACTION_SETTING);
	obs_property_list_add_int(
		p, obs_module_text("ValueAction.SourceVisibility"),
		VALUE_ACTION_SOURCE_VISIBILITY);
	obs_property_list_add_int(p,
				  obs_module_text("ValueAction.FilterEnable"),
				  VALUE_ACTION_FILTER_ENABLE);
	obs_property_set_modified_callback(p, audio_move_action_changed);

	p = obs_properties_add_list(ppts, "scene", obs_module_text("Scene"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(p, audio_move_scene_changed, data);

	p = obs_properties_add_list(ppts, "sceneitem",
				    obs_module_text("Source"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_list(ppts, "source", obs_module_text("Source"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, audio_move_source_changed, data);

	p = obs_properties_add_list(ppts, "filter", obs_module_text("Filter"),
				    OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, audio_move_source_changed, data);

	p = obs_properties_add_list(ppts, "transform",
				    obs_module_text("Transform"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Transform.PosX"),
				  TRANSFORM_POS_X);
	obs_property_list_add_int(p, obs_module_text("Transform.PosY"),
				  TRANSFORM_POS_Y);
	obs_property_list_add_int(p, obs_module_text("Transform.Rotation"),
				  TRANSFORM_ROT);
	obs_property_list_add_int(p, obs_module_text("Transform.Scale"),
				  TRANSFORM_SCALE);
	obs_property_list_add_int(p, obs_module_text("Transform.ScaleX"),
				  TRANSFORM_SCALE_X);
	obs_property_list_add_int(p, obs_module_text("Transform.ScaleY"),
				  TRANSFORM_SCALE_Y);
	obs_property_list_add_int(p, obs_module_text("Transform.BoundsX"),
				  TRANSFORM_BOUNDS_X);
	obs_property_list_add_int(p, obs_module_text("Transform.BoundsY"),
				  TRANSFORM_BOUNDS_Y);
	obs_property_list_add_int(p, obs_module_text("Transform.CropLeft"),
				  TRANSFORM_CROP_LEFT);
	obs_property_list_add_int(p, obs_module_text("Transform.CropTop"),
				  TRANSFORM_CROP_TOP);
	obs_property_list_add_int(p, obs_module_text("Transform.CropRight"),
				  TRANSFORM_CROP_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Transform.CropBottom"),
				  TRANSFORM_CROP_BOTTOM);
	obs_property_list_add_int(p,
				  obs_module_text("Transform.CropHorizontal"),
				  TRANSFORM_CROP_HORIZONTAL);
	obs_property_list_add_int(p, obs_module_text("Transform.CropVertical"),
				  TRANSFORM_CROP_VERTICAL);

	p = obs_properties_add_list(ppts, "setting", obs_module_text("Setting"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

	p = obs_properties_add_float(ppts, "base_value",
				     obs_module_text("BaseValue"), -DBL_MAX,
				     DBL_MAX, 0.01);
	p = obs_properties_add_float(ppts, "factor", obs_module_text("Factor"),
				     -DBL_MAX, DBL_MAX, 0.01);
	p = obs_properties_add_list(ppts, "threshold_action",
				    obs_module_text("ThresholdAction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("ThresholdAction.None"),
				  THRESHOLD_NONE);
	obs_property_list_add_int(p,
				  obs_module_text("ThresholdAction.EnableOver"),
				  THRESHOLD_ENABLE_OVER);
	obs_property_list_add_int(
		p, obs_module_text("ThresholdAction.EnableUnder"),
		THRESHOLD_ENABLE_UNDER);
	obs_property_list_add_int(
		p, obs_module_text("ThresholdAction.DisableOver"),
		THRESHOLD_DISABLE_OVER);
	obs_property_list_add_int(
		p, obs_module_text("ThresholdAction.DisableUnder"),
		THRESHOLD_DISABLE_UNDER);
	obs_property_list_add_int(
		p, obs_module_text("ThresholdAction.EnableOverDisableUnder"),
		THRESHOLD_ENABLE_OVER_DISABLE_UNDER);
	obs_property_list_add_int(
		p, obs_module_text("ThresholdAction.EnableUnderDisableOver"),
		THRESHOLD_ENABLE_UNDER_DISABLE_OVER);
	p = obs_properties_add_float_slider(ppts, "threshold",
					    obs_module_text("Threshold"), 0.0,
					    100.0, 0.01);
	return ppts;
}

void audio_move_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "factor", 1000.0);
}

void audio_move_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct audio_move_info *filter = data;
	if (!obs_source_enabled(filter->source))
		return;
	if (filter->action == VALUE_ACTION_TRANSFORM) {
		if (!filter->sceneitem) {
			obs_data_t *settings =
				obs_source_get_settings(filter->source);
			audio_move_update(filter, settings);
			obs_data_release(settings);
		}
		if (!filter->sceneitem)
			return;
		if (filter->transform == TRANSFORM_POS_X) {
			struct vec2 pos;
			obs_sceneitem_get_pos(filter->sceneitem, &pos);
			pos.x = filter->factor * filter->audio_value +
				filter->base_value;
			obs_sceneitem_set_pos(filter->sceneitem, &pos);
		} else if (filter->transform == TRANSFORM_POS_Y) {
			struct vec2 pos;
			obs_sceneitem_get_pos(filter->sceneitem, &pos);
			pos.y = filter->factor * filter->audio_value +
				filter->base_value;
			obs_sceneitem_set_pos(filter->sceneitem, &pos);
		} else if (filter->transform == TRANSFORM_ROT) {
			obs_sceneitem_set_rot(
				filter->sceneitem,
				filter->factor * filter->audio_value +
					filter->base_value);
		} else if (filter->transform == TRANSFORM_SCALE) {
			struct vec2 scale;
			scale.x = filter->factor * filter->audio_value +
				  filter->base_value;
			scale.y = scale.x;
			obs_sceneitem_set_scale(filter->sceneitem, &scale);
		} else if (filter->transform == TRANSFORM_SCALE_X) {
			struct vec2 scale;
			obs_sceneitem_get_scale(filter->sceneitem, &scale);
			scale.x = filter->factor * filter->audio_value +
				  filter->base_value;
			obs_sceneitem_set_scale(filter->sceneitem, &scale);
		} else if (filter->transform == TRANSFORM_SCALE_Y) {
			struct vec2 scale;
			obs_sceneitem_get_scale(filter->sceneitem, &scale);
			scale.y = filter->factor * filter->audio_value +
				  filter->base_value;
			obs_sceneitem_set_scale(filter->sceneitem, &scale);
		} else if (filter->transform == TRANSFORM_BOUNDS_X) {
			struct vec2 bounds;
			obs_sceneitem_get_bounds(filter->sceneitem, &bounds);
			bounds.x = filter->factor * filter->audio_value +
				   filter->base_value;
			obs_sceneitem_set_scale(filter->sceneitem, &bounds);
		} else if (filter->transform == TRANSFORM_BOUNDS_Y) {
			struct vec2 bounds;
			obs_sceneitem_get_bounds(filter->sceneitem, &bounds);
			bounds.y = filter->factor * filter->audio_value +
				   filter->base_value;
			obs_sceneitem_set_scale(filter->sceneitem, &bounds);
		} else if (filter->transform == TRANSFORM_CROP_LEFT) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.left = filter->factor * filter->audio_value +
				    filter->base_value;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		} else if (filter->transform == TRANSFORM_CROP_TOP) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.top = filter->factor * filter->audio_value +
				   filter->base_value;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		} else if (filter->transform == TRANSFORM_CROP_RIGHT) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.right = filter->factor * filter->audio_value +
				     filter->base_value;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		} else if (filter->transform == TRANSFORM_CROP_BOTTOM) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.bottom = filter->factor * filter->audio_value +
				      filter->base_value;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		} else if (filter->transform == TRANSFORM_CROP_HORIZONTAL) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.left = filter->factor * filter->audio_value +
				    filter->base_value;
			crop.right = crop.left;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		} else if (filter->transform == TRANSFORM_CROP_VERTICAL) {
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(filter->sceneitem, &crop);
			crop.top = filter->factor * filter->audio_value +
				   filter->base_value;
			crop.bottom = crop.top;
			obs_sceneitem_set_crop(filter->sceneitem, &crop);
		}

	} else if (filter->action == VALUE_ACTION_SOURCE_VISIBILITY) {
		if (!filter->sceneitem) {
			obs_data_t *settings =
				obs_source_get_settings(filter->source);
			audio_move_update(filter, settings);
			obs_data_release(settings);
		}
		if (!filter->sceneitem)
			return;
		if ((filter->threshold_action == THRESHOLD_ENABLE_OVER ||
		     filter->threshold_action ==
			     THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
		    filter->audio_value >= filter->threshold) {
			obs_sceneitem_set_visible(filter->sceneitem, true);
		} else if ((filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
			   filter->audio_value < filter->threshold) {
			obs_sceneitem_set_visible(filter->sceneitem, true);
		} else if ((filter->threshold_action ==
				    THRESHOLD_DISABLE_OVER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
			   filter->audio_value >= filter->threshold) {
			obs_sceneitem_set_visible(filter->sceneitem, false);
		} else if ((filter->threshold_action ==
				    THRESHOLD_DISABLE_UNDER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
			   filter->audio_value < filter->threshold) {
			obs_sceneitem_set_visible(filter->sceneitem, false);
		}
	} else if (filter->action == VALUE_ACTION_FILTER_ENABLE) {
		if (!filter->target_source) {
			obs_data_t *settings =
				obs_source_get_settings(filter->source);
			audio_move_update(filter, settings);
			obs_data_release(settings);
		}
		if (!filter->target_source)
			return;
		if ((filter->threshold_action == THRESHOLD_ENABLE_OVER ||
		     filter->threshold_action ==
			     THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
		    filter->audio_value >= filter->threshold &&
		    !obs_source_enabled(filter->target_source)) {
			obs_source_set_enabled(filter->target_source, true);
		} else if ((filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
			   filter->audio_value < filter->threshold &&
			   !obs_source_enabled(filter->target_source)) {
			obs_source_set_enabled(filter->target_source, true);
		} else if ((filter->threshold_action ==
				    THRESHOLD_DISABLE_OVER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
			   filter->audio_value >= filter->threshold &&
			   obs_source_enabled(filter->target_source)) {
			obs_source_set_enabled(filter->target_source, false);
		} else if ((filter->threshold_action ==
				    THRESHOLD_DISABLE_UNDER ||
			    filter->threshold_action ==
				    THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
			   filter->audio_value < filter->threshold &&
			   obs_source_enabled(filter->target_source)) {
			obs_source_set_enabled(filter->target_source, false);
		}
	} else if (filter->action == VALUE_ACTION_SETTING &&
		   filter->setting_name && strlen(filter->setting_name)) {
		if (!filter->target_source) {
			obs_data_t *settings =
				obs_source_get_settings(filter->source);
			audio_move_update(filter, settings);
			obs_data_release(settings);
		}
		if (!filter->target_source)
			return;
		obs_data_t *settings =
			obs_source_get_settings(filter->target_source);
		const double val = filter->factor * filter->audio_value +
				   filter->base_value;
		obs_data_item_t *setting =
			obs_data_item_byname(settings, filter->setting_name);
		if (setting) {
			const enum obs_data_number_type num_type =
				obs_data_item_numtype(setting);
			if (num_type == OBS_DATA_NUM_INT) {
				obs_data_item_set_int(&setting, val);
			} else if (num_type == OBS_DATA_NUM_DOUBLE) {
				obs_data_item_set_double(&setting, val);
			}
			obs_data_item_release(&setting);
		} else {
			obs_data_set_double(settings, filter->setting_name,
					    val);
		}
		obs_data_release(settings);
		obs_source_update(filter->target_source, NULL);
	}
}
struct obs_source_info audio_move_filter = {
	.id = AUDIO_MOVE_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = audio_move_get_name,
	.create = audio_move_create,
	.destroy = audio_move_destroy,
	.get_properties = audio_move_properties,
	.get_defaults = audio_move_defaults,
	.update = audio_move_update,
	.load = audio_move_update,
	.video_tick = audio_move_tick,
};
