#include "move-transition.h"
#include <obs-module.h>
#include <util/dstr.h>

struct move_source_info {
	obs_source_t *source;
	char *source_name;
	char *filter_name;
	obs_sceneitem_t *scene_item;
	obs_hotkey_id move_start_hotkey;

	long long easing;
	long long easing_function;
	float curve;

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
	uint64_t duration;
	bool moving;
	float running_duration;
	uint32_t canvas_width;
	uint32_t canvas_height;
};

bool find_sceneitem(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	struct move_source_info *move_source = data;
	const char *name =
		obs_source_get_name(obs_sceneitem_get_source(scene_item));
	if (strcmp(name, move_source->source_name) == 0) {
		obs_sceneitem_addref(scene_item);
		move_source->scene_item = scene_item;
		return false;
	}
	return true;
}
void move_source_start(struct move_source_info *move_source)
{
	if (!move_source->scene_item && move_source->source_name) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		obs_scene_t *scene = obs_scene_from_source(parent);
		obs_scene_enum_items(scene, find_sceneitem, move_source);
	}
	if (!move_source->scene_item)
		return;
	move_source->rot_from = obs_sceneitem_get_rot(move_source->scene_item);
	obs_sceneitem_get_pos(move_source->scene_item, &move_source->pos_from);
	obs_sceneitem_get_scale(move_source->scene_item,
				&move_source->scale_from);
	obs_sceneitem_get_bounds(move_source->scene_item,
				 &move_source->bounds_from);
	obs_sceneitem_get_crop(move_source->scene_item,
			       &move_source->crop_from);
	obs_source_t *scene_source = obs_scene_get_source(
		obs_sceneitem_get_scene(move_source->scene_item));
	move_source->canvas_width = obs_source_get_width(scene_source);
	move_source->canvas_height = obs_source_get_height(scene_source);
	move_source->running_duration = 0.0f;
	move_source->moving = true;
}

bool move_source_start_button(obs_properties_t *props, obs_property_t *property,
			      void *data)
{
	struct move_source_info *move_source = data;
	move_source_start(move_source);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

void move_source_start_hotkey(void *data, obs_hotkey_id id,
			      obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	struct move_source_info *move_source = data;
	move_source_start(move_source);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
}

void move_source_update(void *data, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (!move_source->source_name ||
	    strcmp(move_source->source_name, source_name) != 0) {
		bfree(move_source->source_name);
		move_source->source_name = bstrdup(source_name);
		obs_sceneitem_release(move_source->scene_item);
		move_source->scene_item = NULL;
		obs_scene_enum_items(scene, find_sceneitem, data);
	}
	const char *filter_name = obs_source_get_name(move_source->source);
	if (!move_source->filter_name ||
	    strcmp(move_source->filter_name, filter_name) != 0) {
		bfree(move_source->filter_name);

		move_source->filter_name = bstrdup(filter_name);
		obs_hotkey_unregister(move_source->move_start_hotkey);
		move_source->move_start_hotkey = obs_hotkey_register_source(
			parent, move_source->filter_name,
			move_source->filter_name, move_source_start_hotkey,
			data);
	}
	move_source->duration = obs_data_get_int(settings, S_DURATION);
	move_source->curve =
		(float)obs_data_get_double(settings, S_CURVE_MATCH);
	move_source->easing = obs_data_get_int(settings, S_EASING_MATCH);
	move_source->easing_function =
		obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move_source->rot_to = (float)obs_data_get_double(settings, S_ROT);
	obs_data_get_vec2(settings, S_POS, &move_source->pos_to);
	obs_data_get_vec2(settings, S_SCALE, &move_source->scale_to);
	obs_data_get_vec2(settings, S_BOUNDS, &move_source->bounds_to);
	move_source->crop_to.left =
		(int)obs_data_get_int(settings, S_CROP_LEFT);
	move_source->crop_to.top = (int)obs_data_get_int(settings, S_CROP_TOP);
	move_source->crop_to.right =
		(int)obs_data_get_int(settings, S_CROP_RIGHT);
	move_source->crop_to.bottom =
		(int)obs_data_get_int(settings, S_CROP_BOTTOM);
}

void update_transform_text(obs_data_t *settings)
{
	struct dstr transform_text;
	dstr_init(&transform_text);
	struct dstr buffer;
	dstr_init(&buffer);

	struct vec2 pos;
	obs_data_get_vec2(settings, S_POS, &pos);
	dstr_printf(&buffer, "pos: {%.1f,%.1f}", pos.x, pos.y);
	dstr_cat_dstr(&transform_text, &buffer);
	const double rot = obs_data_get_double(settings, S_ROT);
	dstr_printf(&buffer, ", rot: %.1f", rot);
	dstr_cat_dstr(&transform_text, &buffer);
	struct vec2 scale;
	obs_data_get_vec2(settings, S_SCALE, &scale);
	dstr_printf(&buffer, ", scale: {%.1f,%.1f}", scale.x, scale.y);
	dstr_cat_dstr(&transform_text, &buffer);
	struct vec2 bounds;
	obs_data_get_vec2(settings, S_BOUNDS, &bounds);
	dstr_printf(&buffer, ", bounds: {%.1f,%.1f}", bounds.x, bounds.y);
	dstr_cat_dstr(&transform_text, &buffer);

	dstr_printf(&buffer, ", crop: {%d,%d,%d,%d}",
		    (int)obs_data_get_int(settings, S_CROP_LEFT),
		    (int)obs_data_get_int(settings, S_CROP_TOP),
		    (int)obs_data_get_int(settings, S_CROP_RIGHT),
		    (int)obs_data_get_int(settings, S_CROP_BOTTOM));
	dstr_cat_dstr(&transform_text, &buffer);
	obs_data_set_string(settings, S_TRANSFORM_TEXT, transform_text.array);
	dstr_free(&buffer);
	dstr_free(&transform_text);
}

static void *move_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_source_info *move_source =
		bzalloc(sizeof(struct move_source_info));
	move_source->source = source;
	move_source->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
	move_source_update(move_source, settings);
	update_transform_text(settings);
	return move_source;
}

static void move_source_destroy(void *data)
{
	struct move_source_info *move_source = data;
	bfree(move_source);
}

bool move_source_get_transform(obs_properties_t *props,
			       obs_property_t *property, void *data)
{
	struct move_source_info *move_source = data;
	bool settings_changed = false;
	if (!move_source->scene_item && move_source->source_name) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		obs_scene_t *scene = obs_scene_from_source(parent);
		obs_scene_enum_items(scene, find_sceneitem, data);
	}
	if (!move_source->scene_item)
		return settings_changed;
	settings_changed = true;
	obs_data_t *settings = obs_source_get_settings(move_source->source);
	obs_data_set_double(settings, S_ROT,
			    obs_sceneitem_get_rot(move_source->scene_item));
	struct vec2 pos;
	obs_sceneitem_get_pos(move_source->scene_item, &pos);
	obs_data_set_vec2(settings, S_POS, &pos);
	struct vec2 scale;
	obs_sceneitem_get_scale(move_source->scene_item, &scale);
	obs_data_set_vec2(settings, S_SCALE, &scale);
	struct vec2 bounds;
	obs_sceneitem_get_bounds(move_source->scene_item, &bounds);
	obs_data_set_vec2(settings, S_BOUNDS, &bounds);
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(move_source->scene_item, &crop);
	obs_data_set_int(settings, S_CROP_LEFT, crop.left);
	obs_data_set_int(settings, S_CROP_TOP, crop.top);
	obs_data_set_int(settings, S_CROP_RIGHT, crop.right);
	obs_data_set_int(settings, S_CROP_BOTTOM, crop.bottom);
	update_transform_text(settings);
	obs_data_release(settings);

	return settings_changed;
}

bool move_source_changed(void *data, obs_properties_t *props,
			 obs_property_t *property, obs_data_t *settings)
{
	struct move_source_info *move_source = data;
	bool refresh = false;

	const char *source_name = obs_data_get_string(settings, S_SOURCE);
	if (move_source->source_name &&
	    strcmp(move_source->source_name, source_name) == 0)
		return refresh;
	bfree(move_source->source_name);
	move_source->source_name = bstrdup(source_name);
	obs_sceneitem_release(move_source->scene_item);
	move_source->scene_item = NULL;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	obs_scene_enum_items(scene, find_sceneitem, data);
	refresh = move_source_get_transform(props, property, data);
	return refresh;
}

bool prop_list_add_source(obs_scene_t *scene, obs_sceneitem_t *item,
			  void *data);
void prop_list_add_easings(obs_property_t *p);
void prop_list_add_easing_functions(obs_property_t *p);

static obs_properties_t *move_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct move_source_info *move_source = data;
	obs_source_t *parent = obs_filter_get_parent(move_source->source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene) {
		obs_properties_add_button(ppts, "warning",
					  obs_module_text("ScenesOnlyFilter"),
					  NULL);
		return ppts;
	}
	obs_property_t *p = obs_properties_add_list(ppts, S_SOURCE,
						    obs_module_text("Source"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_scene_enum_items(scene, prop_list_add_source, p);
	obs_property_set_modified_callback2(p, move_source_changed, data);

	p = obs_properties_add_text(ppts, S_TRANSFORM_TEXT,
				    obs_module_text("Transform"),
				    OBS_TEXT_DEFAULT);
	obs_property_set_enabled(p, false);
	obs_properties_add_button(ppts, "transform_get",
				  obs_module_text("GetTransform"),
				  move_source_get_transform);

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

	obs_properties_add_float_slider(
		ppts, S_CURVE_MATCH, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_button(ppts, "move_source_start",
				  obs_module_text("Start"),
				  move_source_start_button);
	return ppts;
}
void move_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_DURATION, 300);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH,
				 EASING_CUBIC);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
}

void move_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct move_source_info *filter = data;
	obs_source_skip_video_filter(filter->source);
}

static const char *move_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveSourceFilter");
}

float get_eased(float f, long long easing, long long easing_function);
void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control,
		 struct vec2 *end, const float t);

void move_source_tick(void *data, float seconds)
{
	struct move_source_info *move_source = data;

	if (move_source->move_start_hotkey == OBS_INVALID_HOTKEY_ID &&
	    move_source->filter_name) {
		obs_source_t *parent =
			obs_filter_get_parent(move_source->source);
		if (parent)
			move_source->move_start_hotkey =
				obs_hotkey_register_source(
					parent, move_source->filter_name,
					move_source->filter_name,
					move_source_start_hotkey, data);
	}

	if (!move_source->moving)
		return;

	if (!move_source->scene_item || !move_source->duration) {
		move_source->moving = false;
		return;
	}
	move_source->running_duration += seconds;
	float t = move_source->running_duration * 1000.0f /
		  (float)move_source->duration;
	if (t >= 1.0f) {
		t = 1.0f;
		move_source->moving = false;
	}
	t = get_eased(t, move_source->easing, move_source->easing_function);

	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	struct vec2 pos;
	if (move_source->curve != 0.0f) {
		float diff_x =
			fabsf(move_source->pos_from.x - move_source->pos_to.x);
		float diff_y =
			fabsf(move_source->pos_from.y - move_source->pos_to.y);
		struct vec2 control_pos;
		vec2_set(&control_pos,
			 0.5f * move_source->pos_from.x +
				 0.5f * move_source->pos_to.x,
			 0.5f * move_source->pos_from.y +
				 0.5f * move_source->pos_to.y);
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
		vec2_bezier(&pos, &move_source->pos_from, &control_pos,
			    &move_source->pos_to, t);
	} else {
		vec2_set(&pos,
			 (1.0f - t) * move_source->pos_from.x +
				 t * move_source->pos_to.x,
			 (1.0f - t) * move_source->pos_from.y +
				 t * move_source->pos_to.y);
	}
	obs_sceneitem_defer_update_begin(move_source->scene_item);
	obs_sceneitem_set_pos(move_source->scene_item, &pos);
	const float rot =
		(1.0f - t) * move_source->rot_from + t * move_source->rot_to;
	obs_sceneitem_set_rot(move_source->scene_item, rot);
	struct vec2 scale;
	vec2_set(&scale,
		 (1.0f - t) * move_source->scale_from.x +
			 t * move_source->scale_to.x,
		 (1.0f - t) * move_source->scale_from.y +
			 t * move_source->scale_to.y);
	obs_sceneitem_set_scale(move_source->scene_item, &scale);
	struct vec2 bounds;
	vec2_set(&bounds,
		 (1.0f - t) * move_source->bounds_from.x +
			 t * move_source->bounds_to.x,
		 (1.0f - t) * move_source->bounds_from.y +
			 t * move_source->bounds_to.y);
	obs_sceneitem_set_bounds(move_source->scene_item, &bounds);
	struct obs_sceneitem_crop crop;
	crop.left =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.left +
		      ot * (float)move_source->crop_to.left);
	crop.top =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.top +
		      ot * (float)move_source->crop_to.top);
	crop.right =
		(int)((float)(1.0f - ot) * (float)move_source->crop_from.right +
		      ot * (float)move_source->crop_to.right);
	crop.bottom = (int)((float)(1.0f - ot) *
				    (float)move_source->crop_from.bottom +
			    ot * (float)move_source->crop_to.bottom);
	obs_sceneitem_set_crop(move_source->scene_item, &crop);
	obs_sceneitem_defer_update_end(move_source->scene_item);
}

struct obs_source_info move_source_filter = {
	.id = "move_source_filter",
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
};
