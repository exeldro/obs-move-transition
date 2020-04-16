#include <obs-module.h>
#include <../UI/obs-frontend-api/obs-frontend-api.h>
#include "../obs-transitions/easings.h"
#include "graphics/math-defs.h"
#include "graphics/matrix4.h"
#include "move-transition.h"
#include "easing.h"

struct move_info {
	obs_source_t *source;
	bool start_init;
	DARRAY(struct move_item *) items;
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
	bool zoom_in;
	bool zoom_out;
	long long position_in;
	long long position_out;
	char *transition_move;
	char *transition_in;
	char *transition_out;
	bool part_match;
	bool number_match;
	bool last_word_match;
	enum obs_transition_scale_type transition_move_scale;
	bool render_match;
};

struct move_item {
	obs_sceneitem_t *item_a;
	obs_sceneitem_t *item_b;
	gs_texrender_t *item_render;
	obs_source_t *transition;
	long long easing;
	long long easing_function;
	bool zoom;
	long long position;
	char *transition_name;
	enum obs_transition_scale_type transition_scale;
	float curve;
};

static const char *move_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Move");
}

static void *move_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_info *move = bzalloc(sizeof(struct move_info));
	move->source = source;
	da_init(move->items);
	obs_source_update(source, settings);
	return move;
}

static void clear_items(struct move_info *move)
{
	bool graphics = false;

	for (size_t i = 0; i < move->items.num; i++) {
		struct move_item *item = move->items.array[i];
		obs_sceneitem_release(item->item_a);
		item->item_a = NULL;
		obs_sceneitem_release(item->item_b);
		item->item_b = NULL;
		if (item->item_render) {
			if (!graphics) {
				obs_enter_graphics();
				graphics = true;
			}
			gs_texrender_destroy(item->item_render);
			item->item_render = NULL;
		}
		if (item->transition) {
			obs_transition_force_stop(item->transition);
			obs_source_release(item->transition);
			item->transition = NULL;
		}
		bfree(item->transition_name);
		bfree(item);
	}
	if (graphics)
		obs_leave_graphics();
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
	move->easing_function_move =
		obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);
	move->easing_function_in =
		obs_data_get_int(settings, S_EASING_FUNCTION_IN);
	move->easing_function_out =
		obs_data_get_int(settings, S_EASING_FUNCTION_OUT);
	move->position_in = obs_data_get_int(settings, S_POSITION_IN);
	move->zoom_in = obs_data_get_bool(settings, S_ZOOM_IN);
	move->position_out = obs_data_get_int(settings, S_POSITION_OUT);
	move->zoom_out = obs_data_get_bool(settings, S_ZOOM_OUT);
	move->curve_move = (float)obs_data_get_double(settings, S_CURVE_MATCH);
	move->curve_in = (float)obs_data_get_double(settings, S_CURVE_IN);
	move->curve_out = (float)obs_data_get_double(settings, S_CURVE_OUT);
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
		bstrdup(obs_data_get_string(settings, S_TRANSITION_MATCH));
	move->transition_move_scale =
		obs_data_get_int(settings, S_TRANSITION_SCALE);
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
	if (position & POS_EDGE)
		vec2_set(pos, 0, 0);
	if (position & POS_RIGHT) {
		pos->x = canvas_width;
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
		pos->x = canvas_width >> 1;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx2;
		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx2;
		}
	}

	if (position & POS_BOTTOM) {
		pos->y = canvas_height;
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
		pos->y = canvas_height >> 1;
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

	if (move->render_match && (!item->item_a || !item->item_b))
		return true;
	if (!move->render_match && item->item_a && item->item_b)
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
		if (item->transition_name && !item->transition) {
			obs_source_t *transition = obs_frontend_get_transition(
				item->transition_name);
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
						item->transition_scale);
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
	} else if (move_out && item->transition_name && !item->transition) {
		obs_source_t *transition =
			obs_frontend_get_transition(item->transition_name);
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
	} else if (!move_out && item->transition_name && !item->transition) {
		obs_source_t *transition =
			obs_frontend_get_transition(item->transition_name);
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

	float t = move->t;
	if (EASE_NONE == item->easing) {
	} else if (EASE_IN == item->easing) {
		switch (item->easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseIn(move->t);
			break;
		case EASING_CUBIC:
			t = CubicEaseIn(move->t);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseIn(move->t);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseIn(move->t);
			break;
		case EASING_SINE:
			t = SineEaseIn(move->t);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseIn(move->t);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseIn(move->t);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseIn(move->t);
			break;
		case EASING_BOUNCE:
			t = BounceEaseIn(move->t);
			break;
		case EASING_BACK:
			t = BackEaseIn(move->t);
			break;
		default:;
		}
	} else if (EASE_OUT == item->easing) {
		switch (item->easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseOut(move->t);
			break;
		case EASING_CUBIC:
			t = CubicEaseOut(move->t);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseOut(move->t);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseOut(move->t);
			break;
		case EASING_SINE:
			t = SineEaseOut(move->t);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseOut(move->t);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseOut(move->t);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseOut(move->t);
			break;
		case EASING_BOUNCE:
			t = BounceEaseOut(move->t);
			break;
		case EASING_BACK:
			t = BackEaseOut(move->t);
			break;
		default:;
		}
	} else if (EASE_IN_OUT == item->easing) {
		switch (item->easing_function) {
		case EASING_QUADRATIC:
			t = QuadraticEaseInOut(move->t);
			break;
		case EASING_CUBIC:
			t = CubicEaseInOut(move->t);
			break;
		case EASING_QUARTIC:
			t = QuarticEaseInOut(move->t);
			break;
		case EASING_QUINTIC:
			t = QuinticEaseInOut(move->t);
			break;
		case EASING_SINE:
			t = SineEaseInOut(move->t);
			break;
		case EASING_CIRCULAR:
			t = CircularEaseInOut(move->t);
			break;
		case EASING_EXPONENTIAL:
			t = ExponentialEaseInOut(move->t);
			break;
		case EASING_ELASTIC:
			t = ElasticEaseInOut(move->t);
			break;
		case EASING_BOUNCE:
			t = BounceEaseInOut(move->t);
			break;
		case EASING_BACK:
			t = BackEaseInOut(move->t);
			break;
		default:;
		}
	}
	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	if (item->item_a && item->item_b && item->transition &&
	    !move->start_init) {
		uint32_t width_a = obs_source_get_width(
			obs_sceneitem_get_source(item->item_a));
		uint32_t width_b = obs_source_get_width(
			obs_sceneitem_get_source(item->item_b));
		uint32_t height_a = obs_source_get_height(
			obs_sceneitem_get_source(item->item_a));
		uint32_t height_b = obs_source_get_height(
			obs_sceneitem_get_source(item->item_b));
		width = (1.0f - t) * width_a + t * width_b;
		height = (1.0f - t) * height_a + t * height_b;
		obs_transition_set_size(item->transition, width, height);
	}

	uint32_t original_width = width;
	uint32_t original_height = height;
	struct obs_sceneitem_crop crop;
	if (item->item_a && item->item_b) {
		struct obs_sceneitem_crop crop_a;
		obs_sceneitem_get_crop(item->item_a, &crop_a);
		struct obs_sceneitem_crop crop_b;
		obs_sceneitem_get_crop(item->item_b, &crop_b);
		crop.left = (int)((float)(1.0f - ot) * (float)crop_a.left +
				  ot * (float)crop_b.left);
		crop.top = (int)((float)(1.0f - ot) * (float)crop_a.top +
				 ot * (float)crop_b.top);
		crop.right = (int)((float)(1.0f - ot) * (float)crop_a.right +
				   ot * (float)crop_b.right);
		crop.bottom = (int)((float)(1.0f - ot) * (float)crop_a.bottom +
				    ot * (float)crop_b.bottom);
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
		vec2_set(&scale, (1.0f - t) * scale_a.x + t * scale_b.x,
			 (1.0f - t) * scale_a.y + t * scale_b.y);
	} else {
		if (obs_sceneitem_get_bounds_type(scene_item) !=
		    OBS_BOUNDS_NONE) {
			obs_sceneitem_get_scale(scene_item, &scale);
		} else {
			obs_sceneitem_get_scale(scene_item, &scale);
			if (!move_out && item->zoom) {
				vec2_set(&scale, t * scale.x, t * scale.y);
			} else if (move_out && item->zoom) {
				vec2_set(&scale, (1.0f - t) * scale.x,
					 (1.0f - t) * scale.y);
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
				 (1.0f - t) * bounds_a.x + t * bounds_b.x,
				 (1.0f - t) * bounds_a.y + t * bounds_b.y);
		} else {
			obs_sceneitem_get_bounds(scene_item, &bounds);
			if (!move_out && item->zoom) {
				vec2_set(&bounds, t * bounds.x, t * bounds.y);
			} else if (move_out && item->zoom) {
				vec2_set(&bounds, (1.0f - t) * bounds.x,
					 (1.0f - t) * bounds.y);
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
		rot = (1.0f - t) * rot_a + t * rot_b;
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
		if (item->position & POS_CENTER) {
			vec2_set(&pos_a, canvas_width >> 1, canvas_height >> 1);
			if (!item->zoom)
				pos_add_center(&pos_a, alignment, cx, cy);
		} else if (item->position & POS_EDGE ||
			   item->position & POS_SWIPE) {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			calc_edge_position(&pos_a, item->position, canvas_width,
					   canvas_height, alignment,
					   original_cx, original_cy,
					   item->zoom);

		} else {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			if (item->zoom)
				pos_subtract_center(&pos_a, alignment,
						    original_cx, original_cy);
		}
	}
	struct vec2 pos_b;
	if (item->item_b) {
		obs_sceneitem_get_pos(item->item_b, &pos_b);
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (item->position & POS_CENTER) {
			vec2_set(&pos_b, canvas_width >> 1, canvas_height >> 1);
			if (!item->zoom)
				pos_add_center(&pos_b, alignment, cx, cy);
		} else if (item->position & POS_EDGE ||
			   item->position & POS_SWIPE) {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			calc_edge_position(&pos_b, item->position, canvas_width,
					   canvas_height, alignment,
					   original_cx, original_cy,
					   item->zoom);

		} else {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			if (item->zoom)
				pos_subtract_center(&pos_b, alignment,
						    original_cx, original_cy);
		}
	}
	struct vec2 pos;
	if (item->curve != 0.0f) {
		float diff_x = fabsf(pos_a.x - pos_b.x);
		float diff_y = fabsf(pos_a.y - pos_b.y);
		struct vec2 control_pos;
		vec2_set(&control_pos, 0.5f * pos_a.x + 0.5f * pos_b.x,
			 0.5f * pos_a.y + 0.5f * pos_b.y);
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
		vec2_set(&pos, (1.0f - t) * pos_a.x + t * pos_b.x,
			 (1.0f - t) * pos_a.y + t * pos_b.y);
	}

	matrix4_translate3f(&draw_transform, &draw_transform, pos.x, pos.y,
			    0.0f);

	struct vec2 output_scale = scale;

	if (item->item_render && !item_texture_enabled(item->item_a) &&
	    !item_texture_enabled(item->item_b)) {
		gs_texrender_destroy(item->item_render);
		item->item_render = NULL;
	} else if (!item->item_render && (item_texture_enabled(item->item_a) ||
					  item_texture_enabled(item->item_b))) {
		item->item_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else if (item->item_render) {
		gs_texrender_reset(item->item_render);
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

			if (item->transition && !move->start_init) {
				obs_transition_set_manual_time(item->transition,
							       ot);

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
		if (item->transition && !move->start_init) {
			obs_transition_set_manual_time(item->transition, ot);
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

struct move_item *create_move_item()
{
	struct move_item *item = bzalloc(sizeof(struct move_item));
	return item;
}

bool match_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	struct move_info *move = data;
	struct move_item *item = NULL;

	if (scene == obs_scene_from_source(move->scene_source_a)) {
		item = create_move_item();
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
			item = create_move_item();
			da_push_back(move->items, &item);
		}
		item->item_b = scene_item;
		obs_sceneitem_addref(item->item_b);
	}
	return true;
}
void get_override_filter(obs_source_t *source, obs_source_t *filter,
			 void *param)
{

	if (strcmp(obs_source_get_unversioned_id(filter),
		   "move_transition_override_filter") != 0)
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
		obs_source_enum_filters(scene_source, get_override_filter,
					&filter);
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

static void move_video_render(void *data, gs_effect_t *effect)
{
	struct move_info *move = data;

	move->t = obs_transition_get_time(move->source);

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
		for (size_t i = 0; i < move->items.num; i++) {
			struct move_item *item = move->items.array[i];
			if (item->item_a && item->item_b) {
				item->easing = move->easing_move;
				item->easing_function =
					move->easing_function_move;
				item->transition_scale =
					move->transition_move_scale;
				item->curve = move->curve_move;
			} else if (item->item_b) {
				item->easing = move->easing_in;
				item->easing_function =
					move->easing_function_in;
				item->position = move->position_in;
				item->zoom = move->zoom_in;
				item->curve = move->curve_in;
			} else if (item->item_a) {
				item->easing = move->easing_out;
				item->easing_function =
					move->easing_function_out;
				item->position = move->position_out;
				item->zoom = move->zoom_out;
				item->curve = move->curve_out;
			}

			obs_data_t *settings_a =
				get_override_filter_settings(item->item_a);
			obs_data_t *settings_b =
				get_override_filter_settings(item->item_b);
			if (settings_a && settings_b) {
				long long val_a = obs_data_get_int(
					settings_a, S_EASING_MATCH);
				long long val_b = obs_data_get_int(
					settings_b, S_EASING_MATCH);
				if (val_a != NO_OVERRIDE &&
				    val_b != NO_OVERRIDE) {
					item->easing = (val_a & EASE_IN) |
						       (val_b & EASE_OUT);
				} else if (val_a != NO_OVERRIDE) {
					item->easing = val_a;
				} else if (val_b != NO_OVERRIDE) {
					item->easing = val_b;
				}
				val_a = obs_data_get_int(
					settings_a, S_EASING_FUNCTION_MATCH);
				val_b = obs_data_get_int(
					settings_b, S_EASING_FUNCTION_MATCH);
				if (val_a != NO_OVERRIDE) {
					item->easing_function = val_a;
				} else if (val_b != NO_OVERRIDE) {
					item->easing_function = val_b;
				}
				const char *cv_a = obs_data_get_string(
					settings_a, S_TRANSITION_MATCH);
				const char *cv_b = obs_data_get_string(
					settings_b, S_TRANSITION_MATCH);
				if (cv_a && strlen(cv_a)) {
					item->transition_name = bstrdup(cv_a);
				} else if (cv_b && strlen(cv_b)) {
					item->transition_name = bstrdup(cv_b);
				}
				val_a = obs_data_get_int(settings_a,
							 S_TRANSITION_SCALE);
				val_b = obs_data_get_int(settings_b,
							 S_TRANSITION_SCALE);
				if (val_a != NO_OVERRIDE) {
					item->transition_scale = val_a;
				} else if (val_b != NO_OVERRIDE) {
					item->transition_scale = val_b;
				}
				if (obs_data_get_bool(settings_a,
						      S_CURVE_OVERRIDE_MATCH)) {
					item->curve = (float)obs_data_get_double(
						settings_a, S_CURVE_MATCH);
				} else if (obs_data_get_bool(
						   settings_b,
						   S_CURVE_OVERRIDE_MATCH)) {
					item->curve = (float)obs_data_get_double(
						settings_b, S_CURVE_MATCH);
				}
			} else if (settings_a) {
				long long val;
				if (item->item_a && item->item_b)
					val = obs_data_get_int(settings_a,
							       S_EASING_MATCH);
				else
					val = obs_data_get_int(settings_a,
							       S_EASING_OUT);
				if (val != NO_OVERRIDE) {
					item->easing = val;
				}
				if (item->item_a && item->item_b)
					val = obs_data_get_int(
						settings_a,
						S_EASING_FUNCTION_MATCH);
				else
					val = obs_data_get_int(
						settings_a,
						S_EASING_FUNCTION_OUT);
				if (val != NO_OVERRIDE) {
					item->easing_function = val;
				}
				val = obs_data_get_int(settings_a, S_ZOOM_OUT);
				if (val != NO_OVERRIDE) {
					item->zoom = !!val;
				}
				val = obs_data_get_int(settings_a,
						       S_POSITION_OUT);
				if (val != NO_OVERRIDE) {
					item->position = val;
				}
				val = obs_data_get_int(settings_a,
						       S_TRANSITION_SCALE);
				if (val != NO_OVERRIDE) {
					item->transition_scale = val;
				}
				const char *ti = obs_data_get_string(
					settings_a, S_TRANSITION_OUT);
				if (ti && strlen(ti) && item->item_a &&
				    !item->item_b) {
					item->transition_name = bstrdup(ti);
				}
				const char *tm = obs_data_get_string(
					settings_a, S_TRANSITION_MATCH);
				if (tm && strlen(tm) && item->item_a &&
				    item->item_b) {
					item->transition_name = bstrdup(tm);
				}
				if (item->item_a && item->item_b &&
				    obs_data_get_bool(settings_a,
						      S_CURVE_OVERRIDE_MATCH)) {
					item->curve = (float)obs_data_get_double(
						settings_a, S_CURVE_MATCH);
				} else if (item->item_a && !item->item_b &&
					   obs_data_get_bool(
						   settings_a,
						   S_CURVE_OVERRIDE_OUT)) {
					item->curve = (float)obs_data_get_double(
						settings_a, S_CURVE_OUT);
				}
			} else if (settings_b) {
				long long val;
				if (item->item_a && item->item_b)
					val = obs_data_get_int(settings_b,
							       S_EASING_MATCH);
				else
					val = obs_data_get_int(settings_b,
							       S_EASING_IN);
				if (val != NO_OVERRIDE) {
					item->easing = val;
				}
				if (item->item_a && item->item_b)
					val = obs_data_get_int(
						settings_b,
						S_EASING_FUNCTION_MATCH);
				else
					val = obs_data_get_int(
						settings_b,
						S_EASING_FUNCTION_IN);
				if (val != NO_OVERRIDE) {
					item->easing_function = val;
				}
				val = obs_data_get_int(settings_b, S_ZOOM_IN);
				if (val != NO_OVERRIDE) {
					item->zoom = !!val;
				}
				val = obs_data_get_int(settings_b,
						       S_POSITION_IN);
				if (val != NO_OVERRIDE) {
					item->position = val;
				}
				val = obs_data_get_int(settings_b,
						       S_TRANSITION_SCALE);
				if (val != NO_OVERRIDE) {
					item->transition_scale = val;
				}
				const char *to = obs_data_get_string(
					settings_b, S_TRANSITION_IN);
				if (to && strlen(to) && !item->item_a &&
				    item->item_b) {
					item->transition_name = bstrdup(to);
				}
				const char *tm = obs_data_get_string(
					settings_b, S_TRANSITION_MATCH);
				if (tm && strlen(tm) && item->item_a &&
				    item->item_b) {
					item->transition_name = bstrdup(tm);
				}
				if (item->item_a && item->item_b &&
				    obs_data_get_bool(settings_b,
						      S_CURVE_OVERRIDE_MATCH)) {
					item->curve = (float)obs_data_get_double(
						settings_b, S_CURVE_MATCH);
				} else if (!item->item_a && item->item_b &&
					   obs_data_get_bool(
						   settings_b,
						   S_CURVE_OVERRIDE_IN)) {
					item->curve =
						(float)obs_data_get_double(
							settings_b, S_CURVE_IN);
				}
			}
			obs_data_release(settings_a);
			obs_data_release(settings_b);
			if (!item->transition_name && !item->item_a &&
			    item->item_b && move->transition_in &&
			    strlen(move->transition_in))
				item->transition_name =
					bstrdup(move->transition_in);
			if (!item->transition_name && item->item_a &&
			    !item->item_b && move->transition_out &&
			    strlen(move->transition_out))
				item->transition_name =
					bstrdup(move->transition_out);
			if (!item->transition_name && item->item_a &&
			    item->item_b && move->transition_move &&
			    strlen(move->transition_move))
				item->transition_name =
					bstrdup(move->transition_move);
		}
	}

	if (move->t > 0.0f && move->t < 1.0f) {
		if (!move->scene_source_a)
			move->scene_source_a = obs_transition_get_source(
				move->source, OBS_TRANSITION_SOURCE_A);
		if (!move->scene_source_b)
			move->scene_source_b = obs_transition_get_source(
				move->source, OBS_TRANSITION_SOURCE_B);

		gs_matrix_push();
		gs_blend_state_push();
		gs_reset_blend_state();
		if (move->t <= 0.5f) {
			move->render_match = true;
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
			move->render_match = false;
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
		} else {
			move->render_match = true;
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
			move->render_match = false;
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_a),
				render2_item, data);
			obs_scene_enum_items(
				obs_scene_from_source(move->scene_source_b),
				render2_item, data);
		}

		gs_blend_state_pop();
		gs_matrix_pop();

	} else if (move->t <= 0.5f) {
		obs_transition_video_render_direct(move->source,
						   OBS_TRANSITION_SOURCE_A);
	} else {
		obs_transition_video_render_direct(move->source,
						   OBS_TRANSITION_SOURCE_B);
	}
	move->start_init = false;

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
	obs_property_list_add_int(p, obs_module_text("Position.Left"),
				  POS_SWIPE | POS_LEFT);
	obs_property_list_add_int(p, obs_module_text("Position.Top"),
				  POS_SWIPE | POS_TOP);
	obs_property_list_add_int(p, obs_module_text("Position.Right"),
				  POS_SWIPE | POS_RIGHT);
	obs_property_list_add_int(p, obs_module_text("Position.Bottom"),
				  POS_SWIPE | POS_BOTTOM);
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

void prop_list_add_transitions(obs_property_t *p)
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

void prop_list_add_scales(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("TransitionScale.MaxOnly"),
				  OBS_TRANSITION_SCALE_MAX_ONLY);
	obs_property_list_add_int(p, obs_module_text("TransitionScale.Aspect"),
				  OBS_TRANSITION_SCALE_ASPECT);
	obs_property_list_add_int(p, obs_module_text("TransitionScale.Stretch"),
				  OBS_TRANSITION_SCALE_STRETCH);
}

static obs_properties_t *move_properties(void *data)
{
	obs_property_t *p;
	obs_properties_t *ppts = obs_properties_create();
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_bool(group, S_NAME_PART_MATCH,
				obs_module_text("NamePartMatch"));
	obs_properties_add_bool(group, S_NAME_NUMBER_MATCH,
				obs_module_text("NameNumberMatch"));
	obs_properties_add_bool(group, S_NAME_LAST_WORD_MATCH,
				obs_module_text("NameLastWordMatch"));

	obs_properties_add_group(ppts, S_MATCH, obs_module_text("MatchName"),
				 OBS_GROUP_NORMAL, group);

	//Matched items
	group = obs_properties_create();
	p = obs_properties_add_list(group, S_EASING_MATCH,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_MATCH,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_list(group, S_TRANSITION_MATCH,
				    obs_module_text("Transition"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	p = obs_properties_add_list(group, S_TRANSITION_SCALE,
				    obs_module_text("ScaleType"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_scales(p);

	obs_properties_add_float_slider(group, S_CURVE_MATCH,
					obs_module_text("Curve"), -2.0, 2.0,
					0.01);

	obs_properties_add_group(ppts, S_MOVE_MATCH,
				 obs_module_text("MoveMatch"), OBS_GROUP_NORMAL,
				 group);

	//Move in
	group = obs_properties_create();

	p = obs_properties_add_list(group, S_EASING_IN,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_IN,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	obs_properties_add_bool(group, S_ZOOM_IN, obs_module_text("Zoom"));
	p = obs_properties_add_list(group, S_POSITION_IN,
				    obs_module_text("Position"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	p = obs_properties_add_list(group, S_TRANSITION_IN,
				    obs_module_text("Transition"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	obs_properties_add_float_slider(
		group, S_CURVE_IN, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_group(ppts, S_MOVE_IN, obs_module_text("MoveIn"),
				 OBS_GROUP_NORMAL, group);

	//Move out
	group = obs_properties_create();

	p = obs_properties_add_list(group, S_EASING_OUT,
				    obs_module_text("Easing"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_OUT,
				    obs_module_text("EasingFunction"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	obs_properties_add_bool(group, S_ZOOM_OUT, obs_module_text("Zoom"));
	p = obs_properties_add_list(group, S_POSITION_OUT,
				    obs_module_text("Position"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);

	p = obs_properties_add_list(group, S_TRANSITION_OUT,
				    obs_module_text("Transition"),
				    OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	prop_list_add_transitions(p);

	obs_properties_add_float_slider(
		group, S_CURVE_OUT, obs_module_text("Curve"), -2.0, 2.0, 0.01);

	obs_properties_add_group(ppts, S_MOVE_OUT, obs_module_text("MoveOut"),
				 OBS_GROUP_NORMAL, group);

	UNUSED_PARAMETER(data);
	return ppts;
}

void move_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_IN, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_OUT, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH,
				 EASING_CUBIC);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_IN, EASING_CUBIC);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_OUT, EASING_CUBIC);
	obs_data_set_default_int(settings, S_POSITION_IN, POS_EDGE | POS_LEFT);
	obs_data_set_default_bool(settings, S_ZOOM_IN, true);
	obs_data_set_default_int(settings, S_POSITION_OUT,
				 POS_EDGE | POS_RIGHT);
	obs_data_set_default_bool(settings, S_ZOOM_OUT, true);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
	obs_data_set_default_double(settings, S_CURVE_IN, 0.0);
	obs_data_set_default_double(settings, S_CURVE_OUT, 0.0);
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

struct obs_source_info move_transition_override_filter;

bool obs_module_load(void)
{
	obs_register_source(&move_transition);
	obs_register_source(&move_transition_override_filter);
	return true;
}
