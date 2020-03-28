#include <obs-module.h>
#include "../obs-transitions/easings.h"
#include "graphics/math-defs.h"
#include "graphics/matrix4.h"
#include "move-transition.h"

#define S_POSITION_IN "position_in"
#define S_POSITION_OUT "position_out"
#define S_ZOOM_IN "zoom_in"
#define S_ZOOM_OUT "zoom_out"
#define S_EASE_IN_OUT "ease_in_out"

#define POS_NONE 0
#define POS_CENTER (1 << 0)
#define POS_EDGE (1 << 1)
#define POS_LEFT (1 << 2)
#define POS_RIGHT (1 << 3)
#define POS_TOP (1 << 4)
#define POS_BOTTOM (1 << 4)

struct move_info {
	obs_source_t *source;
	bool start_init;
	DARRAY(struct move_item *) items;
	float t;
	obs_source_t *scene_source_a;
	obs_source_t *scene_source_b;
	gs_samplerstate_t *point_sampler;
	bool ease_in_out;
	bool zoom_in;
	bool zoom_out;
	long long position_in;
	long long position_out;
};

struct move_item {
	obs_sceneitem_t *item_a;
	obs_sceneitem_t *item_b;
	gs_texrender_t *item_render;
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
		bfree(item);
	}
	move->items.num = 0;
}

static void move_destroy(void *data)
{
	struct move_info *move = data;
	clear_items(move);
	da_free(move->items);
	if (move->scene_source_a)
		obs_source_release(move->scene_source_a);
	if (move->scene_source_b)
		obs_source_release(move->scene_source_b);
	bfree(move);
}

static void move_update(void *data, obs_data_t *settings)
{
	struct move_info *move = data;
	move->ease_in_out = obs_data_get_bool(settings, S_EASE_IN_OUT);
	move->position_in = obs_data_get_int(settings, S_POSITION_IN);
	move->zoom_in = obs_data_get_bool(settings, S_ZOOM_IN);
	move->position_out = obs_data_get_int(settings, S_POSITION_OUT);
	move->zoom_out = obs_data_get_bool(settings, S_ZOOM_OUT);
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

void calc_edge_position(struct vec2 *pos, long long position,
			uint32_t canvas_width, uint32_t canvas_height,
			uint32_t alignment, uint32_t cx, uint32_t cy, bool zoom)
{

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
		float factor_x = fabs(diff_x) / (canvas_width >> 1);
		float factor_y = fabs(diff_y) / (canvas_height >> 1);
		uint32_t cx2 = cx >> 1;
		uint32_t cy2 = cy >> 1;
		if (zoom) {
			cx2 = 0;
			cy2 = 0;
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
	vec2_set(pos, 0, 0);
	if (position & POS_RIGHT) {
		pos->x += canvas_width;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x += cx;
		} else if (alignment & OBS_ALIGN_LEFT) {

		} else {
			pos->x += cx >> 1;
		}
	} else if (position & POS_LEFT) {
		if (alignment & OBS_ALIGN_RIGHT) {

		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x -= cx;
		} else {
			pos->x -= cx >> 1;
		}
	} else {
		pos->x += canvas_width >> 1;
		if (alignment & OBS_ALIGN_RIGHT) {
			pos->x -= cx >> 1;
		} else if (alignment & OBS_ALIGN_LEFT) {
			pos->x += cx >> 1;
		}
	}

	if (position & POS_BOTTOM) {
		pos->y += canvas_height;
		if (alignment & OBS_ALIGN_TOP) {
		} else if (alignment & OBS_ALIGN_BOTTOM) {
			pos->y += cy;
		} else {
			pos->y += cy >> 1;
		}
	} else if (position & POS_TOP) {
		if (alignment & OBS_ALIGN_BOTTOM) {
		} else if (alignment & OBS_ALIGN_TOP) {
			pos->y -= cy;
		} else {
			pos->y -= cy >> 1;
		}
	} else {
		pos->y += canvas_height >> 1;
	}
}

bool render2_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
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
	bool move_out = item->item_a == scene_item;
	if (item->item_a && item->item_b) {
		if (item->item_a == scene_item && move->t > 0.5f)
			return true;
		if (item->item_b == scene_item && move->t <= 0.5f)
			return true;
	}
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);
	uint32_t original_width = width;
	uint32_t original_height = height;
	struct obs_sceneitem_crop crop;
	if (item->item_a && item->item_b) {
		struct obs_sceneitem_crop crop_a;
		obs_sceneitem_get_crop(item->item_a, &crop_a);
		struct obs_sceneitem_crop crop_b;
		obs_sceneitem_get_crop(item->item_b, &crop_b);
		crop.left =
			(1.0f - move->t) * crop_a.left + move->t * crop_b.left;
		crop.top = (1.0f - move->t) * crop_a.top + move->t * crop_b.top;
		crop.right = (1.0f - move->t) * crop_a.right +
			     move->t * crop_b.right;
		crop.bottom = (1.0f - move->t) * crop_a.bottom +
			      move->t * (float)crop_b.bottom;
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
			pos_add_center(&pos_a, alignment, cx, cy);
		} else if (move->position_in & POS_EDGE) {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
			calc_edge_position(&pos_a, move->position_in,
					   canvas_width, canvas_height,
					   alignment, original_cx, original_cy,
					   move->zoom_in);

		} else {
			obs_sceneitem_get_pos(item->item_b, &pos_a);
		}
	}
	struct vec2 pos_b;
	if (item->item_b) {
		obs_sceneitem_get_pos(item->item_b, &pos_b);
	} else {
		uint32_t alignment = obs_sceneitem_get_alignment(scene_item);
		if (move->position_out & POS_CENTER) {
			vec2_set(&pos_b, canvas_width >> 1, canvas_height >> 1);
			pos_add_center(&pos_b, alignment, cx, cy);
		} else if (move->position_out & POS_EDGE) {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
			calc_edge_position(&pos_b, move->position_out,
					   canvas_width, canvas_height,
					   alignment, original_cx, original_cy,
					   move->zoom_out);

		} else {
			obs_sceneitem_get_pos(item->item_a, &pos_b);
		}
	}
	struct vec2 pos;
	vec2_set(&pos, (1.0f - move->t) * pos_a.x + move->t * pos_b.x,
		 (1.0f - move->t) * pos_a.y + move->t * pos_b.y);

	matrix4_translate3f(&draw_transform, &draw_transform, pos.x, pos.y,
			    0.0f);

	struct vec2 output_scale = scale;

	if (item->item_render && !item_texture_enabled(scene_item)) {
		gs_texrender_destroy(item->item_render);
		item->item_render = NULL;
	} else if (!item->item_render && item_texture_enabled(scene_item)) {
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
			float cy_scale = (float)original_height / (float)width;
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)original_width, 0.0f,
				 (float)original_height, -100.0f, 100.0f);

			gs_matrix_scale3f(cx_scale, cy_scale, 1.0f);
			gs_matrix_translate3f(-(float)crop.left,
					      -(float)crop.top, 0.0f);

			obs_source_video_render(source);

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
		uint32_t cx = gs_texture_get_width(tex);
		uint32_t cy = gs_texture_get_height(tex);
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
		obs_source_video_render(obs_sceneitem_get_source(scene_item));
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

bool match_item(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	struct move_info *move = data;
	struct move_item *item = NULL;
	obs_source_t *source = obs_sceneitem_get_source(scene_item);
	if (scene == obs_scene_from_source(move->scene_source_a)) {
		item = bzalloc(sizeof(struct move_item));
		item->item_a = scene_item;
		obs_sceneitem_addref(item->item_a);
		da_push_back(move->items, &item);
	} else if (scene == obs_scene_from_source(move->scene_source_b)) {

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
			    obs_sceneitem_get_bounds_alignment(
				    check_item->item_a) !=
				    obs_sceneitem_get_bounds_alignment(
					    scene_item))
				continue;

			obs_source_t *check_source =
				obs_sceneitem_get_source(check_item->item_a);

			if (check_source == source) {
				item = check_item;
				break;
			}
			if (strcmp(obs_source_get_name(check_source),
				   obs_source_get_name(source)) == 0) {
				item = check_item;
				break;
			}
		}
		if (!item) {
			item = bzalloc(sizeof(struct move_item));
			da_push_back(move->items, &item);
		}
		item->item_b = scene_item;
		obs_sceneitem_addref(item->item_b);
	} else {
		int p = 0;
	}
	return true;
}

static void move_video_render(void *data, gs_effect_t *effect)
{
	struct move_info *move = data;

	float t = obs_transition_get_time(move->source);
	if (move->ease_in_out) {
		move->t = cubic_ease_in_out(t);
	} else {
		move->t = t;
	}

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

static obs_properties_t *move_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;
	obs_properties_add_bool(ppts, S_EASE_IN_OUT,
				obs_module_text("EaseInOut"));
	p = obs_properties_add_list(ppts, S_POSITION_IN,
				    obs_module_text("PositionIn"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);
	obs_properties_add_bool(ppts, S_ZOOM_IN, obs_module_text("ZoomIn"));

	p = obs_properties_add_list(ppts, S_POSITION_OUT,
				    obs_module_text("PositionOut"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_positions(p);
	obs_properties_add_bool(ppts, S_ZOOM_OUT, obs_module_text("ZoomOut"));

	UNUSED_PARAMETER(data);
	return ppts;
}

void move_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, S_EASE_IN_OUT, true);
	obs_data_set_default_int(settings, S_POSITION_IN, POS_EDGE | POS_LEFT);
	obs_data_set_default_bool(settings, S_ZOOM_IN, true);
	obs_data_set_default_int(settings, S_POSITION_OUT,
				 POS_EDGE | POS_RIGHT);
	obs_data_set_default_bool(settings, S_ZOOM_OUT, true);
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
