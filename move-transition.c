#include <obs-module.h>
#include <../UI/obs-frontend-api/obs-frontend-api.h>
#include "../obs-transitions/easings.h"
#include "graphics/math-defs.h"
#include "graphics/matrix4.h"
#include "move-transition.h"
#include "easing.h"

#define S_POSITION_IN "position_in"
#define S_POSITION_OUT "position_out"
#define S_ZOOM_IN "zoom_in"
#define S_ZOOM_OUT "zoom_out"
#define S_EASING "easing"
#define S_EASING_FUNCTION "easing_function"
#define S_CURVE "curve"
#define S_TRANSITION_IN "transition_in"
#define S_TRANSITION_OUT "transition_out"
#define S_TRANSITION_MOVE "transition_move"
#define S_TRANSITION_MOVE_SCALE "transition_move_scale"
#define S_NAME_PART_MATCH "name_part_match"
#define S_NAME_NUMBER_MATCH "name_number_match"
#define S_NAME_LAST_WORD_MATCH "name_last_word_match"

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

struct move_info {
	obs_source_t *source;
	bool start_init;
	DARRAY(struct move_item *) items;
	float ot;
	float t;
	float curve;
	obs_source_t *scene_source_a;
	obs_source_t *scene_source_b;
	gs_samplerstate_t *point_sampler;
	long long easing;
	long long easing_function;
	bool zoom_in;
	bool zoom_out;
	long long position_in;
	long long position_out;
	char *transition_in;
	char *transition_out;
	char *transition_move;
	bool part_match;
	bool number_match;
	bool last_word_match;
	bool stopped;
	enum obs_transition_scale_type transition_move_scale;
};

struct move_item {
	obs_sceneitem_t *item_a;
	obs_sceneitem_t *item_b;
	gs_texrender_t *item_render;
	obs_source_t *transition;
};

static const char *move_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Name");
}

static void *move_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_info *move;
	move = bzalloc(sizeof(struct move_info));
	move->source = source;
	da_init(move->items);
	obs_source_update(source, settings);

	return move;
}

static void clear_items(struct move_info *move)
{
	for (size_t i = 0; i < move->items.num; i++) {
		struct move_item *item = move->items.array[i];
		obs_sceneitem_release(item->item_a);
		item->item_a = NULL;
		obs_sceneitem_release(item->item_b);
		item->item_b = NULL;
		gs_texrender_destroy(item->item_render);
		item->item_render = NULL;
		obs_source_release(item->transition);
		item->transition = NULL;
		bfree(item);
	}
	move->items.num = 0;
}

static void move_destroy(void *data)
{
	struct move_info *move = data;
	clear_items(move);
	da_free(move->items);
	obs_source_release(move->scene_source_a);
	obs_source_release(move->scene_source_b);
	bfree(move->transition_in);
	bfree(move->transition_out);
	bfree(move->transition_move);
	bfree(move);
}

static void move_update(void *data, obs_data_t *settings)
{
	struct move_info *move = data;
	move->easing = obs_data_get_int(settings, S_EASING);
	move->easing_function = obs_data_get_int(settings, S_EASING_FUNCTION);
	move->position_in = obs_data_get_int(settings, S_POSITION_IN);
	move->zoom_in = obs_data_get_bool(settings, S_ZOOM_IN);
	move->position_out = obs_data_get_int(settings, S_POSITION_OUT);
	move->zoom_out = obs_data_get_bool(settings, S_ZOOM_OUT);
	move->curve = (float)obs_data_get_double(settings, S_CURVE);
	bfree(move->transition_in);
	move->transition_in =
		bstrdup(obs_data_get_string(settings, S_TRANSITION_IN));
	bfree(move->transition_out);
	move->transition_out =
		bstrdup(obs_data_get_string(settings, S_TRANSITION_OUT));
	move->part_match = obs_data_get_bool(settings, S_NAME_PART_MATCH);
	move->number_match = obs_data_get_bool(settings, S_NAME_NUMBER_MATCH);
	move->last_word_match =
		obs_data_get_bool(settings, S_NAME_LAST_WORD_MATCH);
	bfree(move->transition_move);
	move->transition_move =
		bstrdup(obs_data_get_string(settings, S_TRANSITION_MOVE));
	move->transition_move_scale =
		obs_data_get_int(settings, S_TRANSITION_MOVE_SCALE);
}

void add_alignment(struct vec2 *v, uint32_t align, int cx, int cy)
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

static void calculate_bounds_data(struct obs_scene_item *item,
				  struct vec2 *origin, struct vec2 *scale,
				  uint32_t *cx, uint32_t *cy,
				  struct vec2 *bounds)
{
	float width = (float)(*cx) * fabsf(scale->x);
	float height = (float)(*cy) * fabsf(scale->y);
	float item_aspect = width / height;
	float bounds_aspect = bounds->x / bounds->y;
	uint32_t bounds_type = obs_sceneitem_get_bounds_type(item);
	float width_diff, height_diff;

	if (bounds_type == OBS_BOUNDS_MAX_ONLY)
		if (width > bounds->x || height > bounds->y)
			bounds_type = OBS_BOUNDS_SCALE_INNER;

	if (bounds_type == OBS_BOUNDS_SCALE_INNER ||
	    bounds_type == OBS_BOUNDS_SCALE_OUTER) {
		bool use_width = (bounds_aspect < item_aspect);
		float mul;

		if (obs_sceneitem_get_bounds_type(item) ==
		    OBS_BOUNDS_SCALE_OUTER)
			use_width = !use_width;

		mul = use_width ? bounds->x / width : bounds->y / height;

		vec2_mulf(scale, scale, mul);

	} else if (bounds_type == OBS_BOUNDS_SCALE_TO_WIDTH) {
		vec2_mulf(scale, scale, bounds->x / width);

	} else if (bounds_type == OBS_BOUNDS_SCALE_TO_HEIGHT) {
		vec2_mulf(scale, scale, bounds->y / height);

	} else if (bounds_type == OBS_BOUNDS_STRETCH) {
		scale->x = bounds->x / (float)(*cx);
		scale->y = bounds->y / (float)(*cy);
	}

	width = (float)(*cx) * scale->x;
	height = (float)(*cy) * scale->y;
	width_diff = bounds->x - width;
	height_diff = bounds->y - height;
	*cx = (uint32_t)bounds->x;
	*cy = (uint32_t)bounds->y;

	add_alignment(origin, obs_sceneitem_get_bounds_alignment(item),
		      (int)-width_diff, (int)-height_diff);
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

static inline bool item_texture_enabled(struct obs_scene_item *item)
{
	if (!item)
		false;
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	return crop_enabled(&crop) || scale_filter_enabled(item) ||
	       (item_is_scene(item) && !obs_sceneitem_is_group(item));
}

void pos_add_center(struct vec2 *pos, uint32_t alignment, uint32_t cx,
		    uint32_t cy)
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

void pos_subtract_center(struct vec2 *pos, uint32_t alignment, uint32_t cx,
			 uint32_t cy)
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

void calc_edge_position(struct vec2 *pos, long long position,
			uint32_t canvas_width, uint32_t canvas_height,
			uint32_t alignment, uint32_t cx, uint32_t cy, bool zoom)
{
	uint32_t cx2 = cx >> 1;
	uint32_t cy2 = cy >> 1;
	if (zoom) {
		cx2 = 0;
		cy2 = 0;
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
				float move_x = -(pos->x + cx2);
				float move_y =
					diff_y * (diff_x + move_x) / diff_x;
				vec2_set(pos, -(float)cx2,
					 (float)(canvas_height >> 1) + move_y);
			} else {
				//right edge
				float move_x = (canvas_width - pos->x) + cx2;
				float move_y =
					diff_y * (diff_x + move_x) / diff_x;
				vec2_set(pos, (float)canvas_width + cx2,
					 (float)(canvas_height >> 1) + move_y);
			}
		} else {
			if (diff_y < 0.0f) {
				//top edge
				float move_y = -(pos->y + cy2);
				float move_x =
					diff_x * (diff_y + move_y) / diff_y;
				vec2_set(pos,
					 (float)(canvas_width >> 1) + move_x,
					 -(float)cy2);
			} else {
				//bottom edge
				float move_y = (canvas_height - pos->y) + cy2;
				float move_x =
					diff_x * (diff_y + move_y) / diff_y;
				vec2_set(pos,
					 (float)(canvas_width >> 1) + move_x,
					 (float)canvas_height + cy2);
			}
		}

		if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx2;
		} else if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx2;
		}
		if (alignment & OBS_ALIGN_TOP) {
			pos->y -= cy2;
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y += cy2;
		}

		return;
	}
	if (zoom) {
		cx = 0;
		cy = 0;
	}
	vec2_set(pos, 0, 0);
	if (position & POS_RIGHT) {
		pos->x += canvas_width;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx;
		} else if (alignment & OBS_ALIGN_LEFT) {

		} else {
			pos->x += cx2;
		}
	} else if (position & POS_LEFT) {
		if (alignment & OBS_ALIGN_RIGHT) {

		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx;
		} else {
			pos->x -= cx2;
		}
	} else {
		pos->x += canvas_width >> 1;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx2;
		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx2;
		}
	}

	if (position & POS_BOTTOM) {
		pos->y += canvas_height;
		if (alignment & OBS_ALIGN_TOP) {
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y += cy;
		} else {
			pos->y += cy2;
		}
	} else if (position & POS_TOP) {
		if (alignment & OBS_ALIGN_BOTTOM) {
		} else if (alignment & OBS_ALIGN_TOP) {
			pos->y -= cy;
		} else {
			pos->y -= cy2;
		}
	} else {
		pos->y += canvas_height >> 1;
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
	return p * bezier(point, t, order - 1) +
	       t * bezier(&point[1], t, order - 1);
}

void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control,
		 struct vec2 *end, const float t)
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
		const char *n =
			obs_source_get_name(transitions.sources.array[i]);
		if (n && strcmp(n, name) == 0) {
			obs_source_t *transition = transitions.sources.array[i];
			obs_source_addref(transition);
			obs_frontend_source_list_free(&transitions);
			return transition;
		}
	}
	obs_frontend_source_list_free(&transitions);
	return NULL;
}

bool render2_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct move_info *move = data;
	if (!obs_sceneitem_visible(scene_item))
		return true;
	struct move_item *item = NULL;
	for (size_t i = 0; i < move->items.num; i++) {
		struct move_item *check_item = move->items.array[i];
		if (check_item->item_a == scene_item ||
		    check_item->item_b == scene_item) {
			item = check_item;
			break;
		}
	}

	if (!item)
		return true;

	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);
	bool move_out = item->item_a == scene_item;
	if (item->item_a && item->item_b) {
		if (item->item_a == scene_item && move->t > 0.5f)
			return true;
		if (item->item_b == scene_item && move->t <= 0.5f)
			return true;
		if (move->transition_move && !item->transition) {
			obs_source_t *transition = obs_frontend_get_transition(
				move->transition_move);
			if (transition) {
				if (obs_source_get_type(transition) ==
				    OBS_SOURCE_TYPE_TRANSITION) {
					item->transition = obs_source_duplicate(
						transition, NULL, true);
					obs_transition_set_size(
						item->transition, width,
						height);
					obs_transition_set_alignment(
						item->transition,
						OBS_ALIGN_CENTER);
					obs_transition_set_scale_type(
						item->transition,
						move->transition_move_scale);
					obs_transition_set(
						item->transition,
						obs_sceneitem_get_source(
							item->item_a));
					obs_transition_start(
						item->transition,
						OBS_TRANSITION_MODE_MANUAL,
						obs_frontend_get_transition_duration(),
						obs_sceneitem_get_source(
							item->item_b));
				}
				obs_source_release(transition);
			}
		}
		if (item->transition) {
			uint32_t width_a = obs_source_get_width(
				obs_sceneitem_get_source(item->item_a));
			uint32_t width_b = obs_source_get_width(
				obs_sceneitem_get_source(item->item_b));
			uint32_t height_a = obs_source_get_height(
				obs_sceneitem_get_source(item->item_a));
			uint32_t height_b = obs_source_get_height(
				obs_sceneitem_get_source(item->item_b));
			width = (1.0f - move->t) * width_a + move->t * width_b;
			height = (1.0f - move->t) * height_a +
				 move->t * height_b;
			obs_transition_set_size(item->transition, width,
						height);
		}
	} else if (move_out && move->transition_out && !item->transition) {
		obs_source_t *transition =
			obs_frontend_get_transition(move->transition_out);
		if (transition) {
			if (obs_source_get_type(transition) ==
			    OBS_SOURCE_TYPE_TRANSITION) {
				item->transition = obs_source_duplicate(
					transition, NULL, true);
				obs_transition_set_size(item->transition, width,
							height);
				obs_transition_set_alignment(item->transition,
							     OBS_ALIGN_CENTER);
				obs_transition_set_scale_type(
					item->transition,
					OBS_TRANSITION_SCALE_ASPECT);
				obs_transition_set(item->transition, source);
				obs_transition_start(
					item->transition,
					OBS_TRANSITION_MODE_MANUAL,
					obs_frontend_get_transition_duration(),
					NULL);
			}
			obs_source_release(transition);
		}
	} else if (!move_out && move->transition_in && !item->transition) {
		obs_source_t *transition =
			obs_frontend_get_transition(move->transition_in);
		if (transition) {
			if (obs_source_get_type(transition) ==
			    OBS_SOURCE_TYPE_TRANSITION) {
				item->transition = obs_source_duplicate(
					transition, NULL, true);
				obs_transition_set_size(item->transition, width,
							height);
				obs_transition_set_alignment(item->transition,
							     OBS_ALIGN_CENTER);
				obs_transition_set_scale_type(
					item->transition,
					OBS_TRANSITION_SCALE_ASPECT);
				obs_transition_set(item->transition, NULL);
				obs_transition_start(
					item->transition,
					OBS_TRANSITION_MODE_MANUAL,
					obs_frontend_get_transition_duration(),
					source);
			}
			obs_source_release(transition);
		}
	}
	uint32_t original_width = width;
	uint32_t original_height = height;
	struct obs_sceneitem_crop crop;
	if (item->item_a && item->item_b) {
		struct obs_sceneitem_crop crop_a;
		obs_sceneitem_get_crop(item->item_a, &crop_a);
		struct obs_sceneitem_crop crop_b;
		obs_sceneitem_get_crop(item->item_b, &crop_b);
		crop.left =
			(int)((float)(1.0f - move->ot) * (float)crop_a.left +
			      move->ot * (float)crop_b.left);
		crop.top = (int)((float)(1.0f - move->ot) * (float)crop_a.top +
				 move->ot * (float)crop_b.top);
		crop.right =
			(int)((float)(1.0f - move->ot) * (float)crop_a.right +
			      move->ot * (float)crop_b.right);
		crop.bottom =
			(int)((float)(1.0f - move->ot) * (float)crop_a.bottom +
			      move->ot * (float)crop_b.bottom);
	} else {
		obs_sceneitem_get_crop(scene_item, &crop);
	}
	uint32_t crop_cx = crop.left + crop.right;
	uint32_t cx = (crop_cx > width) ? 2 : (width - crop_cx);
	uint32_t crop_cy = crop.top + crop.bottom;
	uint32_t cy = (crop_cy > height) ? 2 : (height - crop_cy);
	struct vec2 scale;
	struct vec2 original_scale;
	obs_sceneitem_get_scale(scene_item, &original_scale);
	if (item->item_a && item->item_b) {
		struct vec2 scale_a;
		obs_sceneitem_get_scale(item->item_a, &scale_a);
		struct vec2 scale_b;
		obs_sceneitem_get_scale(item->item_b, &scale_b);
		vec2_set(&scale,
			 (1.0f - move->t) * scale_a.x + move->t * scale_b.x,
			 (1.0f - move->t) * scale_a.y + move->t * scale_b.y);
	} else {
		if (obs_sceneitem_get_bounds_type(scene_item) !=
		    OBS_BOUNDS_NONE) {
			obs_sceneitem_get_scale(scene_item, &scale);
		} else {
			obs_sceneitem_get_scale(scene_item, &scale);
			if (!move_out && move->zoom_in) {
				vec2_set(&scale, move->t * scale.x,
					 move->t * scale.y);
			} else if (move_out && move->zoom_out) {
				vec2_set(&scale, (1.0f - move->t) * scale.x,
					 (1.0f - move->t) * scale.y);
			}
		}
	}
	width = cx;
	height = cy;
	uint32_t original_cx = cx;
	uint32_t original_cy = cy;

	struct vec2 base_origin;
	struct vec2 origin;
	struct vec2 origin2;
	vec2_zero(&base_origin);
	vec2_zero(&origin);
	vec2_zero(&origin2);

	if (obs_sceneitem_get_bounds_type(scene_item) != OBS_BOUNDS_NONE) {
		struct vec2 bounds;
		if (item->item_a && item->item_b) {
			struct vec2 bounds_a;
			obs_sceneitem_get_bounds(item->item_a, &bounds_a);
			struct vec2 bounds_b;
			obs_sceneitem_get_bounds(item->item_b, &bounds_b);
			vec2_set(&bounds,
				 (1.0f - move->t) * bounds_a.x +
					 move->t * bounds_b.x,
				 (1.0f - move->t) * bounds_a.y +
					 move->t * bounds_b.y);
		} else {
			obs_sceneitem_get_bounds(scene_item, &bounds);
			if (!move_out && move->zoom_in) {
				vec2_set(&bounds, move->t * bounds.x,
					 move->t * bounds.y);
			} else if (move_out && move->zoom_out) {
				vec2_set(&bounds, (1.0f - move->t) * bounds.x,
					 (1.0f - move->t) * bounds.y);
			}
		}
		calculate_bounds_data(scene_item, &origin, &scale, &cx, &cy,
				      &bounds);
		struct vec2 original_bounds;
		obs_sceneitem_get_bounds(scene_item, &original_bounds);
		calculate_bounds_data(scene_item, &origin2, &original_scale,
				      &original_cx, &original_cy,
				      &original_bounds);
	} else {
		original_cx = (uint32_t)((float)cx * original_scale.x);
		original_cy = (uint32_t)((float)cy * original_scale.y);
		cx = (uint32_t)((float)cx * scale.x);
		cy = (uint32_t)((float)cy * scale.y);
	}

	add_alignment(&origin, obs_sceneitem_get_alignment(scene_item), (int)cx,
		      (int)cy);

	struct matrix4 draw_transform;
	matrix4_identity(&draw_transform);
	matrix4_scale3f(&draw_transform, &draw_transform, scale.x, scale.y,
			1.0f);
	matrix4_translate3f(&draw_transform, &draw_transform, -origin.x,
			    -origin.y, 0.0f);
	float rot;
	if (item->item_a && item->item_b) {
		float rot_a = obs_sceneitem_get_rot(item->item_a);
		float rot_b = obs_sceneitem_get_rot(item->item_b);
		rot = (1.0f - move->t) * rot_a + move->t * rot_b;
	} else {
		rot = obs_sceneitem_get_rot(scene_item);
	}
	matrix4_rotate_aa4f(&draw_transform, &draw_transform, 0.0f, 0.0f, 1.0f,
			    RAD(rot));

	uint32_t canvas_width = obs_source_get_width(move->source);
	uint32_t canvas_height = obs_source_get_height(move->source);
	struct vec2 pos_a;
	if (item->item_a) {
		obs_sceneitem_get_pos(item->item_a, &pos_a);
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (move->position_in & POS_CENTER) {
			vec2_set(&pos_a, canvas_width >> 1, canvas_height >> 1);
			if (!move->zoom_in)
				pos_add_center(&pos_a, alignment, cx, cy);
		} else if (move->position_in & POS_EDGE) {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			calc_edge_position(&pos_a, move->position_in,
					   canvas_width, canvas_height,
					   alignment, original_cx, original_cy,
					   move->zoom_in);

		} else {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			if (move->zoom_in)
				pos_subtract_center(&pos_a, alignment,
						    original_cx, original_cy);
		}
	}
	struct vec2 pos_b;
	if (item->item_b) {
		obs_sceneitem_get_pos(item->item_b, &pos_b);
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (move->position_out & POS_CENTER) {
			vec2_set(&pos_b, canvas_width >> 1, canvas_height >> 1);
			if (!move->zoom_out)
				pos_add_center(&pos_b, alignment, cx, cy);
		} else if (move->position_out & POS_EDGE) {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			calc_edge_position(&pos_b, move->position_out,
					   canvas_width, canvas_height,
					   alignment, original_cx, original_cy,
					   move->zoom_out);

		} else {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			if (move->zoom_out)
				pos_subtract_center(&pos_b, alignment,
						    original_cx, original_cy);
		}
	}
	struct vec2 pos;
	if (move->curve != 0.0f) {
		float diff_x = fabsf(pos_a.x - pos_b.x);
		float diff_y = fabsf(pos_a.y - pos_b.y);
		struct vec2 control_pos;
		vec2_set(&control_pos, 0.5f * pos_a.x + 0.5f * pos_b.x,
			 0.5f * pos_a.y + 0.5f * pos_b.y);
		if (control_pos.x >= (canvas_width >> 1)) {
			control_pos.x += diff_y * move->curve;
		} else {
			control_pos.x -= diff_y * move->curve;
		}
		if (control_pos.y >= (canvas_height >> 1)) {
			control_pos.y += diff_x * move->curve;
		} else {
			control_pos.y -= diff_x * move->curve;
		}
		vec2_bezier(&pos, &pos_a, &control_pos, &pos_b, move->t);
	} else {
		vec2_set(&pos, (1.0f - move->t) * pos_a.x + move->t * pos_b.x,
			 (1.0f - move->t) * pos_a.y + move->t * pos_b.y);
	}

	matrix4_translate3f(&draw_transform, &draw_transform, pos.x, pos.y,
			    0.0f);

	struct vec2 output_scale = scale;

	if (item->item_render && !item_texture_enabled(item->item_a) &&
	    !item_texture_enabled(item->item_b)) {
		gs_texrender_destroy(item->item_render);
		item->item_render = NULL;
	} else if (item_texture_enabled(item->item_a) ||
		   item_texture_enabled(item->item_b)) {
		gs_texrender_destroy(item->item_render);
		item->item_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	}
	if (!move->point_sampler) {
		struct gs_sampler_info point_sampler_info = {0};
		point_sampler_info.max_anisotropy = 1;
		move->point_sampler =
			gs_samplerstate_create(&point_sampler_info);
	}

	if (item->item_render) {
		if (width && height &&
		    gs_texrender_begin(item->item_render, width, height)) {
			float cx_scale = (float)original_width / (float)width;
			float cy_scale = (float)original_height / (float)height;
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)original_width, 0.0f,
				 (float)original_height, -100.0f, 100.0f);

			gs_matrix_scale3f(cx_scale, cy_scale, 1.0f);
			gs_matrix_translate3f(-(float)crop.left,
					      -(float)crop.top, 0.0f);

			if (item->transition) {
				obs_transition_set_manual_time(item->transition,
							       move->ot);

				obs_source_video_render(item->transition);
			} else {
				obs_source_video_render(source);
			}

			gs_texrender_end(item->item_render);
		}
	}

	gs_matrix_push();
	gs_matrix_mul(&draw_transform);
	if (item->item_render) {
		//render_item_texture(item);
		gs_texture_t *tex = gs_texrender_get_texture(item->item_render);
		if (!tex) {
			gs_matrix_pop();
			return true;
		}

		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

		enum obs_scale_type type =
			obs_sceneitem_get_scale_filter(scene_item);
		cx = gs_texture_get_width(tex);
		cy = gs_texture_get_height(tex);
		const char *tech = "Draw";
		if (type != OBS_SCALE_DISABLE) {
			if (type == OBS_SCALE_POINT) {
				gs_eparam_t *image =
					gs_effect_get_param_by_name(effect,
								    "image");
				gs_effect_set_next_sampler(image,
							   move->point_sampler);
			} else if (!close_float(output_scale.x, 1.0f,
						EPSILON) ||
				   !close_float(output_scale.y, 1.0f,
						EPSILON)) {
				gs_eparam_t *scale_param;
				gs_eparam_t *scale_i_param;

				if (output_scale.x < 0.5f ||
				    output_scale.y < 0.5f) {
					effect = obs_get_base_effect(
						OBS_EFFECT_BILINEAR_LOWRES);
				} else if (type == OBS_SCALE_BICUBIC) {
					effect = obs_get_base_effect(
						OBS_EFFECT_BICUBIC);
				} else if (type == OBS_SCALE_LANCZOS) {
					effect = obs_get_base_effect(
						OBS_EFFECT_LANCZOS);
				} else if (type == OBS_SCALE_AREA) {
					effect = obs_get_base_effect(
						OBS_EFFECT_AREA);
					if ((output_scale.x >= 1.0f) &&
					    (output_scale.y >= 1.0f))
						tech = "DrawUpscale";
				}

				scale_param = gs_effect_get_param_by_name(
					effect, "base_dimension");
				if (scale_param) {
					struct vec2 base_res = {(float)cx,
								(float)cy};

					gs_effect_set_vec2(scale_param,
							   &base_res);
				}

				scale_i_param = gs_effect_get_param_by_name(
					effect, "base_dimension_i");
				if (scale_i_param) {
					struct vec2 base_res_i = {
						1.0f / (float)cx,
						1.0f / (float)cy};

					gs_effect_set_vec2(scale_i_param,
							   &base_res_i);
				}
			}
		}

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

		while (gs_effect_loop(effect, tech))
			obs_source_draw(tex, 0, 0, 0, 0, 0);

		gs_blend_state_pop();
	} else {
		if (item->transition) {
			obs_transition_set_manual_time(item->transition,
						       move->ot);

			obs_source_video_render(item->transition);
		} else {
			obs_source_video_render(source);
		}
	}
	gs_matrix_pop();
	return true;
}

bool same_transform_type(struct obs_transform_info *info_a,
			 struct obs_transform_info *info_b)
{
	if (!info_a || !info_b)
		return false;

	return info_a->alignment == info_b->alignment &&
	       info_a->bounds_type == info_b->bounds_type &&
	       info_a->bounds_alignment == info_b->bounds_alignment;
}

bool is_number_match(const char c)
{
	if (c >= '0' && c <= '9')
		return true;
	if (c == '(' || c == ')' || c == ' ' || c == '.' || c == ',')
		return true;
	return false;
}

struct move_item *match_item2(struct move_info *move,
			      obs_sceneitem_t *scene_item, bool part_match)
{
	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	for (size_t i = 0; i < move->items.num; i++) {
		struct move_item *check_item = move->items.array[i];
		if (check_item->item_b)
			continue;
		if (!obs_sceneitem_visible(check_item->item_a))
			continue;
		if (obs_sceneitem_get_bounds_type(check_item->item_a) !=
		    obs_sceneitem_get_bounds_type(scene_item))
			continue;
		if (obs_sceneitem_get_alignment(check_item->item_a) !=
		    obs_sceneitem_get_alignment(scene_item))
			continue;
		if (obs_sceneitem_get_bounds_type(scene_item) !=
			    OBS_BOUNDS_NONE &&
		    obs_sceneitem_get_bounds_alignment(check_item->item_a) !=
			    obs_sceneitem_get_bounds_alignment(scene_item))
			continue;

		obs_source_t *check_source =
			obs_sceneitem_get_source(check_item->item_a);
		if (!check_source)
			continue;

		if (check_source == source) {
			item = check_item;
			break;
		}
		const char *name_a = obs_source_get_name(check_source);
		const char *name_b = obs_source_get_name(source);
		if (name_a && name_b) {
			if (strcmp(name_a, name_b) == 0) {
				item = check_item;
				break;
			}
			if (part_match) {
				size_t len_a = strlen(name_a);
				size_t len_b = strlen(name_b);
				if (!len_a || !len_b)
					continue;
				if (len_a > len_b) {
					if (move->last_word_match) {
						char *last_space =
							strrchr(name_b, ' ');
						if (last_space &&
						    last_space > name_b) {
							len_b = last_space -
								name_b;
						}
					}
					while (len_b > 0 &&
					       move->number_match &&
					       is_number_match(
						       name_b[len_b - 1]))
						len_b--;
					if (len_b > 0 && move->part_match) {
						for (size_t pos = 0;
						     pos <= len_a - len_b;
						     pos++) {
							if (memcmp(name_a + pos,
								   name_b,
								   len_b) ==
							    0) {
								item = check_item;
								break;
							}
						}
						if (item)
							break;
					} else if (len_b > 0 &&
						   memcmp(name_a, name_b,
							  len_b) == 0) {
						item = check_item;
						break;
					}

				} else {
					if (move->last_word_match) {
						char *last_space =
							strrchr(name_a, ' ');
						if (last_space &&
						    last_space > name_a) {
							len_a = last_space -
								name_a;
						}
					}
					while (len_a > 0 &&
					       move->number_match &&
					       is_number_match(
						       name_a[len_a - 1]))
						len_a--;
					if (len_a > 0 && move->part_match) {
						for (size_t pos = 0;
						     pos <= len_b - len_a;
						     pos++) {
							if (memcmp(name_a,
								   name_b + pos,
								   len_a) ==
							    0) {
								item = check_item;
								break;
							}
						}
						if (item)
							break;
					} else if (len_a > 0 &&
						   memcmp(name_a, name_b,
							  len_a) == 0) {
						item = check_item;
						break;
					}
				}
			}
		} else if (!part_match) {
			if (obs_source_get_type(check_source) ==
			    obs_source_get_type(source)) {
				obs_data_t *settings =
					obs_source_get_settings(source);
				obs_data_t *check_settings =
					obs_source_get_settings(check_source);
				if (settings && check_settings &&
				    strcmp(obs_data_get_json(settings),
					   obs_data_get_json(check_settings)) ==
					    0) {
					item = check_item;
					obs_data_release(check_settings);
					obs_data_release(settings);
					break;
				}
				obs_data_release(check_settings);
				obs_data_release(settings);
			}
		}
	}
	return item;
}

bool match_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	struct move_info *move = data;
	struct move_item *item = NULL;

	if (scene == obs_scene_from_source(move->scene_source_a)) {
		item = bzalloc(sizeof(struct move_item));
		item->item_a = scene_item;
		obs_sceneitem_addref(item->item_a);
		da_push_back(move->items, &item);
	} else if (scene == obs_scene_from_source(move->scene_source_b)) {
		item = match_item2(move, scene_item, false);
		if (!item && (move->part_match || move->number_match ||
			      move->last_word_match)) {
			item = match_item2(move, scene_item, true);
		}
		if (!item) {
			item = bzalloc(sizeof(struct move_item));
			da_push_back(move->items, &item);
		}
		item->item_b = scene_item;
		obs_sceneitem_addref(item->item_b);
	}
	return true;
}

static void move_video_render(void *data, gs_effect_t *effect)
{
	struct move_info *move = data;

	float t = obs_transition_get_time(move->source);
	if (EASE_NONE == move->easing) {
		move->t = t;
	} else if (EASE_IN == move->easing) {
		switch (move->easing_function) {
		case EASING_QUADRATIC:
			move->t = QuadraticEaseIn(t);
			break;
		case EASING_CUBIC:
			move->t = CubicEaseIn(t);
			break;
		case EASING_QUARTIC:
			move->t = QuarticEaseIn(t);
			break;
		case EASING_QUINTIC:
			move->t = QuinticEaseIn(t);
			break;
		case EASING_SINE:
			move->t = SineEaseIn(t);
			break;
		case EASING_CIRCULAR:
			move->t = CircularEaseIn(t);
			break;
		case EASING_EXPONENTIAL:
			move->t = ExponentialEaseIn(t);
			break;
		case EASING_ELASTIC:
			move->t = ElasticEaseIn(t);
			break;
		case EASING_BOUNCE:
			move->t = BounceEaseIn(t);
			break;
		case EASING_BACK:
			move->t = BackEaseIn(t);
			break;
		}
	} else if (EASE_OUT == move->easing) {
		switch (move->easing_function) {
		case EASING_QUADRATIC:
			move->t = QuadraticEaseOut(t);
			break;
		case EASING_CUBIC:
			move->t = CubicEaseOut(t);
			break;
		case EASING_QUARTIC:
			move->t = QuarticEaseOut(t);
			break;
		case EASING_QUINTIC:
			move->t = QuinticEaseOut(t);
			break;
		case EASING_SINE:
			move->t = SineEaseOut(t);
			break;
		case EASING_CIRCULAR:
			move->t = CircularEaseOut(t);
			break;
		case EASING_EXPONENTIAL:
			move->t = ExponentialEaseOut(t);
			break;
		case EASING_ELASTIC:
			move->t = ElasticEaseOut(t);
			break;
		case EASING_BOUNCE:
			move->t = BounceEaseOut(t);
			break;
		case EASING_BACK:
			move->t = BackEaseOut(t);
			break;
		}
	} else if (EASE_IN_OUT == move->easing) {
		switch (move->easing_function) {
		case EASING_QUADRATIC:
			move->t = QuadraticEaseInOut(t);
			break;
		case EASING_CUBIC:
			move->t = CubicEaseInOut(t);
			break;
		case EASING_QUARTIC:
			move->t = QuarticEaseInOut(t);
			break;
		case EASING_QUINTIC:
			move->t = QuinticEaseInOut(t);
			break;
		case EASING_SINE:
			move->t = SineEaseInOut(t);
			break;
		case EASING_CIRCULAR:
			move->t = CircularEaseInOut(t);
			break;
		case EASING_EXPONENTIAL:
			move->t = ExponentialEaseInOut(t);
			break;
		case EASING_ELASTIC:
			move->t = ElasticEaseInOut(t);
			break;
		case EASING_BOUNCE:
			move->t = BounceEaseInOut(t);
			break;
		case EASING_BACK:
			move->t = BackEaseInOut(t);
			break;
		}
	}
	move->ot = move->t;
	if (move->t > 1.0f)
		move->ot = 1.0f;
	else if (move->t < 0.0f)
		move->ot = 0.0f;

	if (move->start_init) {
		if (move->scene_source_a)
			obs_source_release(move->scene_source_a);
		move->scene_source_a = obs_transition_get_source(
			move->source, OBS_TRANSITION_SOURCE_A);
		if (move->scene_source_b)
			obs_source_release(move->scene_source_b);
		move->scene_source_b = obs_transition_get_source(
			move->source, OBS_TRANSITION_SOURCE_B);

		clear_items(move);

		obs_scene_enum_items(
			obs_scene_from_source(move->scene_source_a), match_item,
			data);
		obs_scene_enum_items(
			obs_scene_from_source(move->scene_source_b), match_item,
			data);
		move->start_init = false;
		move->stopped = false;
	}

	if (t > 0.0f && t < 1.0f) {
		if (!move->scene_source_a)
			move->scene_source_a = obs_transition_get_source(
				move->source, OBS_TRANSITION_SOURCE_A);
		if (!move->scene_source_b)
			move->scene_source_b = obs_transition_get_source(
				move->source, OBS_TRANSITION_SOURCE_B);

		gs_matrix_push();
		gs_blend_state_push();
		gs_reset_blend_state();
		if (t <= 0.5f) {
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
		} else {
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
		}

		gs_blend_state_pop();
		gs_matrix_pop();

	} else if (t <= 0.5f) {
		obs_transition_video_render_direct(move->source,
						   OBS_TRANSITION_SOURCE_A);
	} else {
		obs_transition_video_render_direct(move->source,
						   OBS_TRANSITION_SOURCE_B);
		if (!move->stopped) {
			move->stopped = true;
			obs_transition_force_stop(move->source);
		}
	}

	UNUSED_PARAMETER(effect);
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - cubic_ease_in_out(t);
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return cubic_ease_in_out(t);
}

static bool move_audio_render(void *data, uint64_t *ts_out,
			      struct obs_source_audio_mix *audio,
			      uint32_t mixers, size_t channels,
			      size_t sample_rate)
{
	struct move_info *move = data;
	return obs_transition_audio_render(move->source, ts_out, audio, mixers,
					   channels, sample_rate, mix_a, mix_b);
}

void prop_list_add_positions(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("Position.None"),
				  POS_NONE);
	obs_property_list_add_int(p, obs_module_text("Position.Center"),
				  POS_CENTER);
	obs_property_list_add_int(p, obs_module_text("Position.CenterInverse"),
				  POS_EDGE);
	obs_property_list_add_int(p, obs_module_text("Position.TopLeft"),
				  POS_EDGE | POS_TOP | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.TopCenter"),
				  POS_EDGE | POS_TOP);
	obs_property_list_add_int(p, obs_module_text("Position.TopRight"),
				  POS_EDGE | POS_TOP | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.CenterRight"),
				  POS_EDGE | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.BottomRight"),
				  POS_EDGE | POS_BOTTOM | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.BottomCenter"),
				  POS_EDGE | POS_BOTTOM);
	obs_property_list_add_int(p, obs_module_text("Position.BottomLeft"),
				  POS_EDGE | POS_BOTTOM | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.CenterLeft"),
				  POS_EDGE | POS_LEFT);
}

void prop_list_add_easings(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("Easing.None"), EASE_NONE);
	obs_property_list_add_int(p, obs_module_text("Easing.In"), EASE_IN);
	obs_property_list_add_int(p, obs_module_text("Easing.Out"), EASE_OUT);
	obs_property_list_add_int(p, obs_module_text("Easing.InOut"),
				  EASE_IN_OUT);
}

void prop_list_add_easing_functions(obs_property_t *p)
{
	obs_property_list_add_int(p,
				  obs_module_text("EasingFunction.Quadratic"),
				  EASING_QUADRATIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Cubic"),
				  EASING_CUBIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Quartic"),
				  EASING_QUARTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Quintic"),
				  EASING_QUINTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Sine"),
				  EASING_SINE);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Circular"),
				  EASING_CIRCULAR);
	obs_property_list_add_int(p,
				  obs_module_text("EasingFunction.Exponential"),
				  EASING_EXPONENTIAL);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Elastic"),
				  EASING_ELASTIC);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Bounce"),
				  EASING_BOUNCE);
	obs_property_list_add_int(p, obs_module_text("EasingFunction.Back"),
				  EASING_BACK);
}

static bool easing_modified(obs_properties_t *ppts, obs_property_t *p,
			    obs_data_t *settings)
{
	long long easing = obs_data_get_int(settings, S_EASING);
	p = obs_properties_get(ppts, S_EASING_FUNCTION);

	obs_property_set_visible(p, EASE_NONE != easing);

	return true;
}

static bool transition_move_modified(obs_properties_t *ppts, obs_property_t *p,
				     obs_data_t *settings)
{
	const char *transition_move =
		obs_data_get_string(settings, S_TRANSITION_MOVE);
	p = obs_properties_get(ppts, S_TRANSITION_MOVE_SCALE);
	obs_property_set_visible(p, strlen(transition_move));
	return true;
}

static void prop_list_add_transitions(obs_property_t *p)
{
	struct obs_frontend_source_list transitions = {0};
	obs_property_list_add_string(p, obs_module_text("Transition.None"),
				     NULL);
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		if (strcmp(obs_source_get_unversioned_id(
				   transitions.sources.array[i]),
			   "move_transition") == 0)
			continue;
		const char *name =
			obs_source_get_name(transitions.sources.array[i]);
		obs_property_list_add_string(p, name, name);
	}
	obs_frontend_source_list_free(&transitions);
}

static obs_properties_t *move_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;
	p = obs_properties_add_list(ppts, S_EASING, obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);
	obs_property_set_modified_callback(p, easing_modified);

	p = obs_properties_add_list(ppts, S_EASING_FUNCTION,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	obs_properties_add_bool(ppts, S_ZOOM_IN, obs_module_text("ZoomIn"));
	p = obs_properties_add_list(ppts, S_POSITION_IN,
				    obs_module_text("PositionIn"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	obs_properties_add_bool(ppts, S_ZOOM_OUT, obs_module_text("ZoomOut"));
	p = obs_properties_add_list(ppts, S_POSITION_OUT,
				    obs_module_text("PositionOut"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	obs_properties_add_float_slider(ppts, S_CURVE, obs_module_text("Curve"),
					-2.0, 2.0, 0.01);

	p = obs_properties_add_list(ppts, S_TRANSITION_IN,
				    obs_module_text("TransitionIn"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	p = obs_properties_add_list(ppts, S_TRANSITION_OUT,
				    obs_module_text("TransitionOut"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	obs_properties_add_bool(ppts, S_NAME_PART_MATCH,
				obs_module_text("NamePartMatch"));
	obs_properties_add_bool(ppts, S_NAME_NUMBER_MATCH,
				obs_module_text("NameNumberMatch"));
	obs_properties_add_bool(ppts, S_NAME_LAST_WORD_MATCH,
				obs_module_text("NameLastWordMatch"));

	p = obs_properties_add_list(ppts, S_TRANSITION_MOVE,
				    obs_module_text("TransitionMove"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);
	obs_property_set_modified_callback(p, transition_move_modified);
	p = obs_properties_add_list(ppts, S_TRANSITION_MOVE_SCALE,
				    obs_module_text("TransitionMoveScaleType"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(
		p, obs_module_text("TransitionMoveScale.MaxOnly"),
		OBS_TRANSITION_SCALE_MAX_ONLY);
	obs_property_list_add_int(p,
				  obs_module_text("TransitionMoveScale.Aspect"),
				  OBS_TRANSITION_SCALE_ASPECT);
	obs_property_list_add_int(
		p, obs_module_text("TransitionMoveScale.Stretch"),
		OBS_TRANSITION_SCALE_STRETCH);
	UNUSED_PARAMETER(data);
	return ppts;
}

void move_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_EASING, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION, EASING_CUBIC);
	obs_data_set_default_int(settings, S_POSITION_IN, POS_EDGE | POS_LEFT);
	obs_data_set_default_bool(settings, S_ZOOM_IN, true);
	obs_data_set_default_int(settings, S_POSITION_OUT,
				 POS_EDGE | POS_RIGHT);
	obs_data_set_default_bool(settings, S_ZOOM_OUT, true);
	obs_data_set_default_double(settings, S_CURVE, 0.0);
}

static void move_start(void *data)
{
	struct move_info *move = data;
	move->start_init = true;
}

static void move_stop(void *data)
{
	struct move_info *move = data;
	clear_items(move);
	if (move->scene_source_a)
		obs_source_release(move->scene_source_a);
	move->scene_source_a = NULL;

	if (move->scene_source_b)
		obs_source_release(move->scene_source_b);
	move->scene_source_b = NULL;
}

struct obs_source_info move_transition = {.id = "move_transition",
					  .type = OBS_SOURCE_TYPE_TRANSITION,
					  .get_name = move_get_name,
					  .create = move_create,
					  .destroy = move_destroy,
					  .update = move_update,
					  .video_render = move_video_render,
					  .audio_render = move_audio_render,
					  .get_properties = move_properties,
					  .get_defaults = move_defaults,
					  .transition_start = move_start,
					  .transition_stop = move_stop};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("move-transition", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

bool obs_module_load(void)
{
	obs_register_source(&move_transition);
	return true;
}
