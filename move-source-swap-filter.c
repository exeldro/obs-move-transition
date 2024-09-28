#include "move-transition.h"

struct move_source_swap_info {
	struct move_filter move_filter;

	char *source_name1;
	obs_sceneitem_t *scene_item1;
	char *source_name2;
	obs_sceneitem_t *scene_item2;

	uint8_t swap_order;
	uint8_t swap_visibility;
	uint8_t swap_mute;
	bool swap_volume;
	bool stretch;
	float curve;

	struct vec2 pos1;
	struct vec2 pos2;
	float rot1;
	float rot2;
	struct vec2 scale1;
	struct vec2 scale2;
	struct vec2 bounds1;
	struct vec2 bounds2;
	struct obs_sceneitem_crop crop1;
	struct obs_sceneitem_crop crop2;
	float volume1;
	float volume2;
	uint32_t width1;
	uint32_t width2;
	uint32_t height1;
	uint32_t height2;

	uint32_t canvas_width;
	uint32_t canvas_height;

	bool midpoint;
};

#define SWAP_NONE 0
#define SWAP_START 1
#define SWAP_END 2
#define SWAP_MIDPOINT 3

static const char *move_source_swap_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveSourceSwapFilter");
}

static obs_source_t *move_source_swap_get_source(void *data, const char *name)
{
	struct move_source_swap_info *move_source_swap = data;
	obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
	if (source1) {
		obs_source_t *filter = obs_source_get_filter_by_name(source1, name);
		if (filter)
			return filter;
	}
	obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
	if (source2) {
		obs_source_t *filter = obs_source_get_filter_by_name(source2, name);
		if (filter)
			return filter;
	}
	return NULL;
}

static void move_source_swap_source_rename(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	const char *new_name = calldata_string(call_data, "new_name");
	const char *prev_name = calldata_string(call_data, "prev_name");
	obs_data_t *settings = obs_source_get_settings(move_source_swap->move_filter.source);
	if (!settings || !new_name || !prev_name)
		return;
	const char *source_name1 = obs_data_get_string(settings, "source1");
	if (source_name1 && strlen(source_name1) && strcmp(source_name1, prev_name) == 0) {
		obs_data_set_string(settings, "source1", new_name);
	}
	const char *source_name2 = obs_data_get_string(settings, "source2");
	if (source_name2 && strlen(source_name2) && strcmp(source_name2, prev_name) == 0) {
		obs_data_set_string(settings, "source2", new_name);
	}
	obs_data_release(settings);
}

static void move_source_swap_scene_remove(void *data, calldata_t *call_data);

static void move_source_swap_item_remove(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	if (!move_source_swap)
		return;
	if (!call_data)
		return;
	obs_sceneitem_t *item = calldata_ptr(call_data, "item");
	if (!item)
		return;
	if (item == move_source_swap->scene_item1) {
		move_source_swap->scene_item1 = NULL;
	} else if (item == move_source_swap->scene_item2) {
		move_source_swap->scene_item2 = NULL;
	} else {
		return;
	}
	obs_scene_t *scene = calldata_ptr(call_data, "scene");
	if (!scene)
		return;

	obs_source_t *parent = obs_scene_get_source(scene);
	if (!parent)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(parent);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "item_remove", move_source_swap_item_remove, move_source_swap);
	signal_handler_disconnect(sh, "remove", move_source_swap_scene_remove, move_source_swap);
	signal_handler_disconnect(sh, "destroy", move_source_swap_scene_remove, move_source_swap);
}

static void move_source_swap_scene_remove(void *data, calldata_t *call_data)
{
	struct move_source_info *move_source = data;
	obs_source_t *source = (obs_source_t *)calldata_ptr(call_data, "source");

	signal_handler_t *sh = obs_source_get_signal_handler(source);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "item_remove", move_source_swap_item_remove, move_source);
	signal_handler_disconnect(sh, "remove", move_source_swap_scene_remove, move_source);
	signal_handler_disconnect(sh, "destroy", move_source_swap_scene_remove, move_source);
}

static bool find_sceneitem(obs_scene_t *scene, obs_sceneitem_t *scene_item, void *data)
{
	struct move_source_swap_info *move_source_swap = data;
	const char *name = obs_source_get_name(obs_sceneitem_get_source(scene_item));
	if (!name)
		return true;
	if (!move_source_swap->scene_item1 && move_source_swap->source_name1 && strcmp(name, move_source_swap->source_name1) == 0) {
		move_source_swap->scene_item1 = scene_item;
	} else if (!move_source_swap->scene_item2 && move_source_swap->source_name2 &&
		   strcmp(name, move_source_swap->source_name2) == 0) {
		move_source_swap->scene_item2 = scene_item;
	} else {
		return true;
	}

	obs_source_t *parent = obs_scene_get_source(scene);
	if (!parent)
		return false;

	signal_handler_t *sh = obs_source_get_signal_handler(parent);
	if (sh) {
		signal_handler_disconnect(sh, "item_remove", move_source_swap_item_remove, move_source_swap);
		signal_handler_disconnect(sh, "remove", move_source_swap_scene_remove, move_source_swap);
		signal_handler_disconnect(sh, "destroy", move_source_swap_scene_remove, move_source_swap);
		signal_handler_connect(sh, "item_remove", move_source_swap_item_remove, move_source_swap);
		signal_handler_connect(sh, "remove", move_source_swap_scene_remove, move_source_swap);
		signal_handler_connect(sh, "destroy", move_source_swap_scene_remove, move_source_swap);
	}

	return !move_source_swap->scene_item1 || !move_source_swap->scene_item2;
}

static void move_source_swap_ended(struct move_source_swap_info *move_source_swap)
{
	move_filter_ended(&move_source_swap->move_filter);
	if (move_source_swap->swap_order == SWAP_END) {
		int order1 = obs_sceneitem_get_order_position(move_source_swap->scene_item1);
		int order2 = obs_sceneitem_get_order_position(move_source_swap->scene_item2);
		if (order1 < order2) {
			obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
			obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
		} else {
			obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
			obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
		}
	}
	if (move_source_swap->swap_visibility == SWAP_END) {
		bool vis1 = obs_sceneitem_visible(move_source_swap->scene_item1);
		bool vis2 = obs_sceneitem_visible(move_source_swap->scene_item2);
		if (vis1 != vis2) {
			obs_sceneitem_set_visible(move_source_swap->scene_item1, vis2);
			obs_sceneitem_set_visible(move_source_swap->scene_item2, vis1);
		}
	}
	if (move_source_swap->swap_mute == SWAP_END) {
		obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
		obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
		bool muted1 = obs_source_muted(source1);
		bool muted2 = obs_source_muted(source2);
		if (muted1 != muted2) {
			obs_source_set_muted(source1, muted2);
			obs_source_set_muted(source2, muted1);
		}
	}
}

static void move_source_swap_start(struct move_source_swap_info *move_source_swap)
{
	if ((!move_source_swap->scene_item1 && move_source_swap->source_name1 && strlen(move_source_swap->source_name1)) ||
	    (!move_source_swap->scene_item2 && move_source_swap->source_name2 && strlen(move_source_swap->source_name2))) {
		obs_source_t *parent = obs_filter_get_parent(move_source_swap->move_filter.source);
		if (parent) {
			obs_scene_t *scene = obs_scene_from_source(parent);
			if (!scene)
				scene = obs_group_from_source(parent);
			if (scene)
				obs_scene_enum_items(scene, find_sceneitem, move_source_swap);
		}
	}
	if (!move_source_swap->scene_item1 || !move_source_swap->scene_item2) {
		move_source_swap->move_filter.moving = false;
		move_source_swap_ended(move_source_swap);
		return;
	}
	if (!move_filter_start_internal(&move_source_swap->move_filter))
		return;
	if (move_source_swap->swap_order == SWAP_START) {
		int order1 = obs_sceneitem_get_order_position(move_source_swap->scene_item1);
		int order2 = obs_sceneitem_get_order_position(move_source_swap->scene_item2);
		if (order1 < order2) {
			obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
			obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
		} else {
			obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
			obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
		}
	}
	if (move_source_swap->swap_visibility == SWAP_START) {
		bool vis1 = obs_sceneitem_visible(move_source_swap->scene_item1);
		bool vis2 = obs_sceneitem_visible(move_source_swap->scene_item2);
		if (vis1 != vis2) {
			obs_sceneitem_set_visible(move_source_swap->scene_item1, vis2);
			obs_sceneitem_set_visible(move_source_swap->scene_item2, vis1);
		}
	}
	if (move_source_swap->swap_mute == SWAP_START) {
		obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
		obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
		bool muted1 = obs_source_muted(source1);
		bool muted2 = obs_source_muted(source2);
		if (muted1 != muted2) {
			obs_source_set_muted(source1, muted2);
			obs_source_set_muted(source2, muted1);
		}
	}

	obs_source_t *scene_source = obs_scene_get_source(obs_sceneitem_get_scene(move_source_swap->scene_item1));
	move_source_swap->canvas_width = obs_source_get_width(scene_source);
	move_source_swap->canvas_height = obs_source_get_height(scene_source);

	move_source_swap->rot1 = obs_sceneitem_get_rot(move_source_swap->scene_item1);
	move_source_swap->rot2 = obs_sceneitem_get_rot(move_source_swap->scene_item2);
	obs_sceneitem_get_pos(move_source_swap->scene_item1, &move_source_swap->pos1);
	obs_sceneitem_get_pos(move_source_swap->scene_item2, &move_source_swap->pos2);
	obs_sceneitem_get_scale(move_source_swap->scene_item1, &move_source_swap->scale1);
	obs_sceneitem_get_scale(move_source_swap->scene_item2, &move_source_swap->scale2);
	obs_sceneitem_get_bounds(move_source_swap->scene_item1, &move_source_swap->bounds1);
	obs_sceneitem_get_bounds(move_source_swap->scene_item2, &move_source_swap->bounds2);
	obs_sceneitem_get_crop(move_source_swap->scene_item1, &move_source_swap->crop1);
	obs_sceneitem_get_crop(move_source_swap->scene_item2, &move_source_swap->crop2);
	obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
	obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
	move_source_swap->volume1 = obs_source_get_volume(source1);
	move_source_swap->volume2 = obs_source_get_volume(source2);
	move_source_swap->width1 = obs_source_get_width(source1);
	move_source_swap->width2 = obs_source_get_width(source2);
	if (!move_source_swap->width1)
		move_source_swap->width1 = 1;
	if (!move_source_swap->width2)
		move_source_swap->width2 = 1;
	move_source_swap->height1 = obs_source_get_height(source1);
	move_source_swap->height2 = obs_source_get_height(source2);
	if (!move_source_swap->height1)
		move_source_swap->height1 = 1;
	if (!move_source_swap->height2)
		move_source_swap->height2 = 1;

	move_source_swap->midpoint = false;
}

static void move_source_swap_stop(struct move_source_swap_info *move_source_swap)
{
	move_filter_stop(&move_source_swap->move_filter);
}


static void move_source_swap_source_activate(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	if (move_source_swap->move_filter.start_trigger == START_TRIGGER_SOURCE_ACTIVATE)
		move_source_swap_start(move_source_swap);
	if (move_source_swap->move_filter.stop_trigger == START_TRIGGER_SOURCE_ACTIVATE)
		move_source_swap_stop(move_source_swap);
	UNUSED_PARAMETER(call_data);
}

static void move_source_swap_source_deactivate(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	if (move_source_swap->move_filter.start_trigger == START_TRIGGER_SOURCE_DEACTIVATE)
		move_source_swap_start(move_source_swap);
	if (move_source_swap->move_filter.stop_trigger == START_TRIGGER_SOURCE_DEACTIVATE)
		move_source_swap_stop(move_source_swap);
	UNUSED_PARAMETER(call_data);
}

static void move_source_swap_source_show(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	if (move_source_swap->move_filter.start_trigger == START_TRIGGER_SOURCE_SHOW)
		move_source_swap_start(move_source_swap);
	if (move_source_swap->move_filter.stop_trigger == START_TRIGGER_SOURCE_SHOW)
		move_source_swap_stop(move_source_swap);
	UNUSED_PARAMETER(call_data);
}

static void move_source_swap_source_hide(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	if (move_source_swap->move_filter.start_trigger == START_TRIGGER_SOURCE_HIDE)
		move_source_swap_start(move_source_swap);
	if (move_source_swap->move_filter.stop_trigger == START_TRIGGER_SOURCE_HIDE)
		move_source_swap_stop(move_source_swap);
	UNUSED_PARAMETER(call_data);
}

static void *move_source_swap_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_source_swap_info *move_source_swap = bzalloc(sizeof(struct move_source_swap_info));
	move_filter_init(&move_source_swap->move_filter, source, (void (*)(void *))move_source_swap_start);
	move_source_swap->move_filter.get_alternative_filter = move_source_swap_get_source;
	obs_source_update(source, settings);
	signal_handler_connect(obs_get_signal_handler(), "source_rename", move_source_swap_source_rename, move_source_swap);

	return move_source_swap;
}

static void move_source_swap_source1_remove(void *data, calldata_t *call_data);
static void move_source_swap_source2_remove(void *data, calldata_t *call_data);

static void move_source_swap_source_disconnect_signals(obs_source_t *source, void *data)
{
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	if (sh) {
		signal_handler_disconnect(sh, "activate", move_source_swap_source_activate, data);
		signal_handler_disconnect(sh, "deactivate", move_source_swap_source_deactivate, data);
		signal_handler_disconnect(sh, "show", move_source_swap_source_show, data);
		signal_handler_disconnect(sh, "hide", move_source_swap_source_hide, data);
		signal_handler_disconnect(sh, "remove", move_source_swap_source1_remove, data);
		signal_handler_disconnect(sh, "remove", move_source_swap_source2_remove, data);
	}
}

static void move_source_swap_source1_remove(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	obs_source_t *source = (obs_source_t *)calldata_ptr(call_data, "source");
	move_source_swap_source_disconnect_signals(source, data);
	move_source_swap->scene_item1 = NULL;
}

static void move_source_swap_source2_remove(void *data, calldata_t *call_data)
{
	struct move_source_swap_info *move_source_swap = data;
	obs_source_t *source = (obs_source_t *)calldata_ptr(call_data, "source");
	move_source_swap_source_disconnect_signals(source, data);
	move_source_swap->scene_item2 = NULL;
}

static void move_source_swap_destroy(void *data)
{
	struct move_source_swap_info *move_source_swap = data;
	signal_handler_disconnect(obs_get_signal_handler(), "source_rename", move_source_swap_source_rename, move_source_swap);

	obs_source_t *parent = obs_filter_get_parent(move_source_swap->move_filter.source);
	if (parent) {
		signal_handler_t *sh = obs_source_get_signal_handler(parent);
		signal_handler_disconnect(sh, "item_remove", move_source_swap_item_remove, move_source_swap);
		signal_handler_disconnect(sh, "remove", move_source_swap_scene_remove, move_source_swap);
		signal_handler_disconnect(sh, "destroy", move_source_swap_scene_remove, move_source_swap);
	}

	obs_source_t *source1 = NULL;
	obs_source_t *source2 = NULL;
	if (move_source_swap->scene_item1)
		source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
	if (move_source_swap->scene_item2)
		source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
	
	if (!source1 && move_source_swap->source_name1 && strlen(move_source_swap->source_name1)) {
		source1 = obs_get_source_by_name(move_source_swap->source_name1);
		obs_source_release(source1);
	}
	if (!source2 && move_source_swap->source_name2 && strlen(move_source_swap->source_name2)) {
		source2 = obs_get_source_by_name(move_source_swap->source_name2);
		obs_source_release(source2);
	}
	if (source1)
		move_source_swap_source_disconnect_signals(source1, data);
	if (source2)
		move_source_swap_source_disconnect_signals(source2, data);
	
	move_source_swap->scene_item1 = NULL;
	move_source_swap->scene_item2 = NULL;
	move_filter_destroy(&move_source_swap->move_filter);
	bfree(move_source_swap->source_name1);
	bfree(move_source_swap->source_name2);
	bfree(move_source_swap);
}

static bool move_source_swap_start_button(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct move_source_swap_info *move_source_swap = data;
	move_source_swap_start(move_source_swap);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

static void move_source_swap_source_changed(struct move_source_swap_info *move_source_swap, char **old_source_name,
					    const char *new_source_name, obs_sceneitem_t **scene_item)
{

	obs_source_t *source = *old_source_name && strlen(*old_source_name) ? obs_get_source_by_name(*old_source_name) : NULL;
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "activate", move_source_swap_source_activate, move_source_swap);
			signal_handler_disconnect(sh, "deactivate", move_source_swap_source_deactivate, move_source_swap);
			signal_handler_disconnect(sh, "show", move_source_swap_source_show, move_source_swap);
			signal_handler_disconnect(sh, "hide", move_source_swap_source_hide, move_source_swap);
			signal_handler_disconnect(sh, "remove", move_source_swap_source1_remove, move_source_swap);
			signal_handler_disconnect(sh, "remove", move_source_swap_source2_remove, move_source_swap);
		}
		obs_source_release(source);
	}

	bfree(*old_source_name);
	*old_source_name = NULL;

	source = obs_get_source_by_name(new_source_name);
	if (source) {
		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "activate", move_source_swap_source_activate, move_source_swap);
			signal_handler_disconnect(sh, "deactivate", move_source_swap_source_deactivate, move_source_swap);
			signal_handler_disconnect(sh, "show", move_source_swap_source_show, move_source_swap);
			signal_handler_disconnect(sh, "hide", move_source_swap_source_hide, move_source_swap);
			signal_handler_disconnect(sh, "remove", move_source_swap_source1_remove, move_source_swap);
			signal_handler_disconnect(sh, "remove", move_source_swap_source2_remove, move_source_swap);
			signal_handler_connect(sh, "activate", move_source_swap_source_activate, move_source_swap);
			signal_handler_connect(sh, "deactivate", move_source_swap_source_deactivate, move_source_swap);
			signal_handler_connect(sh, "show", move_source_swap_source_show, move_source_swap);
			signal_handler_connect(sh, "hide", move_source_swap_source_hide, move_source_swap);
			signal_handler_connect(sh, "remove", move_source_swap_source1_remove, move_source_swap);
			signal_handler_connect(sh, "remove", move_source_swap_source2_remove, move_source_swap);
			*old_source_name = bstrdup(new_source_name);
		}
		obs_source_release(source);
	}
	*scene_item = NULL;
	obs_source_t *parent = obs_filter_get_parent(move_source_swap->move_filter.source);
	if (parent) {
		signal_handler_t *sh = obs_source_get_signal_handler(parent);
		if (sh) {
			signal_handler_disconnect(sh, "item_remove", move_source_swap_item_remove, move_source_swap);
			signal_handler_disconnect(sh, "remove", move_source_swap_scene_remove, move_source_swap);
			signal_handler_disconnect(sh, "destroy", move_source_swap_scene_remove, move_source_swap);
		}
	}
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene)
		scene = obs_group_from_source(parent);
	if (*old_source_name && scene)
		obs_scene_enum_items(scene, find_sceneitem, move_source_swap);
}

static bool move_source_swap_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	struct move_source_swap_info *move_source_swap = data;

	const char *source_name1 = obs_data_get_string(settings, "source1");
	const char *source_name2 = obs_data_get_string(settings, "source2");
	if (move_source_swap->source_name1 && strcmp(move_source_swap->source_name1, source_name1) != 0) {
		move_source_swap_source_changed(move_source_swap, &move_source_swap->source_name1, source_name1,
						&move_source_swap->scene_item1);
	} else if (move_source_swap->source_name2 && strcmp(move_source_swap->source_name2, source_name2) != 0) {
		move_source_swap_source_changed(move_source_swap, &move_source_swap->source_name2, source_name2,
						&move_source_swap->scene_item2);
	} else {
		return false;
	}

	obs_source_t *parent = obs_filter_get_parent(move_source_swap->move_filter.source);
	obs_property_t *p = obs_properties_get(props, S_SIMULTANEOUS_MOVE);
	if (p) {
		obs_property_list_clear(p);
		obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
		obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
		obs_source_t *source = obs_sceneitem_get_source(move_source_swap->scene_item1);
		if (source)
			obs_source_enum_filters(source, prop_list_add_move_source_filter, p);
	}
	p = obs_properties_get(props, S_NEXT_MOVE);
	if (p) {
		obs_property_list_clear(p);
		obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
		obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
		obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
		obs_source_t *source = obs_sceneitem_get_source(move_source_swap->scene_item1);
		if (source)
			obs_source_enum_filters(source, prop_list_add_move_source_filter, p);
		source = obs_sceneitem_get_source(move_source_swap->scene_item2);
		if (source)
			obs_source_enum_filters(source, prop_list_add_move_source_filter, p);
	}

	obs_source_t *source1 = obs_get_source_by_name(source_name1);
	obs_source_t *source2 = obs_get_source_by_name(source_name2);
	if (source1 && source2) {
		uint32_t flags1 = obs_source_get_output_flags(source1);
		uint32_t flags2 = obs_source_get_output_flags(source2);
		p = obs_properties_get(props, S_AUDIO_ACTION);
		const bool audio = flags1 & flags2 & OBS_SOURCE_AUDIO;
		obs_property_set_visible(p, audio);
	} else {
		p = obs_properties_get(props, S_AUDIO_ACTION);
		obs_property_set_visible(p, false);
	}
	obs_source_release(source1);
	obs_source_release(source2);
	return true;
}

bool prop_list_add_sceneitem(obs_scene_t *scene, obs_sceneitem_t *item, void *data);

static obs_properties_t *move_source_swap_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	struct move_source_swap_info *move_source_swap = data;
	obs_source_t *parent = obs_filter_get_parent(move_source_swap->move_filter.source);
	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene)
		scene = obs_group_from_source(parent);
	if (!scene) {
		obs_property_t *w = obs_properties_add_text(ppts, "warning", obs_module_text("ScenesOnlyFilter"), OBS_TEXT_INFO);
		obs_property_text_set_info_type(w, OBS_TEXT_INFO_WARNING);
		obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
		return ppts;
	}
	if ((!move_source_swap->scene_item1 && move_source_swap->source_name1 && strlen(move_source_swap->source_name1)) ||
	    (!move_source_swap->scene_item2 && move_source_swap->source_name2 && strlen(move_source_swap->source_name2))) {
		obs_scene_enum_items(scene, find_sceneitem, move_source_swap);
	}
	obs_properties_t *group = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(group, "source1", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_scene_enum_items(scene, prop_list_add_sceneitem, p);
	obs_property_set_modified_callback2(p, move_source_swap_changed, data);

	p = obs_properties_add_list(group, "source2", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_scene_enum_items(scene, prop_list_add_sceneitem, p);
	obs_property_set_modified_callback2(p, move_source_swap_changed, data);

	p = obs_properties_add_int(group, S_START_DELAY, obs_module_text("StartDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");
	obs_properties_t *duration = obs_properties_create();
	p = obs_properties_add_int(duration, S_DURATION, "", 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");
	p = obs_properties_add_group(group, S_CUSTOM_DURATION, obs_module_text("CustomDuration"), OBS_GROUP_CHECKABLE, duration);

	p = obs_properties_add_int(group, S_END_DELAY, obs_module_text("EndDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(group, S_EASING_MATCH, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(group, S_EASING_FUNCTION_MATCH, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_group(ppts, S_GENERAL, obs_module_text("General"), OBS_GROUP_NORMAL, group);

	group = obs_properties_create();
	obs_properties_add_bool(group, "stretch", obs_module_text("Stretch"));
	obs_properties_add_float_slider(group, S_CURVE_MATCH, obs_module_text("Curve"), -2.0, 2.0, 0.01);
	p = obs_properties_add_group(ppts, S_TRANSFORM, obs_module_text("Transform"), OBS_GROUP_NORMAL, group);

	group = obs_properties_create();

	p = obs_properties_add_list(group, S_CHANGE_VISIBILITY, obs_module_text("ChangeVisibility"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Swap.No"), SWAP_NONE);
	obs_property_list_add_int(p, obs_module_text("Swap.Start"), SWAP_START);
	obs_property_list_add_int(p, obs_module_text("Swap.End"), SWAP_END);
	obs_property_list_add_int(p, obs_module_text("Swap.Midpoint"), SWAP_MIDPOINT);

	p = obs_properties_add_list(group, S_CHANGE_ORDER, obs_module_text("ChangeOrder"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Swap.No"), SWAP_NONE);
	obs_property_list_add_int(p, obs_module_text("Swap.Start"), SWAP_START);
	obs_property_list_add_int(p, obs_module_text("Swap.End"), SWAP_END);
	obs_property_list_add_int(p, obs_module_text("Swap.Midpoint"), SWAP_MIDPOINT);

	p = obs_properties_add_group(ppts, S_VISIBILITY_ORDER, obs_module_text("VisibilityOrder"), OBS_GROUP_NORMAL, group);

	obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
	obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);

	const uint32_t flags1 = source1 ? obs_source_get_output_flags(source1) : 0;
	const uint32_t flags2 = source2 ? obs_source_get_output_flags(source2) : 0;
	const bool audio = (flags1 & OBS_SOURCE_AUDIO) && (flags2 & OBS_SOURCE_AUDIO);

	group = obs_properties_create();

	p = obs_properties_add_list(group, S_MUTE_ACTION, obs_module_text("MuteAction"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Swap.No"), SWAP_NONE);
	obs_property_list_add_int(p, obs_module_text("Swap.Start"), SWAP_START);
	obs_property_list_add_int(p, obs_module_text("Swap.End"), SWAP_END);
	obs_property_list_add_int(p, obs_module_text("Swap.Midpoint"), SWAP_MIDPOINT);

	p = obs_properties_add_bool(group, S_AUDIO_FADE, obs_module_text("AudioFade"));
	p = obs_properties_add_group(ppts, S_AUDIO_ACTION, obs_module_text("AudioAction"), OBS_GROUP_NORMAL, group);
	obs_property_set_visible(p, audio);

	group = obs_properties_create();

	p = obs_properties_add_bool(group, S_ENABLED_MATCH_MOVING, obs_module_text("EnabledMatchMoving"));

	p = obs_properties_add_list(group, S_START_TRIGGER, obs_module_text("StartTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceActivate"), START_TRIGGER_SOURCE_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceDeactivate"), START_TRIGGER_SOURCE_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceShow"), START_TRIGGER_SOURCE_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceHide"), START_TRIGGER_SOURCE_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaStarted"), START_TRIGGER_MEDIA_STARTED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaEnded"), START_TRIGGER_MEDIA_ENDED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Load"), START_TRIGGER_LOAD);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);

	obs_property_set_modified_callback2(p, move_filter_start_trigger_changed, data);

	obs_properties_add_int(ppts, S_START_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_START_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(group, S_STOP_TRIGGER, obs_module_text("StopTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StopTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceActivate"), START_TRIGGER_SOURCE_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceDeactivate"), START_TRIGGER_SOURCE_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceShow"), START_TRIGGER_SOURCE_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.SourceHide"), START_TRIGGER_SOURCE_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaStarted"), START_TRIGGER_MEDIA_STARTED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MediaEnded"), START_TRIGGER_MEDIA_ENDED);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);
	obs_property_set_modified_callback2(p, move_filter_stop_trigger_changed, data);

	obs_properties_add_int(ppts, S_STOP_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_STOP_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(group, S_SIMULTANEOUS_MOVE, obs_module_text("SimultaneousMove"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
	obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
	if (source1)
		obs_source_enum_filters(source1, prop_list_add_move_source_filter, p);
	if (source2)
		obs_source_enum_filters(source2, prop_list_add_move_source_filter, p);

	p = obs_properties_add_list(group, S_NEXT_MOVE, obs_module_text("NextMove"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
	obs_source_enum_filters(parent, prop_list_add_move_source_filter, p);
	if (source1)
		obs_source_enum_filters(source1, prop_list_add_move_source_filter, p);
	if (source2)
		obs_source_enum_filters(source2, prop_list_add_move_source_filter, p);

	p = obs_properties_add_list(group, S_NEXT_MOVE_ON, obs_module_text("NextMoveOn"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"), NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"), NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(group, "move_source_start", obs_module_text("Start"), move_source_swap_start_button);

	p = obs_properties_add_group(ppts, S_ACTIONS, obs_module_text("Actions"), OBS_GROUP_NORMAL, group);
	obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
	return ppts;
}

static void move_source_swap_defaults(obs_data_t *settings)
{
	move_filter_defaults(settings);
	obs_data_set_default_bool(settings, S_CUSTOM_DURATION, true);
	obs_data_set_default_int(settings, S_EASING_MATCH, EASE_IN_OUT);
	obs_data_set_default_int(settings, S_EASING_FUNCTION_MATCH, EASING_CUBIC);
	obs_data_set_default_double(settings, S_CURVE_MATCH, 0.0);
}

static void move_source_swap_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct move_source_swap_info *filter = data;
	obs_source_skip_video_filter(filter->move_filter.source);
}

void vec2_bezier(struct vec2 *dst, struct vec2 *begin, struct vec2 *control, struct vec2 *end, const float t);

static void move_source_swap_tick(void *data, float seconds)
{
	struct move_source_swap_info *move_source_swap = data;
	float t;
	if (!move_filter_tick(&move_source_swap->move_filter, seconds, &t))
		return;

	if (!move_source_swap->scene_item1 || !move_source_swap->scene_item2) {
		move_source_swap->move_filter.moving = false;
		move_source_swap_ended(move_source_swap);
		return;
	}

	if (!move_source_swap->midpoint && t >= 0.5) {
		move_source_swap->midpoint = true;
		if (move_source_swap->swap_order == SWAP_MIDPOINT) {
			int order1 = obs_sceneitem_get_order_position(move_source_swap->scene_item1);
			int order2 = obs_sceneitem_get_order_position(move_source_swap->scene_item2);
			if (order1 < order2) {
				obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
				obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
			} else {
				obs_sceneitem_set_order_position(move_source_swap->scene_item1, order2);
				obs_sceneitem_set_order_position(move_source_swap->scene_item2, order1);
			}
		}
		if (move_source_swap->swap_visibility == SWAP_MIDPOINT) {
			bool vis1 = obs_sceneitem_visible(move_source_swap->scene_item1);
			bool vis2 = obs_sceneitem_visible(move_source_swap->scene_item2);
			if (vis1 != vis2) {
				obs_sceneitem_set_visible(move_source_swap->scene_item1, vis2);
				obs_sceneitem_set_visible(move_source_swap->scene_item2, vis1);
			}
		}
		if (move_source_swap->swap_mute == SWAP_MIDPOINT) {
			obs_source_t *source1 = obs_sceneitem_get_source(move_source_swap->scene_item1);
			obs_source_t *source2 = obs_sceneitem_get_source(move_source_swap->scene_item2);
			bool muted1 = obs_source_muted(source1);
			bool muted2 = obs_source_muted(source2);
			if (muted1 != muted2) {
				obs_source_set_muted(source1, muted2);
				obs_source_set_muted(source2, muted1);
			}
		}
	}

	float ot = t;
	if (t > 1.0f)
		ot = 1.0f;
	else if (t < 0.0f)
		ot = 0.0f;

	if (move_source_swap->swap_volume && move_source_swap->volume1 != move_source_swap->volume2) {
		obs_source_set_volume(obs_sceneitem_get_source(move_source_swap->scene_item1),
				      (1.0f - ot) * move_source_swap->volume1 + ot * move_source_swap->volume2);
		obs_source_set_volume(obs_sceneitem_get_source(move_source_swap->scene_item2),
				      (1.0f - ot) * move_source_swap->volume2 + ot * move_source_swap->volume1);
	}

	struct vec2 pos1;
	struct vec2 pos2;
	if (move_source_swap->curve != 0.0f) {
		const float diff_x = fabsf(move_source_swap->pos1.x - move_source_swap->pos2.x);
		const float diff_y = fabsf(move_source_swap->pos1.y - move_source_swap->pos2.y);
		struct vec2 control_pos;
		vec2_set(&control_pos, 0.5f * move_source_swap->pos1.x + 0.5f * move_source_swap->pos2.x,
			 0.5f * move_source_swap->pos1.y + 0.5f * move_source_swap->pos2.y);

		if (control_pos.x >= (move_source_swap->canvas_width >> 1)) {
			control_pos.x += diff_y * move_source_swap->curve;
		} else {
			control_pos.x -= diff_y * move_source_swap->curve;
		}
		if (control_pos.y >= (move_source_swap->canvas_height >> 1)) {
			control_pos.y += diff_x * move_source_swap->curve;
		} else {
			control_pos.y -= diff_x * move_source_swap->curve;
		}
		vec2_bezier(&pos1, &move_source_swap->pos1, &control_pos, &move_source_swap->pos2, t);
		vec2_bezier(&pos2, &move_source_swap->pos2, &control_pos, &move_source_swap->pos1, t);
	} else {
		vec2_set(&pos1, (1.0f - t) * move_source_swap->pos1.x + t * move_source_swap->pos2.x,
			 (1.0f - t) * move_source_swap->pos1.y + t * move_source_swap->pos2.y);
		vec2_set(&pos2, (1.0f - t) * move_source_swap->pos2.x + t * move_source_swap->pos1.x,
			 (1.0f - t) * move_source_swap->pos2.y + t * move_source_swap->pos1.y);
	}
	obs_sceneitem_defer_update_begin(move_source_swap->scene_item1);
	obs_sceneitem_defer_update_begin(move_source_swap->scene_item2);
	obs_sceneitem_set_pos(move_source_swap->scene_item1, &pos1);
	obs_sceneitem_set_pos(move_source_swap->scene_item2, &pos2);
	const float rot1 = (1.0f - t) * move_source_swap->rot1 + t * move_source_swap->rot2;
	const float rot2 = (1.0f - t) * move_source_swap->rot2 + t * move_source_swap->rot1;
	obs_sceneitem_set_rot(move_source_swap->scene_item1, rot1);
	obs_sceneitem_set_rot(move_source_swap->scene_item2, rot2);
	if (move_source_swap->stretch) {
		struct vec2 scale1;
		struct vec2 scale2;
		vec2_set(&scale1,
			 ((1.0f - t) * move_source_swap->scale1.x * move_source_swap->width1 +
			  t * move_source_swap->scale2.x * move_source_swap->width2) /
				 (float)move_source_swap->width1,
			 ((1.0f - t) * move_source_swap->scale1.y * move_source_swap->height1 +
			  t * move_source_swap->scale2.y * move_source_swap->height2) /
				 (float)move_source_swap->height1);
		vec2_set(&scale2,
			 ((1.0f - t) * move_source_swap->scale2.x * move_source_swap->width2 +
			  t * move_source_swap->scale1.x * move_source_swap->width1) /
				 (float)move_source_swap->width2,
			 ((1.0f - t) * move_source_swap->scale2.y * move_source_swap->height2 +
			  t * move_source_swap->scale1.y * move_source_swap->height1) /
				 (float)move_source_swap->height2);
		obs_sceneitem_set_scale(move_source_swap->scene_item1, &scale1);
		obs_sceneitem_set_scale(move_source_swap->scene_item2, &scale2);

		struct vec2 bounds1;
		struct vec2 bounds2;
		vec2_set(&bounds1, (1.0f - t) * move_source_swap->bounds1.x + t * move_source_swap->bounds2.x,
			 (1.0f - t) * move_source_swap->bounds1.y + t * move_source_swap->bounds2.y);
		vec2_set(&bounds2, (1.0f - t) * move_source_swap->bounds2.x + t * move_source_swap->bounds1.x,
			 (1.0f - t) * move_source_swap->bounds2.y + t * move_source_swap->bounds1.y);
		obs_sceneitem_set_bounds(move_source_swap->scene_item2, &bounds2);
	}
	struct obs_sceneitem_crop crop1;
	struct obs_sceneitem_crop crop2;
	crop1.left = (int)((float)(1.0f - ot) * (float)move_source_swap->crop1.left + ot * (float)move_source_swap->crop2.left);
	crop2.left = (int)((float)(1.0f - ot) * (float)move_source_swap->crop2.left + ot * (float)move_source_swap->crop1.left);
	crop1.top = (int)((float)(1.0f - ot) * (float)move_source_swap->crop1.top + ot * (float)move_source_swap->crop2.top);
	crop2.top = (int)((float)(1.0f - ot) * (float)move_source_swap->crop2.top + ot * (float)move_source_swap->crop1.top);
	crop1.right = (int)((float)(1.0f - ot) * (float)move_source_swap->crop1.right + ot * (float)move_source_swap->crop2.right);
	crop2.right = (int)((float)(1.0f - ot) * (float)move_source_swap->crop2.right + ot * (float)move_source_swap->crop1.right);
	crop1.bottom =
		(int)((float)(1.0f - ot) * (float)move_source_swap->crop1.bottom + ot * (float)move_source_swap->crop2.bottom);
	crop2.bottom =
		(int)((float)(1.0f - ot) * (float)move_source_swap->crop2.bottom + ot * (float)move_source_swap->crop1.bottom);
	obs_sceneitem_set_crop(move_source_swap->scene_item1, &crop1);
	obs_sceneitem_set_crop(move_source_swap->scene_item2, &crop2);
	obs_sceneitem_defer_update_end(move_source_swap->scene_item1);
	obs_sceneitem_defer_update_end(move_source_swap->scene_item2);

	if (!move_source_swap->move_filter.moving) {
		move_source_swap_ended(move_source_swap);
	}
}

static void move_source_swap_update(void *data, obs_data_t *settings)
{
	struct move_source_swap_info *move_source_swap = data;

	const char *source_name1 = obs_data_get_string(settings, "source1");

	if (!move_source_swap->source_name1 || strcmp(move_source_swap->source_name1, source_name1) != 0) {
		move_source_swap_source_changed(move_source_swap, &move_source_swap->source_name1, source_name1,
						&move_source_swap->scene_item1);
	}
	const char *source_name2 = obs_data_get_string(settings, "source2");
	if (!move_source_swap->source_name2 || strcmp(move_source_swap->source_name2, source_name2) != 0) {
		move_source_swap_source_changed(move_source_swap, &move_source_swap->source_name2, source_name2,
						&move_source_swap->scene_item2);
	}
	move_filter_update(&move_source_swap->move_filter, settings);

	move_source_swap->swap_visibility = (uint8_t)obs_data_get_int(settings, S_CHANGE_VISIBILITY);
	move_source_swap->curve = (float)obs_data_get_double(settings, S_CURVE_MATCH);

	move_source_swap->swap_order = (uint8_t)obs_data_get_int(settings, S_CHANGE_ORDER);

	move_source_swap->swap_mute = (uint8_t)obs_data_get_int(settings, S_MUTE_ACTION);
	move_source_swap->swap_volume = obs_data_get_bool(settings, S_AUDIO_FADE);
	move_source_swap->stretch = obs_data_get_bool(settings, "stretch");
	if (move_source_swap->move_filter.start_trigger == START_TRIGGER_LOAD) {
		move_source_swap_start(move_source_swap);
	}
}

struct obs_source_info move_source_swap_filter = {
	.id = MOVE_SOURCE_SWAP_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = move_source_swap_get_name,
	.create = move_source_swap_create,
	.destroy = move_source_swap_destroy,
	.get_properties = move_source_swap_properties,
	.get_defaults = move_source_swap_defaults,
	.video_render = move_source_swap_video_render,
	.video_tick = move_source_swap_tick,
	.update = move_source_swap_update,
	.load = move_source_swap_update,
	.activate = move_filter_activate,
	.deactivate = move_filter_deactivate,
	.show = move_filter_show,
	.hide = move_filter_hide,
};
