#include "move-transition.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "graphics/math-defs.h"
#include "graphics/matrix4.h"
#include "easing.h"
#include <util/threading.h>
#include "obs-websocket-api.h"

struct move_info {
	obs_source_t *source;
	bool start_init;
	bool first_frame;
	DARRAY(struct move_item *) items_a;
	DARRAY(struct move_item *) items_b;
	float t;
	float curve_move;
	float curve_in;
	float curve_out;
	obs_source_t *scene_source_a;
	obs_source_t *scene_source_b;
	gs_samplerstate_t *point_sampler;
	long long easing_move;
	long long easing_in;
	long long easing_out;
	long long easing_function_move;
	long long easing_function_in;
	long long easing_function_out;
	float zoom_in;
	float zoom_out;
	long long position_in;
	long long position_out;
	char *transition_move;
	char *transition_in;
	char *transition_out;
	bool part_match;
	bool number_match;
	bool last_word_match;
	enum obs_transition_scale_type transition_move_scale;
	size_t item_pos;
	uint32_t matched_items;
	bool matched_scene_a;
	bool matched_scene_b;
	uint32_t item_order_switch_percentage;
	bool nested_scenes;
	bool cache_transitions;
	DARRAY(obs_source_t *) transition_pool_move;
	size_t transition_pool_move_index;
	DARRAY(obs_source_t *) transition_pool_in;
	size_t transition_pool_in_index;
	DARRAY(obs_source_t *) transition_pool_out;
	size_t transition_pool_out_index;

	bool scene_flip_horizontal;
	bool scene_flip_vertical;
};

DARRAY(struct move_info *) move_rendering;
DARRAY(char *) move_render_filter_ids;

struct move_item {
	obs_sceneitem_t *item_a;
	obs_sceneitem_t *item_b;
	gs_texrender_t *item_render;
	obs_source_t *transition;
	long long easing;
	long long easing_function;
	float zoom;
	long long position;
	char *transition_name;
	enum obs_transition_scale_type transition_scale;
	float curve;
	bool move_scene;
	int start_percentage;
	int end_percentage;
	obs_scene_t *release_scene_a;
	obs_scene_t *release_scene_b;
	bool scene_flip_horizontal;
	bool scene_flip_vertical;
	struct matrix4 transform_a;
	struct matrix4 transform_b;
	struct obs_sceneitem_crop bounds_crop_a;
	struct obs_sceneitem_crop bounds_crop_b;
	bool move_filter_a;
	bool move_filter_b;
};

static const struct {
	enum gs_blend_type src_color;
	enum gs_blend_type src_alpha;
	enum gs_blend_type dst_color;
	enum gs_blend_type dst_alpha;
	enum gs_blend_op_type op;
} obs_blend_mode_params[] = {
	/* clang-format off */
	// OBS_BLEND_NORMAL
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_INVSRCALPHA,
		GS_BLEND_INVSRCALPHA,
		GS_BLEND_OP_ADD,
	},
	// OBS_BLEND_ADDITIVE
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_OP_ADD,
	},
	// OBS_BLEND_SUBTRACT
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_OP_REVERSE_SUBTRACT,
	},
	// OBS_BLEND_SCREEN
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_INVSRCCOLOR,
		GS_BLEND_INVSRCALPHA,
		GS_BLEND_OP_ADD
	},
	// OBS_BLEND_MULTIPLY
	{
		GS_BLEND_DSTCOLOR,
		GS_BLEND_DSTALPHA,
		GS_BLEND_INVSRCALPHA,
		GS_BLEND_INVSRCALPHA,
		GS_BLEND_OP_ADD
	},
	// OBS_BLEND_LIGHTEN
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_OP_MAX,
	},
	// OBS_BLEND_DARKEN
	{
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_ONE,
		GS_BLEND_OP_MIN,
	},
	/* clang-format on */
};

static const char *move_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Move");
}

static void *move_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct move_info *move = bzalloc(sizeof(struct move_info));
	move->source = source;
	da_init(move->items_a);
	da_init(move->items_b);
	da_init(move->transition_pool_out);
	da_init(move->transition_pool_in);
	da_init(move->transition_pool_out);
	obs_source_update(source, settings);
	return move;
}

static void clear_items(struct move_info *move, bool in_graphics)
{
	bool graphics = false;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (item->item_render) {
			if (!graphics && !in_graphics) {
				obs_enter_graphics();
				graphics = true;
			}
			gs_texrender_destroy(item->item_render);
			item->item_render = NULL;
		}
	}
	if (graphics)
		obs_leave_graphics();

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (item->release_scene_a)
			obs_scene_release(item->release_scene_a);
		item->release_scene_a = NULL;
		obs_sceneitem_release(item->item_a);
		item->item_a = NULL;
		if (item->release_scene_b)
			obs_scene_release(item->release_scene_b);
		item->release_scene_b = NULL;
		obs_sceneitem_release(item->item_b);
		item->item_b = NULL;

		if (item->transition) {
			obs_source_remove_active_child(move->source, item->transition);
			obs_transition_force_stop(item->transition);
			obs_transition_clear(item->transition);
			obs_source_release(item->transition);
			item->transition = NULL;
		}
		bfree(item->transition_name);
		bfree(item);
	}
	move->items_a.num = 0;
	move->items_b.num = 0;
}

void clear_transition_pool(void *data)
{
	DARRAY(obs_source_t *) *transition_pool = data;
	for (size_t i = 0; i < transition_pool->num; i++) {
		obs_source_release(transition_pool->array[i]);
	}
	transition_pool->num = 0;
}

static void move_destroy(void *data)
{
	struct move_info *move = data;
	clear_items(move, false);
	da_free(move->items_a);
	da_free(move->items_b);
	clear_transition_pool(&move->transition_pool_move);
	da_free(move->transition_pool_move);
	clear_transition_pool(&move->transition_pool_in);
	da_free(move->transition_pool_in);
	clear_transition_pool(&move->transition_pool_out);
	da_free(move->transition_pool_out);
	obs_source_release(move->scene_source_a);
	obs_source_release(move->scene_source_b);
	bfree(move->transition_in);
	bfree(move->transition_out);
	bfree(move->transition_move);
	if (move->point_sampler) {
		obs_enter_graphics();
		gs_samplerstate_destroy(move->point_sampler);
		obs_leave_graphics();
	}
	bfree(move);
}

static void move_update(void *data, obs_data_t *settings)
{
	struct move_info *move = data;
	move->easing_move = obs_data_get_int(settings, S_EASING_MATCH);
	move->easing_in = obs_data_get_int(settings, S_EASING_IN);
	move->easing_out = obs_data_get_int(settings, S_EASING_OUT);
	move->easing_function_move = obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move->easing_function_in = obs_data_get_int(settings, S_EASING_FUNCTION_IN);
	move->easing_function_out = obs_data_get_int(settings, S_EASING_FUNCTION_OUT);
	move->position_in = obs_data_get_int(settings, S_POSITION_IN);
	obs_data_item_t *item = obs_data_item_byname(settings, S_ZOOM_IN);
	if (obs_data_item_gettype(item) == OBS_DATA_BOOLEAN) {
		obs_data_set_double(settings, S_ZOOM_IN, obs_data_get_bool(settings, S_ZOOM_IN) ? 0.0 : 100.0);
	}
	obs_data_item_release(&item);
	move->zoom_in = (float)obs_data_get_double(settings, S_ZOOM_IN) / 100.0f;
	move->position_out = obs_data_get_int(settings, S_POSITION_OUT);
	item = obs_data_item_byname(settings, S_ZOOM_IN);
	if (obs_data_item_gettype(item) == OBS_DATA_BOOLEAN) {
		obs_data_set_double(settings, S_ZOOM_OUT, obs_data_get_bool(settings, S_ZOOM_OUT) ? 0.0 : 100.0);
	}
	obs_data_item_release(&item);
	move->zoom_out = (float)obs_data_get_double(settings, S_ZOOM_OUT) / 100.0f;
	move->curve_move = (float)obs_data_get_double(settings, S_CURVE_MATCH);
	move->curve_in = (float)obs_data_get_double(settings, S_CURVE_IN);
	move->curve_out = (float)obs_data_get_double(settings, S_CURVE_OUT);
	bfree(move->transition_in);
	move->transition_in = bstrdup(obs_data_get_string(settings, S_TRANSITION_IN));
	if (move->transition_in && strlen(move->transition_in) && move->transition_pool_in.num &&
	    strcmp(obs_source_get_name(move->transition_pool_in.array[0]), move->transition_in) != 0) {
		clear_transition_pool(&move->transition_pool_in);
	}
	bfree(move->transition_out);
	move->transition_out = bstrdup(obs_data_get_string(settings, S_TRANSITION_OUT));
	if (move->transition_out && strlen(move->transition_out) && move->transition_pool_out.num &&
	    strcmp(obs_source_get_name(move->transition_pool_out.array[0]), move->transition_out) != 0) {
		clear_transition_pool(&move->transition_pool_out);
	}
	move->part_match = obs_data_get_bool(settings, S_NAME_PART_MATCH);
	move->number_match = obs_data_get_bool(settings, S_NAME_NUMBER_MATCH);
	move->last_word_match = obs_data_get_bool(settings, S_NAME_LAST_WORD_MATCH);
	bfree(move->transition_move);
	move->transition_move = bstrdup(obs_data_get_string(settings, S_TRANSITION_MATCH));
	if (move->transition_move && strlen(move->transition_move) && move->transition_pool_move.num &&
	    strcmp(obs_source_get_name(move->transition_pool_move.array[0]), move->transition_move) != 0) {
		clear_transition_pool(&move->transition_pool_move);
	}
	move->transition_move_scale = obs_data_get_int(settings, S_TRANSITION_SCALE);
	move->item_order_switch_percentage = (uint32_t)obs_data_get_int(settings, S_SWITCH_PERCENTAGE);
	move->cache_transitions = obs_data_get_bool(settings, S_CACHE_TRANSITIONS);
	move->nested_scenes = obs_data_get_bool(settings, S_NESTED_SCENES);
}

void add_alignment(struct vec2 *v, uint32_t align, int32_t cx, int32_t cy)
{
	if (align & OBS_ALIGN_RIGHT)
		v->x += (float)cx;
	else if ((align & OBS_ALIGN_LEFT) == 0)
		v->x += (float)(cx >> 1);

	if (align & OBS_ALIGN_BOTTOM)
		v->y += (float)cy;
	else if ((align & OBS_ALIGN_TOP) == 0)
		v->y += (float)(cy >> 1);
}

void add_move_alignment(struct vec2 *v, uint32_t align_a, uint32_t align_b, float t, int32_t cx, int32_t cy)
{
	if (align_a & OBS_ALIGN_RIGHT)
		v->x += (float)cx * (1.0f - t);
	else if ((align_a & OBS_ALIGN_LEFT) == 0)
		v->x += (float)(cx >> 1) * (1.0f - t);

	if (align_a & OBS_ALIGN_BOTTOM)
		v->y += (float)cy * (1.0f - t);
	else if ((align_a & OBS_ALIGN_TOP) == 0)
		v->y += (float)(cy >> 1) * (1.0f - t);

	if (align_b & OBS_ALIGN_RIGHT)
		v->x += (float)cx * t;
	else if ((align_b & OBS_ALIGN_LEFT) == 0)
		v->x += (float)(cx >> 1) * t;

	if (align_b & OBS_ALIGN_BOTTOM)
		v->y += (float)cy * t;
	else if ((align_b & OBS_ALIGN_TOP) == 0)
		v->y += (float)(cy >> 1) * t;
}

static bool crop_to_bounds(const obs_sceneitem_t *item, enum obs_bounds_type bt)
{
	if (bt != OBS_BOUNDS_SCALE_OUTER && bt != OBS_BOUNDS_SCALE_TO_HEIGHT && bt != OBS_BOUNDS_SCALE_TO_WIDTH)
		return false;
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 1, 0)
	return obs_sceneitem_get_bounds_crop(item);
#else
	if (obs_get_version() < MAKE_SEMANTIC_VERSION(30, 1, 0))
		return false;
	obs_source_t *item_source = obs_sceneitem_get_source(item);
	if (!item_source)
		return false;
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *scene_source = obs_scene_get_source(scene);
	if (!scene_source)
		return false;
	obs_data_t *settings = obs_source_get_settings(scene_source);
	obs_data_array_t *items = obs_data_get_array(settings, "items");
	obs_data_release(settings);
	if (!items)
		return false;
	size_t count = obs_data_array_count(items);
	bool crop_to_bounds = false;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item_data = obs_data_array_item(items, i);
		if (obs_sceneitem_get_id(item) == obs_data_get_int(item_data, "id") &&
		    strcmp(obs_data_get_string(item_data, "name"), obs_source_get_name(item_source)) == 0) {
			crop_to_bounds = obs_data_get_bool(item_data, "bounds_crop");
			obs_data_release(item_data);
			break;
		}
		obs_data_release(item_data);
	}
	obs_data_array_release(items);
	return crop_to_bounds;
#endif
}

static void calculate_bounds_data(struct obs_scene_item *item, struct vec2 *origin, struct vec2 *scale, int32_t *cx, int32_t *cy,
				  struct vec2 *bounds)
{
	float width = (float)(*cx) * fabsf(scale->x);
	float height = (float)(*cy) * fabsf(scale->y);
	const float item_aspect = width / height;
	const float bounds_aspect = bounds->x / bounds->y;
	uint32_t bounds_type = obs_sceneitem_get_bounds_type(item);

	if (bounds_type == OBS_BOUNDS_MAX_ONLY)
		if (width > bounds->x || height > bounds->y)
			bounds_type = OBS_BOUNDS_SCALE_INNER;

	if (bounds_type == OBS_BOUNDS_SCALE_INNER || bounds_type == OBS_BOUNDS_SCALE_OUTER) {
		bool use_width = (bounds_aspect < item_aspect);

		if (bounds_type == OBS_BOUNDS_SCALE_OUTER)
			use_width = !use_width;

		const float mul = use_width ? bounds->x / width : bounds->y / height;

		vec2_mulf(scale, scale, mul);

	} else if (bounds_type == OBS_BOUNDS_SCALE_TO_WIDTH) {
		vec2_mulf(scale, scale, bounds->x / width);

	} else if (bounds_type == OBS_BOUNDS_SCALE_TO_HEIGHT) {
		vec2_mulf(scale, scale, bounds->y / height);

	} else if (bounds_type == OBS_BOUNDS_STRETCH) {
		scale->x = copysignf(bounds->x / (float)(*cx), scale->x);
		scale->y = copysignf(bounds->y / (float)(*cy), scale->y);
	}

	width = (float)(*cx) * scale->x;
	height = (float)(*cy) * scale->y;

	/* Disregards flip when calculating size diff */
	const float width_diff = bounds->x - width;
	const float height_diff = bounds->y - height;
	*cx = (uint32_t)bounds->x;
	*cy = (uint32_t)bounds->y;

	add_alignment(origin, obs_sceneitem_get_bounds_alignment(item), (int32_t)-roundf(width_diff),
		      (int32_t)-roundf(height_diff));

	/* Set cropping if enabled and large enough size difference exists */
	if (crop_to_bounds(item, bounds_type) && (width_diff < -0.1 || height_diff < -0.1)) {
		bool crop_width = width_diff < -0.1;
		bool crop_flipped = crop_width ? width < 0.0f : height < 0.0f;

		float crop_diff = crop_width ? width_diff : height_diff;
		float crop_origin = crop_width ? origin->x : origin->y;

		/* Only get alignment for relevant axis */
		uint32_t crop_align_mask = crop_width ? OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT : OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
		uint32_t crop_align = obs_sceneitem_get_bounds_alignment(item) & crop_align_mask;

		if (crop_flipped) {
			/* Adjust origin for flips */
			if (crop_align == OBS_ALIGN_CENTER)
				crop_origin *= 2;
			else if (crop_align & (OBS_ALIGN_TOP | OBS_ALIGN_LEFT))
				crop_origin -= crop_diff;
		} else {
			crop_origin = 0;
		}

		if (crop_width) {
			origin->x = crop_origin;
		} else {
			origin->y = crop_origin;
		}
	}

	/* Makes the item stay in-place in the box if flipped */
	origin->x += (width < 0.0f) ? width : 0.0f;
	origin->y += (height < 0.0f) ? height : 0.0f;
}

static void calculate_move_bounds_data(struct obs_scene_item *item_a, struct obs_scene_item *item_b, float t, struct vec2 *origin,
				       struct vec2 *scale, int32_t *cx, int32_t *cy, struct vec2 *bounds)
{
	struct vec2 origin_a;
	vec2_set(&origin_a, origin->x, origin->y);
	struct vec2 origin_b;
	vec2_set(&origin_b, origin->x, origin->y);

	struct vec2 scale_a;
	vec2_set(&scale_a, scale->x, scale->y);
	struct vec2 scale_b;
	vec2_set(&scale_b, scale->x, scale->y);
	int32_t cxa = *cx;
	int32_t cxb = *cx;
	int32_t cya = *cy;
	int32_t cyb = *cy;
	calculate_bounds_data(item_a, &origin_a, &scale_a, &cxa, &cya, bounds);
	calculate_bounds_data(item_b, &origin_b, &scale_b, &cxb, &cyb, bounds);
	vec2_set(origin, origin_a.x * (1.0f - t) + origin_b.x * t, origin_a.y * (1.0f - t) + origin_b.y * t);
	vec2_set(scale, scale_a.x * (1.0f - t) + scale_b.x * t, scale_a.y * (1.0f - t) + scale_b.y * t);
	*cx = (int32_t)roundf((float)cxa * (1.0f - t) + (float)cxb * t);
	*cy = (int32_t)roundf((float)cya * (1.0f - t) + (float)cyb * t);
}

static inline bool item_is_scene(struct obs_scene_item *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	return source && obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE;
}

static inline bool scale_filter_enabled(struct obs_scene_item *item)
{
	return obs_sceneitem_get_scale_filter(item) != OBS_SCALE_DISABLE;
}

static inline bool crop_enabled(const struct obs_sceneitem_crop *crop)
{
	return crop->left || crop->right || crop->top || crop->bottom;
}

static inline bool default_blending_enabled(struct obs_scene_item *item)
{
	return obs_sceneitem_get_blending_mode(item) == OBS_BLEND_NORMAL;
}

static inline bool item_texture_enabled(struct obs_scene_item *item)
{
	if (!item)
		return false;
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	if (crop_enabled(&crop))
		return true;
	if (item_is_scene(item) && !obs_sceneitem_is_group(item))
		return true;
	if (scale_filter_enabled(item) || !default_blending_enabled(item))
		return true;
	if (obs_sceneitem_get_blending_method(item) == OBS_BLEND_METHOD_SRGB_OFF)
		return true;
	if (crop_to_bounds(item, obs_sceneitem_get_bounds_type(item)))
		return true;
	return false;
}

void pos_add_center(struct vec2 *pos, uint32_t alignment, uint32_t cx, uint32_t cy)
{
	if (alignment & OBS_ALIGN_LEFT) {
		pos->x -= cx >> 1;
	} else if (alignment & OBS_ALIGN_RIGHT) {
		pos->x += cx >> 1;
	}
	if (alignment & OBS_ALIGN_TOP) {
		pos->y -= cy >> 1;
	} else if (alignment & OBS_ALIGN_BOTTOM) {
		pos->y += cy >> 1;
	}
}

void pos_subtract_center(struct vec2 *pos, uint32_t alignment, int32_t cx, int32_t cy)
{
	if (alignment & OBS_ALIGN_LEFT) {
		pos->x += cx >> 1;
	} else if (alignment & OBS_ALIGN_RIGHT) {
		pos->x -= cx >> 1;
	}
	if (alignment & OBS_ALIGN_TOP) {
		pos->y += cy >> 1;
	} else if (alignment & OBS_ALIGN_BOTTOM) {
		pos->y -= cy >> 1;
	}
}

void calc_edge_position(struct vec2 *pos, long long position, uint32_t canvas_width, uint32_t canvas_height, uint32_t alignment,
			int32_t cx, int32_t cy, float zoom)
{
	int32_t cx2 = abs(cx >> 1);
	int32_t cy2 = abs(cy >> 1);
	if (zoom != 1.0f) {
		cx2 = (int32_t)fabs((float)cx * zoom / 2.0f);
		cy2 = (int32_t)fabs((float)cy * zoom / 2.0f);
	}

	if (position - POS_EDGE == 0) {
		if (alignment & OBS_ALIGN_LEFT) {
			pos->x += cx >> 1;
		} else if (alignment & OBS_ALIGN_RIGHT) {
			pos->x -= cx >> 1;
		}
		if (alignment & OBS_ALIGN_TOP) {
			pos->y += cy >> 1;
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y -= cy >> 1;
		}
		// pos is center of object
		float diff_x = pos->x - (canvas_width >> 1);
		float diff_y = pos->y - (canvas_height >> 1);
		float factor_x = fabsf(diff_x) / (canvas_width >> 1);
		float factor_y = fabsf(diff_y) / (canvas_height >> 1);

		if (diff_x == 0.0f && diff_y == 0.0f) {
			diff_y = 1.0f;
		}
		if (factor_x > factor_y) {
			if (diff_x < 0.0f) {
				//left edge
				const float move_x = -(pos->x + cx2);
				const float move_y = diff_y * (diff_x + move_x) / diff_x;
				vec2_set(pos, -(float)cx2, (float)(canvas_height >> 1) + move_y);
			} else {
				//right edge
				const float move_x = (canvas_width - pos->x) + cx2;
				const float move_y = diff_y * (diff_x + move_x) / diff_x;
				vec2_set(pos, (float)canvas_width + cx2, (float)(canvas_height >> 1) + move_y);
			}
		} else {
			if (diff_y < 0.0f) {
				//top edge
				const float move_y = -(pos->y + cy2);
				const float move_x = diff_x * (diff_y + move_y) / diff_y;
				vec2_set(pos, (float)(canvas_width >> 1) + move_x, -(float)cy2);
			} else {
				//bottom edge
				const float move_y = (canvas_height - pos->y) + cy2;
				const float move_x = diff_x * (diff_y + move_y) / diff_y;
				vec2_set(pos, (float)(canvas_width >> 1) + move_x, (float)canvas_height + cy2);
			}
		}

		if (alignment & OBS_ALIGN_LEFT) {
			if (cx < 0) {
				pos->x += cx2;
			} else {
				pos->x -= cx2;
			}
		} else if (alignment & OBS_ALIGN_RIGHT) {
			if (cx < 0) {
				pos->x -= cx2;
			} else {
				pos->x += cx2;
			}
		}
		if (alignment & OBS_ALIGN_TOP) {
			if (cy < 0) {
				pos->y += cy2;
			} else {
				pos->y -= cy2;
			}
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			if (cy < 0) {
				pos->y -= cy2;
			} else {
				pos->y += cy2;
			}
		}

		return;
	}
	if (zoom != 1.0f) {
		cx = (int32_t)((float)cx * zoom);
		cy = (int32_t)((float)cy * zoom);
	}
	if (position & POS_EDGE)
		vec2_set(pos, 0, 0);
	if (position & POS_RIGHT) {
		pos->x = (float)canvas_width;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx;
		} else if (alignment & OBS_ALIGN_LEFT) {

		} else {
			pos->x += cx2;
		}
	} else if (position & POS_LEFT) {
		pos->x = 0;
		if (alignment & OBS_ALIGN_RIGHT) {

		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx;
		} else {
			pos->x -= cx2;
		}
	} else if (position & POS_EDGE) {
		pos->x = (float)(canvas_width >> 1);
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx2;
		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx2;
		}
	}

	if (position & POS_BOTTOM) {
		pos->y = (float)canvas_height;
		if (alignment & OBS_ALIGN_TOP) {
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y += cy;
		} else {
			pos->y += cy2;
		}
	} else if (position & POS_TOP) {
		pos->y = 0;
		if (alignment & OBS_ALIGN_BOTTOM) {
		} else if (alignment & OBS_ALIGN_TOP) {
			pos->y -= cy;
		} else {
			pos->y -= cy2;
		}
	} else if (position & POS_EDGE) {
		pos->y = (float)(canvas_height >> 1);
		if (alignment & OBS_ALIGN_TOP) {
			pos->y -= cy2;
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y += cy2;
		}
	}
}

float bezier(float point[], float t, int order)
{
	const float p = 1.0f - t;
	if (order < 1)
		return point[0];
	if (order == 1)
		return p * point[0] + t * point[1];
	return p * bezier(point, t, order - 1) + t * bezier(&point[1], t, order - 1);
}

void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control, struct vec2 *end, const float t)
{
	float x[3] = {begin->x, control->x, end->x};
	float y[3] = {begin->y, control->y, end->y};
	dst->x = bezier(x, t, 2);
	dst->y = bezier(y, t, 2);
}

static obs_source_t *obs_frontend_get_transition(const char *name)
{
	if (!name)
		return NULL;
	struct obs_frontend_source_list transitions = {0};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n = obs_source_get_name(transitions.sources.array[i]);
		if (n && strcmp(n, name) == 0) {
			obs_source_t *transition = obs_source_get_ref(transitions.sources.array[i]);
			obs_frontend_source_list_free(&transitions);
			return transition;
		}
	}
	obs_frontend_source_list_free(&transitions);
	return NULL;
}

float get_eased(float f, long long easing, long long easing_function)
{
	float t = f;
	if (EASE_NONE == easing) {
	} else if (EASE_IN == easing) {
		switch (easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseIn(f);
			break;
		case EASING_CUBIC:
			t = CubicEaseIn(f);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseIn(f);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseIn(f);
			break;
		case EASING_SINE:
			t = SineEaseIn(f);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseIn(f);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseIn(f);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseIn(f);
			break;
		case EASING_BOUNCE:
			t = BounceEaseIn(f);
			break;
		case EASING_BACK:
			t = BackEaseIn(f);
			break;
		default:;
		}
	} else if (EASE_OUT == easing) {
		switch (easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseOut(f);
			break;
		case EASING_CUBIC:
			t = CubicEaseOut(f);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseOut(f);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseOut(f);
			break;
		case EASING_SINE:
			t = SineEaseOut(f);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseOut(f);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseOut(f);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseOut(f);
			break;
		case EASING_BOUNCE:
			t = BounceEaseOut(f);
			break;
		case EASING_BACK:
			t = BackEaseOut(f);
			break;
		default:;
		}
	} else if (EASE_IN_OUT == easing) {
		switch (easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseInOut(f);
			break;
		case EASING_CUBIC:
			t = CubicEaseInOut(f);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseInOut(f);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseInOut(f);
			break;
		case EASING_SINE:
			t = SineEaseInOut(f);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseInOut(f);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseInOut(f);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseInOut(f);
			break;
		case EASING_BOUNCE:
			t = BounceEaseInOut(f);
			break;
		case EASING_BACK:
			t = BackEaseInOut(f);
			break;
		default:;
		}
	}
	return t;
}

obs_source_t *get_transition(const char *transition_name, void *pool_data, size_t *index, bool cache)
{

	if (!transition_name || strlen(transition_name) == 0 || strcmp(transition_name, "None") == 0)
		return NULL;
	DARRAY(obs_source_t *) *transition_pool = pool_data;
	const size_t i = *index;
	if (cache && transition_pool->num && *index < transition_pool->num) {
		obs_source_t *transition = obs_source_get_ref(transition_pool->array[i]);
		*index = i + 1;
		return transition;
	}
	obs_source_t *frontend_transition = obs_frontend_get_transition(transition_name);
	if (!frontend_transition)
		return NULL;
	obs_source_t *transition = obs_source_duplicate(frontend_transition, transition_name, true);
	obs_source_release(frontend_transition);
	if (cache) {
		transition = obs_source_get_ref(transition);
		darray_push_back(sizeof(obs_source_t *), &transition_pool->da, &transition);
		*index = i + 1;
	}
	return transition;
}

float rot_diff(float rot_a, float rot_b)
{
	float diff = rot_b - rot_a;
	while (diff < -180.0f)
		diff += 360.0f;
	while (diff > 180.0f)
		diff -= 360.0f;
	return diff;
}

static void calculate_bounds_item(const obs_sceneitem_t *item, struct vec2 *origin, struct vec2 *scale, uint32_t *cx, uint32_t *cy,
				  float *calc_width, float *calc_height, struct obs_sceneitem_crop *bounds_crop,
				  enum obs_bounds_type bt)
{
	float width = (float)(*cx) * fabsf(scale->x);
	float height = (float)(*cy) * fabsf(scale->y);
	float item_aspect = width / height;
	struct vec2 bounds;
	obs_sceneitem_get_bounds(item, &bounds);

	float bounds_aspect = bounds.x / bounds.y;

	float width_diff, height_diff;

	if (bt == OBS_BOUNDS_MAX_ONLY)
		if (width > bounds.x || height > bounds.y)
			bt = OBS_BOUNDS_SCALE_INNER;

	if (bt == OBS_BOUNDS_SCALE_INNER || bt == OBS_BOUNDS_SCALE_OUTER) {
		bool use_width = (bounds_aspect < item_aspect);
		float mul;

		if (bt == OBS_BOUNDS_SCALE_OUTER)
			use_width = !use_width;

		mul = use_width ? bounds.x / width : bounds.y / height;

		vec2_mulf(scale, scale, mul);

	} else if (bt == OBS_BOUNDS_SCALE_TO_WIDTH) {
		vec2_mulf(scale, scale, bounds.x / width);

	} else if (bt == OBS_BOUNDS_SCALE_TO_HEIGHT) {
		vec2_mulf(scale, scale, bounds.y / height);

	} else if (bt == OBS_BOUNDS_STRETCH) {
		scale->x = copysignf(bounds.x / (float)(*cx), scale->x);
		scale->y = copysignf(bounds.y / (float)(*cy), scale->y);
	}

	width = (float)(*cx) * scale->x;
	height = (float)(*cy) * scale->y;
	*calc_width = fabsf(width);
	*calc_height = fabsf(height);

	/* Disregards flip when calculating size diff */
	width_diff = bounds.x - *calc_width;
	height_diff = bounds.y - *calc_height;
	*cx = (uint32_t)bounds.x;
	*cy = (uint32_t)bounds.y;

	add_alignment(origin, obs_sceneitem_get_bounds_alignment(item), (int)-width_diff, (int)-height_diff);

	/* Set cropping if enabled and large enough size difference exists */
	if (crop_to_bounds(item, bt) && (width_diff < -0.1 || height_diff < -0.1)) {
		bool crop_width = width_diff < -0.1;
		bool crop_flipped = crop_width ? width < 0.0f : height < 0.0f;

		float crop_diff = crop_width ? width_diff : height_diff;
		float crop_scale = crop_width ? scale->x : scale->y;
		float crop_origin = crop_width ? origin->x : origin->y;

		/* Only get alignment for relevant axis */
		uint32_t crop_align_mask = crop_width ? OBS_ALIGN_LEFT | OBS_ALIGN_RIGHT : OBS_ALIGN_TOP | OBS_ALIGN_BOTTOM;
		uint32_t crop_align = obs_sceneitem_get_bounds_alignment(item) & crop_align_mask;

		/* Cropping values need to scaled to input source */
		float overdraw = fabsf(crop_diff / crop_scale);

		/* tl = top / left, br = bottom / right */
		float overdraw_tl;
		if (crop_align & (OBS_ALIGN_TOP | OBS_ALIGN_LEFT))
			overdraw_tl = 0;
		else if (crop_align & (OBS_ALIGN_BOTTOM | OBS_ALIGN_RIGHT))
			overdraw_tl = overdraw;
		else
			overdraw_tl = overdraw / 2;

		float overdraw_br = overdraw - overdraw_tl;

		int crop_br, crop_tl;
		if (crop_flipped) {
			/* Adjust origin for flips */
			if (crop_align == OBS_ALIGN_CENTER)
				crop_origin *= 2;
			else if (crop_align & (OBS_ALIGN_TOP | OBS_ALIGN_LEFT))
				crop_origin -= crop_diff;

			/* Note that crops are swapped if the axis is flipped */
			crop_br = (int)roundf(overdraw_tl);
			crop_tl = (int)roundf(overdraw_br);
		} else {
			crop_origin = 0;
			crop_br = (int)roundf(overdraw_br);
			crop_tl = (int)roundf(overdraw_tl);
		}

		if (crop_width) {
			bounds_crop->right = crop_br;
			bounds_crop->left = crop_tl;
			origin->x = crop_origin;
		} else {
			bounds_crop->bottom = crop_br;
			bounds_crop->top = crop_tl;
			origin->y = crop_origin;
		}
	}

	/* Makes the item stay in-place in the box if flipped */
	origin->x += (width < 0.0f) ? width : 0.0f;
	origin->y += (height < 0.0f) ? height : 0.0f;
}

void move_get_draw_transform(const obs_sceneitem_t *item, bool flip_horizontal, bool flip_vertical, struct matrix4 *transform,
			     struct obs_sceneitem_crop *bounds_crop)
{
	uint32_t width = obs_source_get_width(obs_sceneitem_get_source(item));
	uint32_t height = obs_source_get_height(obs_sceneitem_get_source(item));
	uint32_t cx = width;
	uint32_t cy = height;
	struct vec2 origin;
	struct vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	struct vec2 scale2;
	obs_sceneitem_get_scale(item, &scale2);

	//vec2_zero(&base_origin);
	vec2_zero(&origin);

	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	uint32_t crop_cx = crop.left + crop.right + bounds_crop->left + bounds_crop->right;
	cx = (crop_cx > width) ? 2 : (width - crop_cx);

	uint32_t crop_cy = crop.top + crop.bottom + bounds_crop->top + bounds_crop->bottom;
	cy = (crop_cy > height) ? 2 : (height - crop_cy);

	enum obs_bounds_type bt = obs_sceneitem_get_bounds_type(item);
	float width_diff, height_diff = 0.0f;

	if (bt != OBS_BOUNDS_NONE) {
		calculate_bounds_item(item, &origin, &scale, &cx, &cy, &width_diff, &height_diff, bounds_crop, bt);

	} else {
		cx = (uint32_t)((float)cx * fabs(scale.x));
		cy = (uint32_t)((float)cy * fabs(scale.y));
	}
	uint32_t align = obs_sceneitem_get_alignment(item);

	add_alignment(&origin, align, (int)cx, (int)cy);

	struct vec2 pos;
	obs_sceneitem_get_pos(item, &pos);

	if (scale.x < 0.0f && flip_horizontal) {
		scale.x = scale.x * -1.0f;
		if (bt != OBS_BOUNDS_NONE)
			origin.x += width_diff;
		else if (align & OBS_ALIGN_RIGHT)
			origin.x -= (float)cx;
		else if (align & OBS_ALIGN_LEFT)
			origin.x += (float)cx;
	}

	if (scale.y < 0.0f && flip_vertical) {
		scale.y = scale.y * -1.0f;
		if (bt != OBS_BOUNDS_NONE)
			origin.y += height_diff;
		else if (align & OBS_ALIGN_BOTTOM)
			origin.y -= (float)cy;
		else if ((align & OBS_ALIGN_TOP) == 0)
			origin.y += (float)cy;
	}

	matrix4_identity(transform);
	matrix4_scale3f(transform, transform, scale.x, scale.y, 1.0f);
	matrix4_translate3f(transform, transform, -origin.x, -origin.y, 0.0f);
	matrix4_rotate_aa4f(transform, transform, 0.0f, 0.0f, 1.0f, RAD(obs_sceneitem_get_rot(item)));

	matrix4_translate3f(transform, transform, pos.x, pos.y, 0.0f);
}

bool render2_item(struct move_info *move, struct move_item *item)
{
	obs_sceneitem_t *scene_item = NULL;
	if (item->item_a && item->item_b) {
		if (item->move_filter_a && !item->move_filter_b) {
			scene_item = item->item_a;
		} else if (!item->move_filter_a && item->move_filter_b) {
			scene_item = item->item_b;
		} else if (move->t <= 0.5) {
			scene_item = item->item_a;
		} else {
			scene_item = item->item_b;
		}
	} else if (item->item_a) {
		scene_item = item->item_a;
	} else if (item->item_b) {
		scene_item = item->item_b;
	}
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);
	bool move_out = item->item_a == scene_item;
	if (item->item_a && item->item_b) {
		if (!item->transition) {
			if (item->move_scene) {
				item->transition = get_transition(obs_source_get_name(move->source), &move->transition_pool_move,
								  &move->transition_pool_move_index, move->cache_transitions);
				struct move_info *mt = (struct move_info *)obs_obj_get_data(item->transition);
				if (mt) {
					mt->scene_flip_horizontal = item->scene_flip_horizontal;
					mt->scene_flip_vertical = item->scene_flip_vertical;
				}
			} else {
				item->transition = get_transition(item->transition_name, &move->transition_pool_move,
								  &move->transition_pool_move_index, move->cache_transitions);
			}
			if (item->transition) {
				obs_transition_set_size(item->transition, width, height);
				obs_transition_set_alignment(item->transition, OBS_ALIGN_CENTER);
				obs_transition_set_scale_type(item->transition, item->transition_scale);
				obs_transition_set(item->transition, obs_sceneitem_get_source(item->item_a));
				obs_source_add_active_child(move->source, item->transition);
				obs_transition_start(item->transition,
						     obs_transition_fixed(item->transition) ? OBS_TRANSITION_MODE_AUTO
											    : OBS_TRANSITION_MODE_MANUAL,
						     obs_frontend_get_transition_duration(),
						     obs_sceneitem_get_source(item->item_b));
			}
		}
	} else if (item->move_scene) {
		if (item->transition_name && !item->transition) {
			item->transition = get_transition(item->transition_name, &move->transition_pool_move,
							  &move->transition_pool_move_index, move->cache_transitions);
			if (item->transition) {
				obs_transition_set_size(item->transition, width, height);
				obs_transition_set_alignment(item->transition, OBS_ALIGN_CENTER);
				obs_transition_set_scale_type(item->transition, item->transition_scale);
				obs_transition_set(item->transition, obs_sceneitem_get_source(scene_item));
				obs_source_add_active_child(move->source, item->transition);
				obs_transition_start(item->transition,
						     obs_transition_fixed(item->transition) ? OBS_TRANSITION_MODE_AUTO
											    : OBS_TRANSITION_MODE_MANUAL,
						     obs_frontend_get_transition_duration(), obs_sceneitem_get_source(scene_item));
			}
		}
	} else if (move_out && item->transition_name && !item->transition) {
		item->transition = get_transition(item->transition_name, &move->transition_pool_out,
						  &move->transition_pool_out_index, move->cache_transitions);
		if (item->transition) {
			obs_transition_set_size(item->transition, width, height);
			obs_transition_set_alignment(item->transition, OBS_ALIGN_CENTER);
			obs_transition_set_scale_type(item->transition, OBS_TRANSITION_SCALE_ASPECT);
			obs_transition_set(item->transition, source);
			obs_source_add_active_child(move->source, item->transition);
			obs_transition_start(item->transition,
					     obs_transition_fixed(item->transition) ? OBS_TRANSITION_MODE_AUTO
										    : OBS_TRANSITION_MODE_MANUAL,
					     obs_frontend_get_transition_duration(), NULL);
		}
	} else if (!move_out && item->transition_name && !item->transition) {
		item->transition = get_transition(item->transition_name, &move->transition_pool_in, &move->transition_pool_in_index,
						  move->cache_transitions);

		if (item->transition) {
			obs_transition_set_size(item->transition, width, height);
			obs_transition_set_alignment(item->transition, OBS_ALIGN_CENTER);
			obs_transition_set_scale_type(item->transition, OBS_TRANSITION_SCALE_ASPECT);
			obs_transition_set(item->transition, NULL);
			obs_source_add_active_child(move->source, item->transition);
			obs_transition_start(item->transition,
					     obs_transition_fixed(item->transition) ? OBS_TRANSITION_MODE_AUTO
										    : OBS_TRANSITION_MODE_MANUAL,
					     obs_frontend_get_transition_duration(), source);
		}
	}

	float t = 0.0f;
	if (item->start_percentage > 0 || item->end_percentage < 100) {
		if (item->start_percentage > item->end_percentage) {
			float avg_switch_point = (float)(item->start_percentage + item->end_percentage) / 200.0f;
			if (move->t > avg_switch_point) {
				t = 1.0f;
			}
		} else if (move->t * 100.0 < item->start_percentage) {
			t = 0.0f;
		} else if (move->t * 100.0 > item->end_percentage) {
			t = 1.0f;
		} else {
			int duration_percentage = item->end_percentage - item->start_percentage;
			t = move->t - (float)item->start_percentage / 100.0f;
			t = t / (float)duration_percentage * 100.0f;
			t = get_eased(t, item->easing, item->easing_function);
		}
	} else {
		t = get_eased(move->t, item->easing, item->easing_function);
	}
	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	if (item->item_a && item->item_b && item->transition &&
	    (!move->first_frame || item->scene_flip_horizontal || item->scene_flip_vertical)) {
		uint32_t width_a = obs_source_get_width(obs_sceneitem_get_source(item->item_a));
		uint32_t width_b = obs_source_get_width(obs_sceneitem_get_source(item->item_b));
		uint32_t height_a = obs_source_get_height(obs_sceneitem_get_source(item->item_a));
		uint32_t height_b = obs_source_get_height(obs_sceneitem_get_source(item->item_b));
		if (width_a != width_b)
			width = (uint32_t)roundf((1.0f - t) * width_a + t * width_b);
		if (height_a != height_b)
			height = (uint32_t)roundf((1.0f - t) * height_a + t * height_b);
		if (height_a != height_b || width_a != width_b)
			obs_transition_set_size(item->transition, width, height);
	}

	uint32_t original_width = width;
	uint32_t original_height = height;
	struct obs_sceneitem_crop crop;
	if (item->item_a && item->item_b) {
		struct obs_sceneitem_crop crop_a;
		struct obs_sceneitem_crop crop_b;
		if (item->scene_flip_horizontal || item->scene_flip_vertical) {
			struct vec2 scale_a;
			obs_sceneitem_get_scale(item->item_a, &scale_a);
			obs_sceneitem_get_crop(item->item_a, &crop);
			if (scale_a.x < 0.0f && item->scene_flip_horizontal) {
				crop_a.left = crop.right;
				crop_a.right = crop.left;
			} else {
				crop_a.left = crop.left;
				crop_a.right = crop.right;
			}
			if (scale_a.y < 0.0f && item->scene_flip_vertical) {
				crop_a.top = crop.bottom;
				crop_a.bottom = crop.top;
			} else {
				crop_a.top = crop.top;
				crop_a.bottom = crop.bottom;
			}
			struct vec2 scale_b;
			obs_sceneitem_get_scale(item->item_b, &scale_b);
			obs_sceneitem_get_crop(item->item_b, &crop);
			if (scale_b.x < 0.0f && item->scene_flip_horizontal) {
				crop_b.left = crop.right;
				crop_b.right = crop.left;
			} else {
				crop_b.left = crop.left;
				crop_b.right = crop.right;
			}
			if (scale_b.y < 0.0f && item->scene_flip_vertical) {
				crop_b.top = crop.bottom;
				crop_b.bottom = crop.top;
			} else {
				crop_b.top = crop.top;
				crop_b.bottom = crop.bottom;
			}
		} else {
			obs_sceneitem_get_crop(item->item_a, &crop_a);
			obs_sceneitem_get_crop(item->item_b, &crop_b);
		}
		crop.left = (int)roundf((float)(1.0f - ot) * (float)crop_a.left + ot * (float)crop_b.left);
		crop.top = (int)roundf((float)(1.0f - ot) * (float)crop_a.top + ot * (float)crop_b.top);
		crop.right = (int)roundf((float)(1.0f - ot) * (float)crop_a.right + ot * (float)crop_b.right);
		crop.bottom = (int)roundf((float)(1.0f - ot) * (float)crop_a.bottom + ot * (float)crop_b.bottom);
	} else if (item->move_scene) {
		obs_sceneitem_get_crop(scene_item, &crop);
		if (item->item_a) {
			crop.left = (int)roundf((float)(1.0f - ot) * (float)crop.left);
			crop.top = (int)roundf((float)(1.0f - ot) * (float)crop.top);
			crop.right = (int)roundf((float)(1.0f - ot) * (float)crop.right);
			crop.bottom = (int)roundf((float)(1.0f - ot) * (float)crop.bottom);
		} else if (item->item_b) {
			crop.left = (int)roundf((float)ot * (float)crop.left);
			crop.top = (int)roundf((float)ot * (float)crop.top);
			crop.right = (int)roundf((float)ot * (float)crop.right);
			crop.bottom = (int)roundf((float)ot * (float)crop.bottom);
		}
	} else {
		obs_sceneitem_get_crop(scene_item, &crop);
	}
	uint32_t crop_cx = crop.left + crop.right;
	int32_t cx = (crop_cx > width) ? 2 : (width - crop_cx);
	uint32_t crop_cy = crop.top + crop.bottom;
	int32_t cy = (crop_cy > height) ? 2 : (height - crop_cy);
	struct vec2 scale;
	struct vec2 original_scale;
	obs_sceneitem_get_scale(scene_item, &original_scale);
	if (item->item_a && item->item_b) {
		struct vec2 scale_a;
		obs_sceneitem_get_scale(item->item_a, &scale_a);
		struct vec2 scale_b;
		obs_sceneitem_get_scale(item->item_b, &scale_b);
		vec2_set(&scale, (1.0f - t) * scale_a.x + t * scale_b.x, (1.0f - t) * scale_a.y + t * scale_b.y);
	} else {
		if (obs_sceneitem_get_bounds_type(scene_item) != OBS_BOUNDS_NONE) {
			obs_sceneitem_get_scale(scene_item, &scale);
		} else {
			obs_sceneitem_get_scale(scene_item, &scale);
			if (item->move_scene) {
				if (item->item_a) {
					vec2_set(&scale, (1.0f - t) * scale.x + t, (1.0f - t) * scale.y + t);
				} else if (item->item_b) {
					vec2_set(&scale, (1.0f - t) + t * scale.x, (1.0f - t) + t * scale.y);
				}
			} else if (!move_out && item->zoom != 1.0f) {
				vec2_set(&scale, (1.0f - t) * scale.x * item->zoom + t * scale.x,
					 (1.0f - t) * scale.y * item->zoom + t * scale.y);
			} else if (move_out && item->zoom != 1.0f) {
				vec2_set(&scale, (1.0f - t) * scale.x + t * scale.x * item->zoom,
					 (1.0f - t) * scale.y + t * scale.y * item->zoom);
			}
		}
	}
	width = cx;
	height = cy;
	int32_t original_cx = cx;
	int32_t original_cy = cy;

	struct vec2 base_origin;
	struct vec2 origin;
	struct vec2 origin2;
	vec2_zero(&base_origin);
	vec2_zero(&origin);
	vec2_zero(&origin2);

	uint32_t canvas_width = obs_source_get_width(move->source);
	if (!canvas_width && move->scene_source_a)
		canvas_width = obs_source_get_width(move->scene_source_a);
	if (!canvas_width && move->scene_source_b)
		canvas_width = obs_source_get_width(move->scene_source_b);
	if (!canvas_width)
		canvas_width = obs_source_get_width(source);
	uint32_t canvas_height = obs_source_get_height(move->source);
	if (!canvas_height && move->scene_source_a)
		canvas_height = obs_source_get_height(move->scene_source_a);
	if (!canvas_height && move->scene_source_b)
		canvas_height = obs_source_get_height(move->scene_source_b);
	if (!canvas_height)
		canvas_height = obs_source_get_height(source);

	enum obs_bounds_type bt_a = obs_sceneitem_get_bounds_type(item->item_a);
	enum obs_bounds_type bt_b = obs_sceneitem_get_bounds_type(item->item_b);

	if (obs_sceneitem_get_bounds_type(scene_item) != OBS_BOUNDS_NONE) {
		struct vec2 bounds;
		if (item->item_a && item->item_b) {
			struct vec2 bounds_a;
			obs_sceneitem_get_bounds(item->item_a, &bounds_a);
			struct vec2 bounds_b;
			obs_sceneitem_get_bounds(item->item_b, &bounds_b);
			vec2_set(&bounds, (1.0f - t) * bounds_a.x + t * bounds_b.x, (1.0f - t) * bounds_a.y + t * bounds_b.y);
		} else if (item->move_scene) {
			obs_sceneitem_get_bounds(scene_item, &bounds);
			if (item->item_a) {
				vec2_set(&bounds, (1.0f - t) * bounds.x + t * canvas_width,
					 (1.0f - t) * bounds.y + t * canvas_height);
			} else if (item->item_b) {
				vec2_set(&bounds, (1.0f - t) * canvas_width + t * bounds.x,
					 (1.0f - t) * canvas_height + t * bounds.y);
			}
		} else {
			obs_sceneitem_get_bounds(scene_item, &bounds);
			if (!move_out && item->zoom != 1.0f) {
				vec2_set(&bounds, (1.0f - t) * bounds.x * item->zoom + t * bounds.x,
					 (1.0f - t) * bounds.y * item->zoom + t * bounds.y);
			} else if (move_out && item->zoom != 1.0f) {
				vec2_set(&bounds, (1.0f - t) * bounds.x + t * bounds.x * item->zoom,
					 (1.0f - t) * bounds.y + t * bounds.y * item->zoom);
			}
		}
		if (item->item_a && item->item_b &&
		    (obs_sceneitem_get_bounds_alignment(item->item_a) != obs_sceneitem_get_bounds_alignment(item->item_b) ||
		     bt_a != bt_b)) {
			calculate_move_bounds_data(item->item_a, item->item_b, t, &origin, &scale, &cx, &cy, &bounds);

		} else {
			calculate_bounds_data(scene_item, &origin, &scale, &cx, &cy, &bounds);
		}
		struct vec2 original_bounds;
		obs_sceneitem_get_bounds(scene_item, &original_bounds);
		calculate_bounds_data(scene_item, &origin2, &original_scale, &original_cx, &original_cy, &original_bounds);
	} else {
		original_cx = (int32_t)roundf((float)cx * original_scale.x);
		original_cy = (int32_t)roundf((float)cy * original_scale.y);
		cx = (int32_t)roundf((float)cx * scale.x);
		cy = (int32_t)roundf((float)cy * scale.y);
	}
	if (item->item_a && item->item_b &&
	    obs_sceneitem_get_alignment(item->item_a) != obs_sceneitem_get_alignment(item->item_b)) {
		add_move_alignment(&origin, obs_sceneitem_get_alignment(item->item_a), obs_sceneitem_get_alignment(item->item_b), t,
				   cx, cy);
	} else {
		add_alignment(&origin, obs_sceneitem_get_alignment(scene_item), cx, cy);
	}

	struct matrix4 draw_transform;
	matrix4_identity(&draw_transform);
	matrix4_scale3f(&draw_transform, &draw_transform, scale.x, scale.y, 1.0f);
	matrix4_translate3f(&draw_transform, &draw_transform, -origin.x, -origin.y, 0.0f);
	float rot;
	float rd = 0.0f;
	if (item->item_a && item->item_b) {
		float rot_a = obs_sceneitem_get_rot(item->item_a);
		float rot_b = obs_sceneitem_get_rot(item->item_b);
		rd = rot_diff(rot_a, rot_b);
		rot = rot_a + t * rd;
	} else if (item->move_scene) {
		rd = rot_diff(0.0f, obs_sceneitem_get_rot(scene_item));
		rot = rd;
		if (item->item_a) {
			rot *= (1.0f - t);
		} else if (item->item_b) {
			rot *= t;
		}
	} else {
		rot = obs_sceneitem_get_rot(scene_item);
	}
	matrix4_rotate_aa4f(&draw_transform, &draw_transform, 0.0f, 0.0f, 1.0f, RAD(rot));

	struct vec2 pos_a;
	if (item->item_a) {
		obs_sceneitem_get_pos(item->item_a, &pos_a);
	} else if (item->move_scene) {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		vec2_set(&pos_a, 0, 0);
		if (alignment & OBS_ALIGN_RIGHT) {
			pos_a.x += canvas_width;
		} else if (alignment & OBS_ALIGN_LEFT) {

		} else {
			pos_a.x += canvas_width >> 1;
		}
		if (alignment & OBS_ALIGN_BOTTOM) {
			pos_a.y += canvas_height;
		} else if (alignment & OBS_ALIGN_TOP) {

		} else {
			pos_a.x += canvas_height >> 1;
		}
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (item->position & POS_CENTER) {
			vec2_set(&pos_a, (float)(canvas_width >> 1), (float)(canvas_height >> 1));
			if (item->zoom == 1.0f)
				pos_add_center(&pos_a, alignment, cx, cy);
		} else if (item->position & POS_EDGE || item->position & POS_SWIPE) {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			calc_edge_position(&pos_a, item->position, canvas_width, canvas_height, alignment, original_cx, original_cy,
					   item->zoom);

		} else {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			if (item->zoom != 1.0f)
				pos_subtract_center(&pos_a, alignment, original_cx, original_cy);
		}
	}
	struct vec2 pos_b;
	if (item->item_b) {
		obs_sceneitem_get_pos(item->item_b, &pos_b);
	} else if (item->move_scene) {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		vec2_set(&pos_b, 0, 0);
		if (alignment & OBS_ALIGN_RIGHT) {
			pos_b.x += canvas_width;
		} else if (alignment & OBS_ALIGN_LEFT) {

		} else {
			pos_b.x += canvas_width >> 1;
		}
		if (alignment & OBS_ALIGN_BOTTOM) {
			pos_b.y += canvas_height;
		} else if (alignment & OBS_ALIGN_TOP) {

		} else {
			pos_b.x += canvas_height >> 1;
		}
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (item->position & POS_CENTER) {
			vec2_set(&pos_b, (float)(canvas_width >> 1), (float)(canvas_height >> 1));
			if (item->zoom == 1.0f)
				pos_add_center(&pos_b, alignment, cx, cy);
		} else if (item->position & POS_EDGE || item->position & POS_SWIPE) {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			calc_edge_position(&pos_b, item->position, canvas_width, canvas_height, alignment, original_cx, original_cy,
					   item->zoom);

		} else {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			if (item->zoom != 1.0f)
				pos_subtract_center(&pos_b, alignment, original_cx, original_cy);
		}
	}
	struct vec2 pos;
	if (item->curve != 0.0f) {
		float diff_x = fabsf(pos_a.x - pos_b.x);
		float diff_y = fabsf(pos_a.y - pos_b.y);
		struct vec2 control_pos;
		vec2_set(&control_pos, 0.5f * pos_a.x + 0.5f * pos_b.x, 0.5f * pos_a.y + 0.5f * pos_b.y);
		if (control_pos.x >= (canvas_width >> 1)) {
			control_pos.x += diff_y * item->curve;
		} else {
			control_pos.x -= diff_y * item->curve;
		}
		if (control_pos.y >= (canvas_height >> 1)) {
			control_pos.y += diff_x * item->curve;
		} else {
			control_pos.y -= diff_x * item->curve;
		}
		vec2_bezier(&pos, &pos_a, &control_pos, &pos_b, t);
	} else {
		vec2_set(&pos, (1.0f - t) * pos_a.x + t * pos_b.x, (1.0f - t) * pos_a.y + t * pos_b.y);
	}

	matrix4_translate3f(&draw_transform, &draw_transform, pos.x, pos.y, 0.0f);

	struct vec2 output_scale = scale;

	if (item->item_render && !item_texture_enabled(item->item_a) && !item_texture_enabled(item->item_b)) {
		gs_texrender_destroy(item->item_render);
		item->item_render = NULL;
	} else if (!item->item_render &&
		   ((item->item_a && item_texture_enabled(item->item_a)) || (item->item_b && item_texture_enabled(item->item_b)))) {
		item->item_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else if (item->item_render) {
		gs_texrender_reset(item->item_render);
	}
	if (!move->point_sampler) {
		struct gs_sampler_info point_sampler_info = {0};
		point_sampler_info.max_anisotropy = 1;
		move->point_sampler = gs_samplerstate_create(&point_sampler_info);
	}
	if (item->item_a) {
		move_get_draw_transform(item->item_a, item->scene_flip_horizontal, item->scene_flip_vertical, &item->transform_a,
					&item->bounds_crop_a);
	}
	if (item->item_b) {
		move_get_draw_transform(item->item_b, item->scene_flip_horizontal, item->scene_flip_vertical, &item->transform_b,
					&item->bounds_crop_b);
	}
	struct obs_sceneitem_crop bounds_crop = {0};
	if ((item->item_a && item->item_b) || item->move_scene) {
		bounds_crop.left =
			(int)roundf((float)(1.0f - ot) * (float)item->bounds_crop_a.left + ot * (float)item->bounds_crop_b.left);
		bounds_crop.top =
			(int)roundf((float)(1.0f - ot) * (float)item->bounds_crop_a.top + ot * (float)item->bounds_crop_b.top);
		bounds_crop.right =
			(int)roundf((float)(1.0f - ot) * (float)item->bounds_crop_a.right + ot * (float)item->bounds_crop_b.right);
		bounds_crop.bottom = (int)roundf((float)(1.0f - ot) * (float)item->bounds_crop_a.bottom +
						 ot * (float)item->bounds_crop_b.bottom);
	} else if (item->item_a) {
		bounds_crop.left = item->bounds_crop_a.left;
		bounds_crop.top = item->bounds_crop_a.top;
		bounds_crop.right = item->bounds_crop_a.right;
		bounds_crop.bottom = item->bounds_crop_a.bottom;
	} else if (item->item_b) {
		bounds_crop.left = item->bounds_crop_b.left;
		bounds_crop.top = item->bounds_crop_b.top;
		bounds_crop.right = item->bounds_crop_b.right;
		bounds_crop.bottom = item->bounds_crop_b.bottom;
	}

	if (item->item_render) {
		if (width && height &&
		    gs_texrender_begin(item->item_render, width - (bounds_crop.left + bounds_crop.right),
				       height - (bounds_crop.top + bounds_crop.bottom))) {
			float cx_scale = (float)original_width / (float)(width - (bounds_crop.left + bounds_crop.right));
			float cy_scale = (float)original_height / (float)(height - (bounds_crop.top + bounds_crop.bottom));
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)original_width, 0.0f, (float)original_height, -100.0f, 100.0f);

			gs_matrix_scale3f(cx_scale, cy_scale, 1.0f);
			gs_matrix_translate3f(-(float)(crop.left + bounds_crop.left), -(float)(crop.top + bounds_crop.top), 0.0f);

			if (item->transition) {
				obs_transition_set_manual_time(item->transition, ot);
				if (!move->first_frame || item->scene_flip_horizontal || item->scene_flip_vertical) {
					obs_source_video_render(item->transition);
				} else if (item->item_a) {
					obs_source_video_render(obs_sceneitem_get_source(item->item_a));
				} else if (item->move_scene) {
					obs_source_video_render(source);
				}
			} else {
				obs_source_video_render(source);
			}

			gs_texrender_end(item->item_render);
		}
	}
	if (item->item_a && item->item_b &&
	    (fabs((double)rd) <= 90.0 || item->scene_flip_horizontal || item->scene_flip_vertical ||
	     (bt_a == OBS_BOUNDS_NONE && bt_b != OBS_BOUNDS_NONE) || (bt_a != OBS_BOUNDS_NONE && bt_b == OBS_BOUNDS_NONE))) {
		draw_transform.x.x = (1.0f - t) * item->transform_a.x.x + t * item->transform_b.x.x;
		draw_transform.x.y = (1.0f - t) * item->transform_a.x.y + t * item->transform_b.x.y;
		draw_transform.x.z = (1.0f - t) * item->transform_a.x.z + t * item->transform_b.x.z;
		draw_transform.x.w = (1.0f - t) * item->transform_a.x.w + t * item->transform_b.x.w;
		draw_transform.y.x = (1.0f - t) * item->transform_a.y.x + t * item->transform_b.y.x;
		draw_transform.y.y = (1.0f - t) * item->transform_a.y.y + t * item->transform_b.y.y;
		draw_transform.y.z = (1.0f - t) * item->transform_a.y.z + t * item->transform_b.y.z;
		draw_transform.y.w = (1.0f - t) * item->transform_a.y.w + t * item->transform_b.y.w;
		draw_transform.z.x = (1.0f - t) * item->transform_a.z.x + t * item->transform_b.z.x;
		draw_transform.z.y = (1.0f - t) * item->transform_a.z.y + t * item->transform_b.z.y;
		draw_transform.z.z = (1.0f - t) * item->transform_a.z.z + t * item->transform_b.z.z;
		draw_transform.z.w = (1.0f - t) * item->transform_a.z.w + t * item->transform_b.z.w;
		draw_transform.t.x = (1.0f - t) * item->transform_a.t.x + t * item->transform_b.t.x;
		draw_transform.t.y = (1.0f - t) * item->transform_a.t.y + t * item->transform_b.t.y;
		draw_transform.t.z = (1.0f - t) * item->transform_a.t.z + t * item->transform_b.t.z;
		draw_transform.t.w = (1.0f - t) * item->transform_a.t.w + t * item->transform_b.t.w;
	}

	gs_matrix_push();
	gs_matrix_mul(&draw_transform);
	const bool previous = gs_set_linear_srgb(obs_sceneitem_get_blending_method(scene_item) != OBS_BLEND_METHOD_SRGB_OFF);
	if (item->item_render) {
		//render_item_texture(item);
		gs_texture_t *tex = gs_texrender_get_texture(item->item_render);
		if (!tex) {
			gs_set_linear_srgb(previous);
			gs_matrix_pop();
			return true;
		}

		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

		enum obs_scale_type type = obs_sceneitem_get_scale_filter(scene_item);
		cx = gs_texture_get_width(tex);
		cy = gs_texture_get_height(tex);
		const char *tech = "Draw";
		if (type != OBS_SCALE_DISABLE) {
			if (type == OBS_SCALE_POINT) {
				gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
				gs_effect_set_next_sampler(image, move->point_sampler);
			} else if (!close_float(output_scale.x, 1.0f, EPSILON) || !close_float(output_scale.y, 1.0f, EPSILON)) {
				gs_eparam_t *scale_param;
				gs_eparam_t *scale_i_param;

				if (output_scale.x < 0.5f || output_scale.y < 0.5f) {
					effect = obs_get_base_effect(OBS_EFFECT_BILINEAR_LOWRES);
				} else if (type == OBS_SCALE_BICUBIC) {
					effect = obs_get_base_effect(OBS_EFFECT_BICUBIC);
				} else if (type == OBS_SCALE_LANCZOS) {
					effect = obs_get_base_effect(OBS_EFFECT_LANCZOS);
				} else if (type == OBS_SCALE_AREA) {
					effect = obs_get_base_effect(OBS_EFFECT_AREA);
					if ((output_scale.x >= 1.0f) && (output_scale.y >= 1.0f))
						tech = "DrawUpscale";
				}

				scale_param = gs_effect_get_param_by_name(effect, "base_dimension");
				if (scale_param) {
					struct vec2 base_res;
					base_res.x = (float)cx;
					base_res.y = (float)cy;
					gs_effect_set_vec2(scale_param, &base_res);
				}

				scale_i_param = gs_effect_get_param_by_name(effect, "base_dimension_i");
				if (scale_i_param) {
					struct vec2 base_res_i;
					base_res_i.x = 1.0f / (float)cx;
					base_res_i.y = 1.0f / (float)cy;

					gs_effect_set_vec2(scale_i_param, &base_res_i);
				}
			}
		}

		gs_blend_state_push();

		enum obs_blending_type blend_type = obs_sceneitem_get_blending_mode(scene_item);
		gs_blend_function_separate(obs_blend_mode_params[blend_type].src_color, obs_blend_mode_params[blend_type].dst_color,
					   obs_blend_mode_params[blend_type].src_alpha,
					   obs_blend_mode_params[blend_type].dst_alpha);
		gs_blend_op(obs_blend_mode_params[blend_type].op);

		while (gs_effect_loop(effect, tech))
			obs_source_draw(tex, 0, 0, 0, 0, 0);

		gs_blend_state_pop();
	} else {
		if (item->transition) {
			obs_transition_set_manual_time(item->transition, ot);
			if (!move->first_frame || item->scene_flip_horizontal || item->scene_flip_vertical) {
				obs_source_video_render(item->transition);
			} else if (item->item_a) {
				obs_source_video_render(obs_sceneitem_get_source(item->item_a));
			} else if (item->move_scene) {
				obs_source_video_render(source);
			}
		} else {
			obs_source_video_render(source);
		}
	}
	gs_set_linear_srgb(previous);
	gs_matrix_pop();
	return true;
}

void get_override_filter(obs_source_t *source, obs_source_t *filter, void *param)
{
	UNUSED_PARAMETER(source);
	if (!obs_source_enabled(filter))
		return;
	if (strcmp(obs_source_get_unversioned_id(filter), "move_transition_override_filter") != 0)
		return;
	obs_source_t *target = *(obs_source_t **)param;
	if (!target) {
		*(obs_source_t **)param = filter;
		return;
	}

	if (obs_source_get_type(target) == OBS_SOURCE_TYPE_FILTER)
		return;
	obs_data_t *settings = obs_source_get_settings(filter);
	if (!settings)
		return;
	const char *sn = obs_data_get_string(settings, S_SOURCE);
	if (sn && strlen(sn)) {
		if (strcmp(obs_source_get_name(target), sn) == 0) {
			*(obs_source_t **)param = filter;
		}
	}
	obs_data_release(settings);
}

obs_data_t *get_override_filter_settings(obs_sceneitem_t *item)
{
	if (!item)
		return NULL;
	obs_source_t *filter = obs_sceneitem_get_source(item);
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	if (scene) {
		obs_source_t *scene_source = obs_scene_get_source(scene);
		obs_source_enum_filters(scene_source, get_override_filter, &filter);
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return NULL;

	if (filter && filter != source)
		return obs_source_get_settings(filter);

	filter = NULL;
	obs_source_enum_filters(source, get_override_filter, &filter);
	if (filter && filter != source)
		return obs_source_get_settings(filter);
	return NULL;
}

bool same_transform_type(struct obs_transform_info *info_a, struct obs_transform_info *info_b)
{
	if (!info_a || !info_b)
		return false;

	return info_a->alignment == info_b->alignment && info_a->bounds_type == info_b->bounds_type &&
	       info_a->bounds_alignment == info_b->bounds_alignment;
}

struct move_item *match_item_by_override(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	const char *name_b = obs_source_get_name(source);
	obs_data_t *override_filter_b = get_override_filter_settings(scene_item);
	const char *override_name_b = override_filter_b ? obs_data_get_string(override_filter_b, S_MATCH_SOURCE) : NULL;
	obs_data_release(override_filter_b);
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b)
			continue;
		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		const char *name_a = obs_source_get_name(check_source);
		if (name_a && strlen(name_a) && override_name_b && strcmp(name_a, override_name_b) == 0) {
			item = check_item;
			*found_pos = i;
			break;
		}
		if (!name_b || !strlen(name_b))
			continue;
		obs_data_t *override_filter_a = get_override_filter_settings(check_item->item_a);
		if (override_filter_a) {
			const char *override_name_a = obs_data_get_string(override_filter_a, S_MATCH_SOURCE);
			obs_data_release(override_filter_a);
			if (strcmp(override_name_a, name_b) == 0) {
				item = check_item;
				*found_pos = i;
				break;
			}
		}
	}
	return item;
}

bool is_number_match(const char c)
{
	if (c >= '0' && c <= '9')
		return true;
	if (c == '(' || c == ')' || c == ' ' || c == '.' || c == ',')
		return true;
	return false;
}

struct move_item *match_item_by_name(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	const char *name_b = obs_source_get_name(source);
	if (!name_b || !strlen(name_b))
		return item;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		if (check_source == source) {
			item = check_item;
			*found_pos = i;
			break;
		}
		const char *name_a = obs_source_get_name(check_source);
		if (!name_a)
			continue;
		if (strcmp(name_a, name_b) == 0) {
			item = check_item;
			*found_pos = i;
			break;
		}
	}
	return item;
}

struct move_item *match_item_by_type_and_settings(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{

	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	if (obs_source_is_scene(source) || obs_source_is_group(source))
		return item;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		if (obs_source_get_type(check_source) != obs_source_get_type(source))
			continue;

		if (strcmp(obs_source_get_unversioned_id(check_source), obs_source_get_unversioned_id(source)) != 0)
			continue;

		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_t *check_settings = obs_source_get_settings(check_source);
		if (settings && check_settings && strcmp(obs_data_get_json(settings), obs_data_get_json(check_settings)) == 0) {
			item = check_item;
			*found_pos = i;
			obs_data_release(check_settings);
			obs_data_release(settings);
			break;
		}
		obs_data_release(check_settings);
		obs_data_release(settings);
	}
	return item;
}

struct move_item *match_item_clone(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	const char *name_b = obs_source_get_name(source);
	if (!name_b || !strlen(name_b))
		return item;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b)
			continue;
		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		const char *name_a = obs_source_get_name(check_source);
		if (!name_a || !strlen(name_a))
			continue;
		const char *id_a = obs_source_get_unversioned_id(check_source);
		const char *id_b = obs_source_get_unversioned_id(source);
		const char *clone_a = NULL;
		const char *clone_b = NULL;
		if (strcmp(id_a, "source-clone") == 0) {
			obs_data_t *s = obs_source_get_settings(check_source);
			clone_a = obs_data_get_string(s, "clone");
			obs_data_release(s);
		} else if (strcmp(id_a, "streamfx-source-mirror") == 0) {
			obs_data_t *s = obs_source_get_settings(check_source);
			clone_a = obs_data_get_string(s, "Source.Mirror.Source");
			obs_data_release(s);
		}

		if (strcmp(id_b, "source-clone") == 0) {
			obs_data_t *s = obs_source_get_settings(source);
			clone_b = obs_data_get_string(s, "clone");
			obs_data_release(s);
		} else if (strcmp(id_b, "streamfx-source-mirror") == 0) {
			obs_data_t *s = obs_source_get_settings(check_source);
			clone_b = obs_data_get_string(s, "Source.Mirror.Source");
			obs_data_release(s);
		}
		if ((clone_a && clone_b && strcmp(clone_a, clone_b) == 0) || (clone_a && strcmp(clone_a, name_b) == 0) ||
		    (clone_b && strcmp(clone_b, name_a) == 0)) {
			item = check_item;
			*found_pos = i;
			break;
		}
	}
	return item;
}

struct move_item *match_item_name_part(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;

	if (!move->last_word_match && !move->number_match && !move->part_match)
		return item;

	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	const char *name_b = obs_source_get_name(source);
	if (!name_b || !strlen(name_b))
		return item;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		const char *name_a = obs_source_get_name(check_source);
		if (!name_a)
			continue;

		size_t len_a = strlen(name_a);
		size_t len_b = strlen(name_b);
		if (!len_a || !len_b)
			continue;

		if (len_a > len_b) {
			if (move->last_word_match) {
				char *last_space = strrchr(name_b, ' ');
				if (last_space && last_space > name_b) {
					len_b = last_space - name_b;
				}
			}
			while (len_b > 0 && move->number_match && is_number_match(name_b[len_b - 1]))
				len_b--;
			if (len_b > 0 && move->part_match) {
				for (size_t pos = 0; pos <= len_a - len_b; pos++) {
					if (memcmp(name_a + pos, name_b, len_b) == 0) {
						item = check_item;
						*found_pos = i;
						break;
					}
				}
				if (item)
					break;
			} else if (len_b > 0 && memcmp(name_a, name_b, len_b) == 0) {
				item = check_item;
				*found_pos = i;
				break;
			}

		} else {
			if (move->last_word_match) {
				char *last_space = strrchr(name_a, ' ');
				if (last_space && last_space > name_a) {
					len_a = last_space - name_a;
				}
			}
			while (len_a > 0 && move->number_match && is_number_match(name_a[len_a - 1]))
				len_a--;
			if (len_a > 0 && move->part_match) {
				for (size_t pos = 0; pos <= len_b - len_a; pos++) {
					if (memcmp(name_a, name_b + pos, len_a) == 0) {
						item = check_item;
						*found_pos = i;
						break;
					}
				}
				if (item)
					break;
			} else if (len_a > 0 && memcmp(name_a, name_b, len_a) == 0) {
				item = check_item;
				*found_pos = i;
				break;
			}
		}
	}
	return item;
}

struct match_item_clone_match {
	const char *name;
	obs_sceneitem_t *matched;
};

bool match_item_clone_match(obs_scene_t *scene, obs_sceneitem_t *sceneitem, void *p)
{
	UNUSED_PARAMETER(scene);
	if (!obs_sceneitem_visible(sceneitem))
		return true;
	struct match_item_clone_match *mi = p;
	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	const char *id = obs_source_get_unversioned_id(source);
	bool match = false;
	if (strcmp(id, "source-clone") == 0) {
		obs_data_t *s = obs_source_get_settings(source);
		match = strcmp(obs_data_get_string(s, "clone"), mi->name) == 0;
		obs_data_release(s);
	} else if (strcmp(id, "streamfx-source-mirror") == 0) {
		obs_data_t *s = obs_source_get_settings(source);
		match = strcmp(obs_data_get_string(s, "Source.Mirror.Source"), mi->name) == 0;
		obs_data_release(s);
	}
	if (match) {
		mi->matched = sceneitem;
		return false;
	}
	return true;
}

obs_sceneitem_t *scene_find_source(obs_scene_t *scene, const char *name)
{
	obs_sceneitem_t *item = obs_scene_find_source(scene, name);
	if (!item) {
		struct match_item_clone_match mi;
		mi.name = name;
		mi.matched = NULL;
		obs_scene_enum_items(scene, match_item_clone_match, &mi);
		item = mi.matched;
	}
	return item;
}

struct match_item_nested_match {
	obs_source_t *check_source;
	bool matched;
	bool scene_flip_horizontal;
	bool scene_flip_vertical;
};

bool match_item_nested_all_match(obs_scene_t *obs_scene, obs_sceneitem_t *sceneitem, void *p)
{
	UNUSED_PARAMETER(obs_scene);
	if (!obs_sceneitem_visible(sceneitem))
		return true;
	struct match_item_nested_match *mi = p;
	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (!source)
		return true;
	obs_scene_t *scene = obs_scene_from_source(mi->check_source);
	if (!scene)
		scene = obs_group_from_source(mi->check_source);
	const char *source_name = obs_source_get_name(source);
	obs_sceneitem_t *item = scene_find_source(scene, source_name);
	if (!item) {
		source_name = NULL;
		const char *id = obs_source_get_unversioned_id(source);
		if (strcmp(id, "source-clone") == 0) {
			obs_data_t *s = obs_source_get_settings(source);
			source_name = obs_data_get_string(s, "clone");
			obs_data_release(s);
		} else if (strcmp(id, "streamfx-source-mirror") == 0) {
			obs_data_t *s = obs_source_get_settings(source);
			source_name = obs_data_get_string(s, "Source.Mirror.Source");
			obs_data_release(s);
		}
		if (source_name)
			item = scene_find_source(scene, source_name);
	}
	if (!item) {
		mi->matched = false;
		return false;
	}
	return true;
}

struct move_item *match_item_scene_same(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	if (!move->nested_scenes)
		return item;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	if (!obs_source_is_scene(source) && !obs_source_is_group(source))
		return item;
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b || check_item->move_scene)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;

		if (!obs_source_is_scene(check_source) && !obs_source_is_group(check_source))
			continue;

		obs_scene_t *check_scene = obs_scene_from_source(check_source);
		if (!check_scene)
			check_scene = obs_group_from_source(check_source);

		struct match_item_nested_match mi;
		mi.check_source = check_source;
		mi.matched = true;
		obs_scene_enum_items(scene, match_item_nested_all_match, &mi);
		mi.check_source = source;
		obs_scene_enum_items(check_scene, match_item_nested_all_match, &mi);
		if (mi.matched) {
			item = check_item;
			item->move_scene = true;
			*found_pos = i;
			break;
		}
	}
	return item;
}

bool match_item_nested_match(obs_scene_t *obs_scene, obs_sceneitem_t *sceneitem, void *p)
{
	UNUSED_PARAMETER(obs_scene);
	if (!obs_sceneitem_visible(sceneitem))
		return true;
	struct match_item_nested_match *mi = p;
	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (!source)
		return true;
	if (source == mi->check_source) {
		mi->matched = true;
		struct vec2 scale;
		obs_sceneitem_get_scale(sceneitem, &scale);
		mi->scene_flip_horizontal = mi->scene_flip_horizontal && scale.x < 0.0f;
		mi->scene_flip_vertical = mi->scene_flip_vertical && scale.y < 0.0f;
		return false;
	}
	const char *name_a = obs_source_get_name(mi->check_source);
	const char *name_b = obs_source_get_name(source);
	if (name_a && name_b && strlen(name_a) && strlen(name_b) && strcmp(name_a, name_b) == 0) {
		mi->matched = true;
		struct vec2 scale;
		obs_sceneitem_get_scale(sceneitem, &scale);
		mi->scene_flip_horizontal = mi->scene_flip_horizontal && scale.x < 0.0f;
		mi->scene_flip_vertical = mi->scene_flip_vertical && scale.y < 0.0f;
		return false;
	}
	return true;
}

struct move_item *match_item_nested(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	if (!move->nested_scenes)
		return item;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b || check_item->move_scene)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		if (obs_source_is_scene(source)) {
			obs_scene_t *scene = obs_scene_from_source(source);
			struct match_item_nested_match mi;
			mi.check_source = check_source;
			mi.matched = false;
			struct vec2 scale;
			obs_sceneitem_get_scale(check_item->item_a, &scale);
			mi.scene_flip_horizontal = scale.x < 0.0f;
			mi.scene_flip_vertical = scale.y < 0.0f;
			obs_scene_enum_items(scene, match_item_nested_match, &mi);
			if (mi.matched) {
				item = check_item;
				item->move_scene = true;
				item->scene_flip_horizontal = mi.scene_flip_horizontal;
				item->scene_flip_vertical = mi.scene_flip_vertical;
				*found_pos = i;
				break;
			}
		} else if (obs_source_is_group(source)) {
			obs_scene_t *scene = obs_group_from_source(source);
			struct match_item_nested_match mi;
			mi.check_source = check_source;
			mi.matched = false;
			struct vec2 scale;
			obs_sceneitem_get_scale(check_item->item_a, &scale);
			mi.scene_flip_horizontal = scale.x < 0.0f;
			mi.scene_flip_vertical = scale.y < 0.0f;
			obs_scene_enum_items(scene, match_item_nested_match, &mi);
			if (mi.matched) {
				item = check_item;
				item->move_scene = true;
				item->scene_flip_horizontal = mi.scene_flip_horizontal;
				item->scene_flip_vertical = mi.scene_flip_vertical;
				*found_pos = i;
				break;
			}
		}
		if (obs_source_is_scene(check_source)) {
			obs_scene_t *scene = obs_scene_from_source(check_source);
			struct match_item_nested_match mi;
			mi.check_source = source;
			mi.matched = false;
			struct vec2 scale;
			obs_sceneitem_get_scale(scene_item, &scale);
			mi.scene_flip_horizontal = scale.x < 0.0f;
			mi.scene_flip_vertical = scale.y < 0.0f;
			obs_scene_enum_items(scene, match_item_nested_match, &mi);
			if (mi.matched) {
				item = check_item;
				item->move_scene = true;
				item->scene_flip_horizontal = mi.scene_flip_horizontal;
				item->scene_flip_vertical = mi.scene_flip_vertical;
				*found_pos = i;
				break;
			}
		} else if (obs_source_is_group(check_source)) {
			obs_scene_t *scene = obs_group_from_source(check_source);
			struct match_item_nested_match mi;
			mi.check_source = source;
			mi.matched = false;
			struct vec2 scale;
			obs_sceneitem_get_scale(scene_item, &scale);
			mi.scene_flip_horizontal = scale.x < 0.0f;
			mi.scene_flip_vertical = scale.y < 0.0f;
			obs_scene_enum_items(scene, match_item_nested_match, &mi);
			if (mi.matched) {
				item = check_item;
				item->move_scene = true;
				item->scene_flip_horizontal = mi.scene_flip_horizontal;
				item->scene_flip_vertical = mi.scene_flip_vertical;
				*found_pos = i;
				break;
			}
		}
	}
	return item;
}

bool match_item_nested_any_match(obs_scene_t *obs_scene, obs_sceneitem_t *sceneitem, void *p)
{
	UNUSED_PARAMETER(obs_scene);
	if (!obs_sceneitem_visible(sceneitem))
		return true;
	struct match_item_nested_match *mi = p;
	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (!source)
		return true;
	obs_scene_t *scene = obs_scene_from_source(mi->check_source);
	if (!scene)
		scene = obs_group_from_source(mi->check_source);
	const char *source_name = obs_source_get_name(source);
	obs_sceneitem_t *item = scene_find_source(scene, source_name);
	if (!item) {
		source_name = NULL;
		const char *id = obs_source_get_unversioned_id(source);
		if (strcmp(id, "source-clone") == 0) {
			obs_data_t *s = obs_source_get_settings(source);
			source_name = obs_data_get_string(s, "clone");
			obs_data_release(s);
		} else if (strcmp(id, "streamfx-source-mirror") == 0) {
			obs_data_t *s = obs_source_get_settings(source);
			source_name = obs_data_get_string(s, "Source.Mirror.Source");
			obs_data_release(s);
		}
		if (source_name)
			item = scene_find_source(scene, source_name);
	}
	if (item && obs_sceneitem_visible(item)) {
		mi->matched = true;
		return false;
	}
	return true;
}

struct move_item *match_item_scene_match(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos)
{
	struct move_item *item = NULL;
	if (!move->nested_scenes)
		return item;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	if (!obs_source_is_scene(source) && !obs_source_is_group(source))
		return item;
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene)
		scene = obs_group_from_source(source);

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *check_item = move->items_a.array[i];
		if (check_item->item_b || check_item->move_scene)
			continue;

		obs_source_t *check_source = obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;
		if (!obs_source_is_scene(check_source) && !obs_source_is_group(check_source))
			continue;

		obs_scene_t *check_scene = obs_scene_from_source(check_source);
		if (!check_scene)
			check_scene = obs_group_from_source(check_source);

		struct match_item_nested_match mi;
		mi.check_source = check_source;
		mi.matched = false;
		obs_scene_enum_items(scene, match_item_nested_any_match, &mi);
		mi.check_source = source;
		obs_scene_enum_items(check_scene, match_item_nested_any_match, &mi);
		if (mi.matched) {
			item = check_item;
			item->move_scene = true;
			*found_pos = i;
			break;
		}
	}
	return item;
}

typedef struct move_item *(*match_function)(struct move_info *move, obs_sceneitem_t *scene_item, size_t *found_pos);

#define MATCH_FUNCTION_COUNT 8
match_function match_functions[MATCH_FUNCTION_COUNT] = {
	match_item_by_override,          match_item_by_name,    match_item_clone,  match_item_name_part,
	match_item_by_type_and_settings, match_item_scene_same, match_item_nested, match_item_scene_match};

struct move_item *create_move_item()
{
	struct move_item *item = bzalloc(sizeof(struct move_item));
	item->end_percentage = 100;
	return item;
}

bool add_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	UNUSED_PARAMETER(scene);
	if (!obs_sceneitem_visible(scene_item)) {
		return true;
	}
	struct move_info *move = data;
	struct move_item *item = create_move_item();
	da_push_back(move->items_a, &item);
	obs_sceneitem_addref(scene_item);
	item->item_a = scene_item;
	item->move_scene = obs_sceneitem_get_source(scene_item) == move->scene_source_b;
	if (item->move_scene)
		move->matched_scene_b = true;
	return true;
}

bool match_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	UNUSED_PARAMETER(scene);
	if (!obs_sceneitem_visible(scene_item)) {
		return true;
	}
	struct move_info *move = data;
	size_t old_pos = 0;
	struct move_item *item;
	if (obs_sceneitem_get_source(scene_item) == move->scene_source_a) {
		item = create_move_item();
		da_insert(move->items_a, move->item_pos, &item);
		move->item_pos++;
		item->move_scene = true;
		move->matched_scene_a = true;
	} else {
		for (size_t i = 0; i < MATCH_FUNCTION_COUNT; i++) {
			item = match_functions[i](move, scene_item, &old_pos);
			if (item)
				break;
		}
		if (item) {
			move->matched_items++;
			if (old_pos >= move->item_pos)
				move->item_pos = old_pos + 1;
		} else {
			item = create_move_item();
			da_insert(move->items_a, move->item_pos, &item);
			move->item_pos++;
		}
	}
	obs_sceneitem_addref(scene_item);
	item->item_b = scene_item;

	da_push_back(move->items_b, &item);
	return true;
}

void sceneitem_start_move(obs_sceneitem_t *item, const char *start_move)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *scene_source = obs_scene_get_source(scene);
	if (obs_source_removed(scene_source))
		return;
	obs_source_t *filter = obs_source_get_filter_by_name(scene_source, start_move);
	if (!filter) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (obs_source_removed(source))
			return;
		filter = obs_source_get_filter_by_name(source, start_move);
	}
	if (!filter)
		return;
	if (obs_source_removed(filter))
		return;

	if (is_move_filter(obs_source_get_unversioned_id(filter)))
		move_filter_start(obs_obj_get_data(filter));
}

static bool darray_sceneitem_push_back(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct darray *array = data;
	if (obs_sceneitem_visible(item))
		darray_push_back(sizeof(item), array, &item);
	return true;
}

static void move_filter_start_matching(obs_source_t *parent, obs_source_t *child, void *param)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(parent);
	if (!is_move_filter(obs_source_get_unversioned_id(child)))
		return;
	struct move_filter *move_filter = (struct move_filter *)obs_obj_get_data(child);
	if (move_filter->start_trigger == START_TRIGGER_MOVE_MATCH)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_MOVE_MATCH)
		move_filter_stop(move_filter);
}

static void move_filter_start_matching_and_check_move(obs_source_t *parent, obs_source_t *child, void *param)
{
	bool *move_filter = param;
	if (obs_source_enabled(child)) {
		const char *filter_id = obs_source_get_unversioned_id(child);
		for (size_t i = 0; i < move_render_filter_ids.num; i++) {
			if (strcmp(filter_id, move_render_filter_ids.array[i]) == 0) {
				*move_filter = true;
				break;
			}
		}
	}
	move_filter_start_matching(parent, child, NULL);
}

static void move_filter_start_in(obs_source_t *parent, obs_source_t *child, void *param)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(parent);
	if (!is_move_filter(obs_source_get_unversioned_id(child)))
		return;
	struct move_filter *move_filter = (struct move_filter *)obs_obj_get_data(child);
	if (move_filter->start_trigger == START_TRIGGER_MOVE_IN)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_MOVE_IN)
		move_filter_stop(move_filter);
}

static void move_filter_start_out(obs_source_t *parent, obs_source_t *child, void *param)
{
	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(parent);
	if (!is_move_filter(obs_source_get_unversioned_id(child)))
		return;
	struct move_filter *move_filter = (struct move_filter *)obs_obj_get_data(child);
	if (move_filter->start_trigger == START_TRIGGER_MOVE_OUT)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_MOVE_OUT)
		move_filter_stop(move_filter);
}

static void move_start_init(struct move_info *move, bool in_graphics)
{
	move->t = obs_transition_get_time(move->source);
	if (!move->start_init)
		return;
	move->start_init = false;
	move->first_frame = true;

	struct move_item *scene_flip_item = NULL;
	obs_sceneitem_t *scene_flip_sceneitem = NULL;

	obs_source_t *old_scene_a = move->scene_source_a;
	move->scene_source_a = obs_transition_get_source(move->source, OBS_TRANSITION_SOURCE_A);
	obs_source_t *old_scene_b = move->scene_source_b;
	move->scene_source_b = obs_transition_get_source(move->source, OBS_TRANSITION_SOURCE_B);

	obs_source_release(old_scene_a);
	obs_source_release(old_scene_b);

	clear_items(move, in_graphics);
	move->matched_items = 0;
	move->transition_pool_move_index = 0;
	move->transition_pool_in_index = 0;
	move->transition_pool_out_index = 0;
	move->matched_scene_a = false;
	move->matched_scene_b = false;
	move->item_pos = 0;

	if (move->scene_source_a)
		obs_source_enum_filters(move->scene_source_a, move_filter_start_out, NULL);
	if (move->scene_source_b)
		obs_source_enum_filters(move->scene_source_b, move_filter_start_in, NULL);

	obs_scene_t *scene_a = obs_scene_from_source(move->scene_source_a);
	if (!scene_a)
		scene_a = obs_group_from_source(move->scene_source_a);
	if (scene_a) {
		obs_scene_enum_items(scene_a, add_item, move);
	} else if (move->scene_source_a) {
		const char *n = obs_source_get_name(move->scene_source_a);
		obs_data_t *sd = obs_data_create();
		obs_data_set_bool(sd, "custom_size", true);
		obs_data_set_int(sd, "cx", obs_source_get_width(move->scene_source_a));
		obs_data_set_int(sd, "cy", obs_source_get_height(move->scene_source_a));
		obs_source_t *ss = obs_source_create_private("scene", n, sd);
		obs_source_load(ss);
		obs_data_release(sd);
		scene_a = obs_scene_from_source(ss);
		obs_sceneitem_t *scene_item = obs_scene_add(scene_a, move->scene_source_a);
		struct move_item *item = create_move_item();
		scene_flip_item = item;
		scene_flip_sceneitem = scene_item;

		da_push_back(move->items_a, &item);
		obs_sceneitem_addref(scene_item);
		item->item_a = scene_item;
		item->release_scene_a = scene_a;
	}
	move->item_pos = 0;
	obs_scene_t *scene_b = obs_scene_from_source(move->scene_source_b);
	if (!scene_b)
		scene_b = obs_group_from_source(move->scene_source_b);
	if (scene_b) {
		//put items in todo array
		DARRAY(obs_sceneitem_t *) items;
		da_init(items);
		obs_scene_enum_items(scene_b, darray_sceneitem_push_back, &items);
		struct move_item *item = NULL;
		for (size_t i = items.num; i > 0; i--) {
			obs_sceneitem_t *scene_item = items.array[i - 1];
			if (obs_sceneitem_get_source(scene_item) == move->scene_source_a) {

				struct move_item *item = NULL;
				for (size_t j = 0; j < MATCH_FUNCTION_COUNT; j++) {
					size_t old_pos = 0;
					item = match_functions[j](move, scene_item, &old_pos);
					if (item)
						break;
				}
				if (item && item->move_scene) {
					obs_sceneitem_release(item->item_a);
					if (item->release_scene_a)
						obs_scene_release(item->release_scene_a);
					if (item->release_scene_b)
						obs_scene_release(item->release_scene_b);
					da_erase_item(move->items_a, &item);
					bfree(item->transition_name);
					bfree(item);
					item = create_move_item();
				} else if (item) {
					move->matched_items++;
				} else {
					item = create_move_item();
				}
				item->move_scene = true;
				move->matched_scene_a = true;
				da_erase(items, i - 1);

				obs_sceneitem_addref(scene_item);
				item->item_b = scene_item;
				da_insert(move->items_b, 0, &item);
			}
		}
		if (!move->matched_scene_a) {
			for (size_t i = 0; i < MATCH_FUNCTION_COUNT; i++) {
				for (size_t j = items.num; j > 0; j--) {
					obs_sceneitem_t *scene_item = items.array[j - 1];
					size_t old_pos = 0;
					item = match_functions[i](move, scene_item, &old_pos);
					if (item) {
						move->matched_items++;

						da_erase(items, j - 1);

						obs_sceneitem_addref(scene_item);
						item->item_b = scene_item;
					}
				}
			}
		}
		for (size_t i = items.num; i > 0; i--) {
			obs_sceneitem_t *scene_item = items.array[i - 1];
			item = create_move_item();

			obs_sceneitem_addref(scene_item);
			item->item_b = scene_item;

			size_t insert = 0;
			int order = obs_sceneitem_get_order_position(scene_item);
			while (insert < move->items_b.num &&
			       order > obs_sceneitem_get_order_position(move->items_b.array[insert]->item_b)) {
				insert++;
			}
			da_insert(move->items_b, insert, &item);
		}
		da_free(items);
		//add matched items to items_b
		for (size_t i = 0; i < move->items_a.num; i++) {
			struct move_item *item = move->items_a.array[i];
			if (item->item_b) {
				size_t insert = 0;
				int order = obs_sceneitem_get_order_position(item->item_b);
				while (insert < move->items_b.num &&
				       order > obs_sceneitem_get_order_position(move->items_b.array[insert]->item_b)) {
					insert++;
				}
				da_insert(move->items_b, insert, &item);
			}
		}
		// insert not matched items from items_b into items_a
		move->item_pos = 0;
		for (size_t i = 0; i < move->items_b.num; i++) {
			struct move_item *item = move->items_b.array[i];
			if (item->item_b && !item->item_a) {
				da_insert(move->items_a, move->item_pos, &item);
				move->item_pos++;
			} else {
				for (size_t j = move->item_pos; j < move->items_a.num; j++) {
					if (item == move->items_a.array[j]) {
						move->item_pos = j + 1;
						break;
					}
				}
			}
		}
	} else if (move->scene_source_b) {
		const char *n = obs_source_get_name(move->scene_source_b);
		obs_data_t *sd = obs_data_create();
		obs_data_set_bool(sd, "custom_size", true);
		obs_data_set_int(sd, "cx", obs_source_get_width(move->scene_source_b));
		obs_data_set_int(sd, "cy", obs_source_get_height(move->scene_source_b));
		obs_source_t *ss = obs_source_create_private("scene", n, sd);
		obs_source_load(ss);
		obs_data_release(sd);
		scene_b = obs_scene_from_source(ss);
		obs_sceneitem_t *scene_item = obs_scene_add(scene_b, move->scene_source_b);
		size_t old_pos = 0;
		struct move_item *item = NULL;
		for (size_t i = 0; i < MATCH_FUNCTION_COUNT; i++) {
			item = match_functions[i](move, scene_item, &old_pos);
			if (item)
				break;
		}
		if (item) {
			move->matched_items++;
			if (old_pos >= move->item_pos)
				move->item_pos = old_pos + 1;
		} else {
			item = create_move_item();
			da_insert(move->items_a, move->item_pos, &item);
			move->item_pos++;
		}
		scene_flip_item = item;
		scene_flip_sceneitem = scene_item;
		obs_sceneitem_addref(scene_item);
		item->item_b = scene_item;
		item->release_scene_b = scene_b;
		da_push_back(move->items_b, &item);
	}
	//only a matched scene
	if (!move->matched_items && (move->matched_scene_a || move->matched_scene_b)) {
		size_t i = 0;
		while (i < move->items_a.num) {
			struct move_item *item = move->items_a.array[i];
			if (move->matched_scene_a && item->item_a) {
				obs_sceneitem_release(item->item_a);
				if (item->release_scene_a)
					obs_scene_release(item->release_scene_a);
				if (item->release_scene_b)
					obs_scene_release(item->release_scene_b);
				da_erase(move->items_a, i);
				bfree(item->transition_name);
				bfree(item);
			} else if (move->matched_scene_b && item->item_b) {
				obs_sceneitem_release(item->item_b);
				if (item->release_scene_a)
					obs_scene_release(item->release_scene_a);
				if (item->release_scene_b)
					obs_scene_release(item->release_scene_b);
				da_erase(move->items_a, i);
				bfree(item->transition_name);
				bfree(item);
			} else {
				i++;
			}
		}
		if (move->matched_scene_b) {
			move->items_b.num = 0;
		}
	}
	// insert missing items from items_a into items_b
	move->item_pos = 0;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (item->item_a && !item->item_b) {
			da_insert(move->items_b, move->item_pos, &item);
			move->item_pos++;
		} else {
			for (size_t j = move->item_pos; j < move->items_b.num; j++) {
				if (item == move->items_b.array[j]) {
					move->item_pos = j + 1;
					break;
				}
			}
		}
	}

	if (move->scene_flip_horizontal || move->scene_flip_vertical) {
		obs_sceneitem_t *other_sceneitem = NULL;
		if (!scene_flip_item) {

		} else if (scene_flip_item->item_a == scene_flip_sceneitem) {
			other_sceneitem = scene_flip_item->item_b;
		} else if (scene_flip_item->item_b == scene_flip_sceneitem) {
			other_sceneitem = scene_flip_item->item_a;
		}

		enum obs_bounds_type bt = obs_sceneitem_get_bounds_type(other_sceneitem);

		obs_sceneitem_set_bounds_type(scene_flip_sceneitem, bt);

		float width = (float)obs_source_get_width(obs_sceneitem_get_source(scene_flip_sceneitem));
		float height = (float)obs_source_get_height(obs_sceneitem_get_source(scene_flip_sceneitem));

		struct obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(scene_flip_sceneitem, &crop);

		uint32_t crop_cx = crop.left + crop.right;
		// + bounds_crop.left + bounds_crop.right;
		//cx = (crop_cx > width) ? 2 : (width - crop_cx);
		width = (crop_cx > width) ? 2 : (width - crop_cx);

		uint32_t crop_cy = crop.top + crop.bottom;
		// + bounds_crop.top + bounds_crop.bottom;
		//cy = (crop_cy > height) ? 2 : (height - crop_cy);
		height = (crop_cy > height) ? 2 : (height - crop_cy);

		struct vec2 scale;
		obs_sceneitem_get_scale(scene_flip_sceneitem, &scale);
		struct vec2 pos;
		obs_sceneitem_get_pos(scene_flip_sceneitem, &pos);
		if (move->scene_flip_horizontal) {
			if (bt == OBS_BOUNDS_NONE) {
				pos.x += width * scale.x;
			}
			scale.x *= -1.0f;
		}
		if (move->scene_flip_vertical) {
			if (bt == OBS_BOUNDS_NONE) {
				pos.y += height * scale.y;
			}
			scale.y *= -1.0f;
		}

		obs_sceneitem_set_scale(scene_flip_sceneitem, &scale);
		obs_sceneitem_set_pos(scene_flip_sceneitem, &pos);

		struct vec2 bounds;
		bounds.x = width;
		bounds.y = height;
		obs_sceneitem_set_bounds(scene_flip_sceneitem, &bounds);
	}

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if ((item->item_a && item->item_b) || item->move_scene) {
			item->easing = move->easing_move;
			item->easing_function = move->easing_function_move;
			item->transition_scale = move->transition_move_scale;
			item->curve = move->curve_move;

			obs_source_t *source_a = obs_sceneitem_get_source(item->item_a);
			obs_source_t *source_b = obs_sceneitem_get_source(item->item_b);
			if (source_a == source_b) {
				obs_source_enum_filters(source_a, move_filter_start_matching, NULL);
			} else {
				if (source_a)
					obs_source_enum_filters(source_a, move_filter_start_matching_and_check_move,
								&item->move_filter_a);
				if (source_b)
					obs_source_enum_filters(source_b, move_filter_start_matching_and_check_move,
								&item->move_filter_b);
			}

		} else if (item->item_b) {
			item->easing = move->easing_in;
			item->easing_function = move->easing_function_in;
			item->position = move->position_in;
			item->zoom = move->zoom_in;
			item->curve = move->curve_in;

			obs_source_t *source = obs_sceneitem_get_source(item->item_b);
			obs_source_enum_filters(source, move_filter_start_in, NULL);
		} else if (item->item_a) {
			item->easing = move->easing_out;
			item->easing_function = move->easing_function_out;
			item->position = move->position_out;
			item->zoom = move->zoom_out;
			item->curve = move->curve_out;

			obs_source_t *source = obs_sceneitem_get_source(item->item_a);
			obs_source_enum_filters(source, move_filter_start_out, NULL);
		}

		obs_data_t *settings_a = get_override_filter_settings(item->item_a);
		obs_data_t *settings_b = get_override_filter_settings(item->item_b);
		if (settings_a && settings_b) {
			long long val_a = obs_data_get_int(settings_a, S_EASING_MATCH);
			long long val_b = obs_data_get_int(settings_b, S_EASING_MATCH);
			if (val_a != NO_OVERRIDE && val_b != NO_OVERRIDE) {
				item->easing = (val_a & EASE_IN) | (val_b & EASE_OUT);
			} else if (val_a != NO_OVERRIDE) {
				item->easing = val_a;
			} else if (val_b != NO_OVERRIDE) {
				item->easing = val_b;
			}
			val_a = obs_data_get_int(settings_a, S_EASING_FUNCTION_MATCH);
			val_b = obs_data_get_int(settings_b, S_EASING_FUNCTION_MATCH);
			if (val_a != NO_OVERRIDE) {
				item->easing_function = val_a;
			} else if (val_b != NO_OVERRIDE) {
				item->easing_function = val_b;
			}
			const char *cv_a = obs_data_get_string(settings_a, S_TRANSITION_MATCH);
			const char *cv_b = obs_data_get_string(settings_b, S_TRANSITION_MATCH);
			if (cv_a && strlen(cv_a)) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(cv_a);
			} else if (cv_b && strlen(cv_b)) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(cv_b);
			}
			val_a = obs_data_get_int(settings_a, S_TRANSITION_SCALE);
			val_b = obs_data_get_int(settings_b, S_TRANSITION_SCALE);
			if (val_a != NO_OVERRIDE) {
				item->transition_scale = val_a;
			} else if (val_b != NO_OVERRIDE) {
				item->transition_scale = val_b;
			}
			if (obs_data_get_bool(settings_a, S_CURVE_OVERRIDE_MATCH)) {
				item->curve = (float)obs_data_get_double(settings_a, S_CURVE_MATCH);
			} else if (obs_data_get_bool(settings_b, S_CURVE_OVERRIDE_MATCH)) {
				item->curve = (float)obs_data_get_double(settings_b, S_CURVE_MATCH);
			}

			val_a = obs_data_get_int(settings_a, S_START_DELAY_MATCH_FROM);
			val_b = obs_data_get_int(settings_b, S_START_DELAY_MATCH_TO);
			if (val_a != NO_OVERRIDE && val_b != NO_OVERRIDE) {
				item->start_percentage = (int)(val_a + val_b) >> 1;
			} else if (val_a != NO_OVERRIDE) {
				item->start_percentage = (int)val_a;
			} else if (val_b != NO_OVERRIDE) {
				item->start_percentage = (int)val_b;
			}
			val_a = obs_data_get_int(settings_a, S_END_DELAY_MATCH_FROM);
			val_b = obs_data_get_int(settings_b, S_END_DELAY_MATCH_TO);
			if (val_a != NO_OVERRIDE && val_b != NO_OVERRIDE) {
				item->end_percentage = 100 - ((int)(val_a + val_b) >> 1);
			} else if (val_a != NO_OVERRIDE) {
				item->end_percentage = 100 - (int)val_a;
			} else if (val_b != NO_OVERRIDE) {
				item->end_percentage = 100 - (int)val_b;
			}
			const char *start_move_a = obs_data_get_string(settings_a, S_START_MOVE_MATCH_FROM);
			if (start_move_a && strlen(start_move_a)) {
				sceneitem_start_move(item->item_a, start_move_a);
			}
			const char *start_move_b = obs_data_get_string(settings_b, S_START_MOVE_MATCH_TO);
			if (start_move_b && strlen(start_move_b)) {
				sceneitem_start_move(item->item_b, start_move_b);
			}
		} else if (settings_a) {
			long long val;
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_a, S_EASING_MATCH);
			else
				val = obs_data_get_int(settings_a, S_EASING_OUT);
			if (val != NO_OVERRIDE) {
				item->easing = val;
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_a, S_EASING_FUNCTION_MATCH);
			else
				val = obs_data_get_int(settings_a, S_EASING_FUNCTION_OUT);
			if (val != NO_OVERRIDE) {
				item->easing_function = val;
			}
			val = obs_data_get_int(settings_a, S_ZOOM_OUT);
			if (val == ZOOM_NO) {
				item->zoom = 1.0f;
			} else if (val == ZOOM_YES) {
				item->zoom = 0.0f;
			}
			val = obs_data_get_int(settings_a, S_POSITION_OUT);
			if (val != NO_OVERRIDE) {
				item->position = val;
			}
			val = obs_data_get_int(settings_a, S_TRANSITION_SCALE);
			if (val != NO_OVERRIDE) {
				item->transition_scale = val;
			}
			const char *ti = obs_data_get_string(settings_a, S_TRANSITION_OUT);
			if (!item->move_scene && ti && strlen(ti) && item->item_a && !item->item_b) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(ti);
			}
			const char *tm = obs_data_get_string(settings_a, S_TRANSITION_MATCH);
			if (tm && strlen(tm) && ((item->item_a && item->item_b) || item->move_scene)) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(tm);
			}
			if (((item->item_a && item->item_b) || item->move_scene) &&
			    obs_data_get_bool(settings_a, S_CURVE_OVERRIDE_MATCH)) {
				item->curve = (float)obs_data_get_double(settings_a, S_CURVE_MATCH);
			} else if (!item->move_scene && item->item_a && !item->item_b &&
				   obs_data_get_bool(settings_a, S_CURVE_OVERRIDE_OUT)) {
				item->curve = (float)obs_data_get_double(settings_a, S_CURVE_OUT);
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_a, S_START_DELAY_MATCH_FROM);
			else
				val = obs_data_get_int(settings_a, S_START_DELAY_OUT);
			if (val != NO_OVERRIDE) {
				item->start_percentage = (int)val;
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_a, S_END_DELAY_MATCH_FROM);
			else
				val = obs_data_get_int(settings_a, S_END_DELAY_OUT);
			if (val != NO_OVERRIDE) {
				item->end_percentage = 100 - (int)val;
			}
			const char *move_start = ((item->item_a && item->item_b) || item->move_scene)
							 ? obs_data_get_string(settings_a, S_START_MOVE_MATCH_FROM)
							 : obs_data_get_string(settings_a, S_START_MOVE_OUT);
			if (move_start && strlen(move_start)) {
				sceneitem_start_move(item->item_a, move_start);
			}

		} else if (settings_b) {
			long long val;
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_b, S_EASING_MATCH);
			else
				val = obs_data_get_int(settings_b, S_EASING_IN);
			if (val != NO_OVERRIDE) {
				item->easing = val;
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_b, S_EASING_FUNCTION_MATCH);
			else
				val = obs_data_get_int(settings_b, S_EASING_FUNCTION_IN);
			if (val != NO_OVERRIDE) {
				item->easing_function = val;
			}
			val = obs_data_get_int(settings_b, S_ZOOM_IN);
			if (val == ZOOM_NO) {
				item->zoom = 1.0f;
			} else if (val == ZOOM_YES) {
				item->zoom = 0.0f;
			}
			val = obs_data_get_int(settings_b, S_POSITION_IN);
			if (val != NO_OVERRIDE) {
				item->position = val;
			}
			val = obs_data_get_int(settings_b, S_TRANSITION_SCALE);
			if (val != NO_OVERRIDE) {
				item->transition_scale = val;
			}
			const char *to = obs_data_get_string(settings_b, S_TRANSITION_IN);
			if (!item->move_scene && to && strlen(to) && !item->item_a && item->item_b) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(to);
			}
			const char *tm = obs_data_get_string(settings_b, S_TRANSITION_MATCH);
			if (tm && strlen(tm) && ((item->item_a && item->item_b) || item->move_scene)) {
				bfree(item->transition_name);
				item->transition_name = bstrdup(tm);
			}
			if (((item->item_a && item->item_b) || item->move_scene) &&
			    obs_data_get_bool(settings_b, S_CURVE_OVERRIDE_MATCH)) {
				item->curve = (float)obs_data_get_double(settings_b, S_CURVE_MATCH);
			} else if (!item->move_scene && !item->item_a && item->item_b &&
				   obs_data_get_bool(settings_b, S_CURVE_OVERRIDE_IN)) {
				item->curve = (float)obs_data_get_double(settings_b, S_CURVE_IN);
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_b, S_START_DELAY_MATCH_TO);
			else
				val = obs_data_get_int(settings_b, S_START_DELAY_IN);
			if (val != NO_OVERRIDE) {
				item->start_percentage = (int)val;
			}
			if ((item->item_a && item->item_b) || item->move_scene)
				val = obs_data_get_int(settings_b, S_END_DELAY_MATCH_TO);
			else
				val = obs_data_get_int(settings_b, S_END_DELAY_IN);
			if (val != NO_OVERRIDE) {
				item->end_percentage = 100 - (int)val;
			}
			const char *move_start = ((item->item_a && item->item_b) || item->move_scene)
							 ? obs_data_get_string(settings_b, S_START_MOVE_MATCH_TO)
							 : obs_data_get_string(settings_a, S_START_MOVE_IN);
			if (move_start && strlen(move_start)) {
				sceneitem_start_move(item->item_b, move_start);
			}
		}
		obs_data_release(settings_a);
		obs_data_release(settings_b);
		if (!item->transition_name && !item->move_scene && !item->item_a && item->item_b && move->transition_in &&
		    strlen(move->transition_in)) {

			bfree(item->transition_name);
			item->transition_name = bstrdup(move->transition_in);
		}
		if (!item->transition_name && !item->move_scene && item->item_a && !item->item_b && move->transition_out &&
		    strlen(move->transition_out)) {
			bfree(item->transition_name);
			item->transition_name = bstrdup(move->transition_out);
		}
		if (!item->transition_name && ((item->item_a && item->item_b) || item->move_scene) && move->transition_move &&
		    strlen(move->transition_move)) {
			bfree(item->transition_name);
			item->transition_name = bstrdup(move->transition_move);
		}
	}
}

static void move_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct move_info *move = data;
	move->first_frame = false;
	move_start_init(move, false);
}

static void move_video_render(void *data, gs_effect_t *effect)
{
	struct move_info *move = data;

	da_push_back(move_rendering, &move);

	move_start_init(move, true);

	if (move->t >= 0.0f && move->t < 1.0f) {
		if (!move->scene_source_a)
			move->scene_source_a = obs_transition_get_source(move->source, OBS_TRANSITION_SOURCE_A);
		if (!move->scene_source_b)
			move->scene_source_b = obs_transition_get_source(move->source, OBS_TRANSITION_SOURCE_B);

		gs_matrix_push();
		gs_blend_state_push();
		gs_reset_blend_state();
		if (move->t * 100.0 < move->item_order_switch_percentage) {
			for (size_t i = 0; i < move->items_a.num; i++) {
				struct move_item *item = move->items_a.array[i];
				render2_item(move, item);
			}
		} else {
			for (size_t i = 0; i < move->items_b.num; i++) {
				struct move_item *item = move->items_b.array[i];
				render2_item(move, item);
			}
		}
		gs_blend_state_pop();
		gs_matrix_pop();

	} else {
		if (move->items_a.num) {
			clear_items(move, true);
		}
		if (move->scene_source_a) {
			obs_source_release(move->scene_source_a);
			move->scene_source_a = NULL;
		}
		if (move->scene_source_b) {
			obs_source_release(move->scene_source_b);
			move->scene_source_b = NULL;
		}
		if (move->t <= 0.5f) {
			obs_transition_video_render_direct(move->source, OBS_TRANSITION_SOURCE_A);
		} else {
			obs_transition_video_render_direct(move->source, OBS_TRANSITION_SOURCE_B);
		}
	}

	da_pop_back(move_rendering);

	UNUSED_PARAMETER(effect);
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - CubicEaseInOut(t);
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return CubicEaseInOut(t);
}

static bool move_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers, size_t channels,
			      size_t sample_rate)
{
	struct move_info *move = data;
	return obs_transition_audio_render(move->source, ts_out, audio, mixers, channels, sample_rate, mix_a, mix_b);
}

void prop_list_add_positions(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("Position.None"), POS_NONE);
	obs_property_list_add_int(p, obs_module_text("Position.Center"), POS_CENTER);
	obs_property_list_add_int(p, obs_module_text("Position.CenterInverse"), POS_EDGE);
	obs_property_list_add_int(p, obs_module_text("Position.TopLeft"), POS_EDGE | POS_TOP | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.TopCenter"), POS_EDGE | POS_TOP);
	obs_property_list_add_int(p, obs_module_text("Position.TopRight"), POS_EDGE | POS_TOP | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.CenterRight"), POS_EDGE | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.BottomRight"), POS_EDGE | POS_BOTTOM | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.BottomCenter"), POS_EDGE | POS_BOTTOM);
	obs_property_list_add_int(p, obs_module_text("Position.BottomLeft"), POS_EDGE | POS_BOTTOM | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.CenterLeft"), POS_EDGE | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.Left"), POS_SWIPE | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.Top"), POS_SWIPE | POS_TOP);
	obs_property_list_add_int(p, obs_module_text("Position.Right"), POS_SWIPE | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.Bottom"), POS_SWIPE | POS_BOTTOM);
}

void prop_list_add_easings(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("Easing.None"), EASE_NONE);
	obs_property_list_add_int(p, obs_module_text("Easing.In"), EASE_IN);
	obs_property_list_add_int(p, obs_module_text("Easing.Out"), EASE_OUT);
	obs_property_list_add_int(p, obs_module_text("Easing.InOut"), EASE_IN_OUT);
}

void prop_list_add_easing_functions(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Quadratic"), EASING_QUADRATIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Cubic"), EASING_CUBIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Quartic"), EASING_QUARTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Quintic"), EASING_QUINTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Sine"), EASING_SINE);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Circular"), EASING_CIRCULAR);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Exponential"), EASING_EXPONENTIAL);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Elastic"), EASING_ELASTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Bounce"), EASING_BOUNCE);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Back"), EASING_BACK);
}

void prop_list_add_transitions(obs_property_t *p)
{
	struct obs_frontend_source_list transitions = {0};
	obs_property_list_add_string(p, obs_module_text("Transition.None"), "None");
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *name = obs_source_get_name(transitions.sources.array[i]);
		obs_property_list_add_string(p, name, name);
	}
	obs_frontend_source_list_free(&transitions);
}

void prop_list_add_scales(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("TransitionScale.MaxOnly"), OBS_TRANSITION_SCALE_MAX_ONLY);
	obs_property_list_add_int(p, obs_module_text("TransitionScale.Aspect"), OBS_TRANSITION_SCALE_ASPECT);
	obs_property_list_add_int(p, obs_module_text("TransitionScale.Stretch"), OBS_TRANSITION_SCALE_STRETCH);
}

static obs_properties_t *move_properties(void *data)
{
	obs_property_t *p;
	obs_properties_t *ppts = obs_properties_create();
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_bool(group, S_NAME_PART_MATCH, obs_module_text("NamePartMatch"));
	obs_properties_add_bool(group, S_NAME_NUMBER_MATCH, obs_module_text("NameNumberMatch"));
	obs_properties_add_bool(group, S_NAME_LAST_WORD_MATCH, obs_module_text("NameLastWordMatch"));

	obs_properties_add_group(ppts, S_MATCH, obs_module_text("MatchName"), OBS_GROUP_NORMAL, group);

	group = obs_properties_create();

	obs_properties_add_bool(group, S_NESTED_SCENES, obs_module_text("NestedScenes"));
	obs_properties_add_bool(group, S_CACHE_TRANSITIONS, obs_module_text("CacheTransitions"));

	p = obs_properties_add_int_slider(group, S_SWITCH_PERCENTAGE, obs_module_text("SwitchPoint"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");

	obs_properties_add_group(ppts, S_MOVE_ALL, obs_module_text("MoveAll"), OBS_GROUP_NORMAL, group);

	//Matched items
	group = obs_properties_create();
	p = obs_properties_add_list(group, S_EASING_MATCH, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_MATCH, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_list(group, S_TRANSITION_MATCH, obs_module_text("Transition"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	p = obs_properties_add_list(group, S_TRANSITION_SCALE, obs_module_text("TransitionScaleType"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_scales(p);

	obs_properties_add_float_slider(group, S_CURVE_MATCH, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_group(ppts, S_MOVE_MATCH, obs_module_text("MoveMatch"), OBS_GROUP_NORMAL, group);

	//Move in
	group = obs_properties_create();

	p = obs_properties_add_list(group, S_EASING_IN, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_IN, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_float_slider(group, S_ZOOM_IN, obs_module_text("Zoom"), 0.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, "%");

	p = obs_properties_add_list(group, S_POSITION_IN, obs_module_text("Position"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	p = obs_properties_add_list(group, S_TRANSITION_IN, obs_module_text("Transition"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	obs_properties_add_float_slider(group, S_CURVE_IN, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_group(ppts, S_MOVE_IN, obs_module_text("MoveIn"), OBS_GROUP_NORMAL, group);

	//Move out
	group = obs_properties_create();

	p = obs_properties_add_list(group, S_EASING_OUT, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_OUT, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_float_slider(group, S_ZOOM_OUT, obs_module_text("Zoom"), 0.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, "%");

	p = obs_properties_add_list(group, S_POSITION_OUT, obs_module_text("Position"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	p = obs_properties_add_list(group, S_TRANSITION_OUT, obs_module_text("Transition"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	obs_properties_add_float_slider(group, S_CURVE_OUT, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_group(ppts, S_MOVE_OUT, obs_module_text("MoveOut"), OBS_GROUP_NORMAL, group);
	obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
	UNUSED_PARAMETER(data);
	return ppts;
}

void move_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_IN, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_OUT, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH, EASING_CUBIC);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_IN, EASING_CUBIC);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_OUT, EASING_CUBIC);
	obs_data_set_default_int(settings, S_POSITION_IN, POS_EDGE | POS_LEFT);
	obs_data_set_default_double(settings, S_ZOOM_IN, 0.0);
	obs_data_set_default_int(settings, S_POSITION_OUT, POS_EDGE | POS_RIGHT);
	obs_data_set_default_double(settings, S_ZOOM_OUT, 0.0);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
	obs_data_set_default_double(settings, S_CURVE_IN, 0.0);
	obs_data_set_default_double(settings, S_CURVE_OUT, 0.0);
	obs_data_set_default_int(settings, S_SWITCH_PERCENTAGE, 50);
	obs_data_set_default_bool(settings, S_NESTED_SCENES, true);
}

static void move_start(void *data)
{
	struct move_info *move = data;
	move->start_init = true;
}

static void move_stop(void *data)
{
	struct move_info *move = data;
	clear_items(move, false);
}

static void move_enum_active_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct move_info *move = data;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (item->transition)
			enum_callback(move->source, item->transition, param);
	}
}

static void move_enum_all_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct move_info *move = data;
	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (item->transition)
			enum_callback(move->source, item->transition, param);
	}
}

struct obs_source_info move_transition = {
	.id = "move_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = move_get_name,
	.create = move_create,
	.destroy = move_destroy,
	.update = move_update,
	.video_tick = move_video_tick,
	.video_render = move_video_render,
	.audio_render = move_audio_render,
	.get_properties = move_properties,
	.get_defaults = move_defaults,
	.transition_start = move_start,
	.transition_stop = move_stop,
	.enum_active_sources = move_enum_active_sources,
	.enum_all_sources = move_enum_all_sources,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("move-transition", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("MoveTransition");
}

extern struct obs_source_info move_transition_override_filter;
extern struct obs_source_info move_source_filter;
extern struct obs_source_info move_source_swap_filter;
extern struct obs_source_info move_value_filter;
extern struct obs_source_info move_audio_value_filter;
extern struct obs_source_info audio_move_filter;
extern struct obs_source_info move_action_filter;
extern struct obs_source_info move_audio_action_filter;
#ifdef WIN32
extern struct obs_source_info nvidia_move_filter;
extern bool load_nvar(void);
extern void unload_nvar(void);
void SetMoveDirectShowFilter(struct obs_source_info *obs_source_info);
#endif

extern DARRAY(struct udp_server) udp_servers;
extern pthread_mutex_t udp_servers_mutex;

static float move_get_transition_filter(obs_source_t *filter_from, obs_source_t **filter_to)
{
	if (!filter_from)
		return 0.0f;
	if (!move_rendering.num)
		return 0.0f;
	struct move_info *move = move_rendering.array[move_rendering.num - 1];

	obs_source_t *source_from = obs_filter_get_parent(filter_from);
	if (!source_from)
		return 0.0f;

	for (size_t i = 0; i < move->items_a.num; i++) {
		struct move_item *item = move->items_a.array[i];
		if (!item->item_a || !item->item_b)
			continue;
		obs_source_t *source_to = NULL;
		bool reverse = false;
		if ((item->item_a && obs_sceneitem_get_source(item->item_a) == source_from)) {
			source_to = obs_sceneitem_get_source(item->item_b);
		} else if (item->item_b && obs_sceneitem_get_source(item->item_b) == source_from) {
			source_to = obs_sceneitem_get_source(item->item_a);
			reverse = true;
		} else {
			continue;
		}
		if (filter_to && source_to) {
			if (source_to == source_from) {
				*filter_to = filter_from;
			} else {
				const char *filter_name = obs_source_get_name(filter_from);
				*filter_to = obs_source_get_filter_by_name(source_to, filter_name);
				obs_source_release(*filter_to);
				if (!reverse && !*filter_to && !item->move_filter_a)
					return 0.0f;
				if (reverse && !*filter_to && !item->move_filter_b)
					return 0.0f;
				if (*filter_to && !obs_source_enabled(*filter_to)) {
					*filter_to = NULL;
				}

				if (*filter_to && strcmp(obs_source_get_unversioned_id(*filter_to),
							 obs_source_get_unversioned_id(filter_from)) != 0) {
					*filter_to = NULL;
					return 0.0f;
				}
			}
		}
		float t = move->t;
		if (filter_to && !*filter_to && source_to && source_to != source_from && item->move_filter_a &&
		    item->move_filter_b) {
			if (reverse) {
				if (t > 0.5f)
					t = (t - 0.5f) * 2.0f;
				else
					t = 0.0f;
			} else {
				if (t <= 0.5)
					t *= 2.0f;
				else
					t = 1.0f;
			}
			t = get_eased(t, item->easing, item->easing_function);
		} else if (item->start_percentage > 0 || item->end_percentage < 100) {
			if (item->start_percentage > item->end_percentage) {
				float avg_switch_point = (float)(item->start_percentage + item->end_percentage) / 200.0f;
				if (t > avg_switch_point) {
					t = 1.0f;
				}
			} else if (t * 100.0 < item->start_percentage) {
				t = 0.0f;
			} else if (t * 100.0 > item->end_percentage) {
				t = 1.0f;
			} else {
				int duration_percentage = item->end_percentage - item->start_percentage;
				t = t - (float)item->start_percentage / 100.0f;
				t = t / (float)duration_percentage * 100.0f;
				t = get_eased(t, item->easing, item->easing_function);
			}
		} else {
			t = get_eased(t, item->easing, item->easing_function);
		}

		if (reverse)
			t = 1.0f - t;
		return t;
	}
	return 0.0f;
}

static void move_get_transition_filter_function(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(data);
	calldata_set_ptr(calldata, "callback", move_get_transition_filter);
	const char *filter_id = calldata_string(calldata, "filter_id");
	if (filter_id) {
		filter_id = bstrdup(filter_id);
		da_push_back(move_render_filter_ids, &filter_id);
	}
}

bool move_exit = false;

void move_frontend_event(enum obs_frontend_event event, void* data) {
	UNUSED_PARAMETER(data);
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		move_exit = true;
	}
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Move Transition] loaded version %s", PROJECT_VERSION);
	da_init(udp_servers);
	da_init(move_rendering);
	da_init(move_render_filter_ids);
	pthread_mutex_init(&udp_servers_mutex, NULL);
	obs_register_source(&move_transition);
	obs_register_source(&move_transition_override_filter);
	obs_register_source(&move_source_filter);
	obs_register_source(&move_source_swap_filter);
	obs_register_source(&move_value_filter);
	obs_register_source(&move_audio_value_filter);
	obs_register_source(&audio_move_filter);
	obs_register_source(&move_action_filter);
	obs_register_source(&move_audio_action_filter);
#ifdef WIN32
	if (load_nvar())
		obs_register_source(&nvidia_move_filter);
	struct obs_source_info move_directshow_filter = {0};
	SetMoveDirectShowFilter(&move_directshow_filter);
	obs_register_source(&move_directshow_filter);
#endif

	proc_handler_t *ph = obs_get_proc_handler();
	proc_handler_add(ph, "void move_get_transition_filter_function(in string filter_id, out ptr callback)",
			 move_get_transition_filter_function, NULL);

	obs_frontend_add_event_callback(move_frontend_event, NULL);
	return true;
}

obs_websocket_vendor vendor = NULL;
void obs_module_post_load()
{
	vendor = obs_websocket_register_vendor("move");
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(move_frontend_event, NULL);
	da_free(udp_servers);
	da_free(move_rendering);
	for (size_t i = 0; i < move_render_filter_ids.num; i++) {
		bfree(move_render_filter_ids.array[i]);
	}
	da_free(move_render_filter_ids);
	//pthread_mutex_destroy(&udp_servers_mutex);
}
