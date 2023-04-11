#include "nvar-load.h"
#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>

#define BBOXES_COUNT 25
#define MAX_ACTIONS 10

#define ACTION_MOVE_SOURCE 0
#define ACTION_MOVE_VALUE 1
#define ACTION_ENABLE_FILTER 2
#define ACTION_SOURCE_VISIBILITY 3

#define SCENEITEM_PROPERTY_ALL 0
#define SCENEITEM_PROPERTY_POS 1
#define SCENEITEM_PROPERTY_POSX 2
#define SCENEITEM_PROPERTY_POSY 3
#define SCENEITEM_PROPERTY_SCALE 4
#define SCENEITEM_PROPERTY_SCALEX 5
#define SCENEITEM_PROPERTY_SCALEY 6
#define SCENEITEM_PROPERTY_ROT 7
#define SCENEITEM_PROPERTY_CROP_LEFT 8
#define SCENEITEM_PROPERTY_CROP_RIGHT 9
#define SCENEITEM_PROPERTY_CROP_TOP 9
#define SCENEITEM_PROPERTY_CROP_BOTTOM 10

#define FEATURE_BOUNDINGBOX 0
#define FEATURE_LANDMARK 1
#define FEATURE_POSE 2
#define FEATURE_EXPRESSION 3
#define FEATURE_GAZE 4
#define FEATURE_KEYPOINTS 5
#define FEATURE_JOINT_ANGLES 6

#define FEATURE_BOUNDINGBOX_LEFT 0
#define FEATURE_BOUNDINGBOX_HORIZONTAL_CENTER 1
#define FEATURE_BOUNDINGBOX_RIGHT 2
#define FEATURE_BOUNDINGBOX_WIDTH 3
#define FEATURE_BOUNDINGBOX_TOP 4
#define FEATURE_BOUNDINGBOX_VERTICAL_CENTER 5
#define FEATURE_BOUNDINGBOX_BOTOM 6
#define FEATURE_BOUNDINGBOX_HEIGHT 7
#define FEATURE_BOUNDINGBOX_TOP_LEFT 8
#define FEATURE_BOUNDINGBOX_TOP_CENTER 9
#define FEATURE_BOUNDINGBOX_TOP_RIGHT 10
#define FEATURE_BOUNDINGBOX_CENTER_RIGHT 11
#define FEATURE_BOUNDINGBOX_BOTTOM_RIGHT 12
#define FEATURE_BOUNDINGBOX_BOTTOM_CENTER 13
#define FEATURE_BOUNDINGBOX_BOTTOM_LEFT 14
#define FEATURE_BOUNDINGBOX_CENTER_LEFT 15
#define FEATURE_BOUNDINGBOX_CENTER 16
#define FEATURE_BOUNDINGBOX_SIZE 17

#define FEATURE_LANDMARK_X 0
#define FEATURE_LANDMARK_Y 1
#define FEATURE_LANDMARK_CONFIDENCE 2
#define FEATURE_LANDMARK_DISTANCE 3
#define FEATURE_LANDMARK_DIFF_X 4
#define FEATURE_LANDMARK_DIFF_Y 5
#define FEATURE_LANDMARK_ROT 6
#define FEATURE_LANDMARK_DIFF 7
#define FEATURE_LANDMARK_POS 8

#define FEATURE_THRESHOLD_NONE 0
#define FEATURE_THRESHOLD_ENABLE_OVER 1
#define FEATURE_THRESHOLD_ENABLE_UNDER 2
#define FEATURE_THRESHOLD_DISABLE_OVER 3
#define FEATURE_THRESHOLD_DISABLE_UNDER 4
#define FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER 5
#define FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER 6

bool nvar_loaded = false;
bool nvar_new_sdk = false;

struct nvidia_move_action {
	uint32_t action;
	obs_weak_source_t *target;
	char *name; //scene item name or setting name
	bool is_int;
	uint32_t property;
	float diff;
	float factor;
	uint32_t feature;
	uint32_t feature_property;
	uint32_t feature_number[3];
	float threshold;
	float value;
};

struct nvidia_move_info {
	obs_source_t *source;

	DARRAY(struct nvidia_move_action) actions;

	NvAR_FeatureHandle handle;
	CUstream stream; // CUDA stream
	char *last_error;

	bool stabilizeFace;
	bool isNumLandmarks126;

	bool got_new_frame;

	bool processing_stop;
	bool target_valid;
	bool images_allocated;
	bool initial_render;
	bool processed_frame;
	uint32_t width;
	uint32_t height;

	NvCVImage *src_img;     // src img in obs format (RGBA ?) on GPU
	NvCVImage *BGR_src_img; // src img in BGR on GPU
	NvCVImage *stage;       // planar stage img used for transfer to texture

	DARRAY(float) landmarks_confidence;
	DARRAY(NvAR_Point2f) landmarks;
	struct NvAR_BBoxes bboxes;

	gs_texrender_t *render;
	gs_texrender_t *render_unorm;
	enum gs_color_space space;

	gs_effect_t *effect;
	gs_eparam_t *image_param;
	gs_eparam_t *multiplier_param;
};

static const char *nv_move_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("NvidiaMoveFilter");
}

static bool nv_move_log_error(struct nvidia_move_info *filter,
			      NvCV_Status nvErr, const char *function)
{
	if (nvErr == NVCV_SUCCESS)
		return false;
	const char *errString = NvCV_GetErrorStringFromCode(nvErr);
	if (filter->last_error && strcmp(errString, filter->last_error) == 0)
		return true;
	blog(LOG_ERROR, "[Move Transition] Error in %s; error %i: %s", function,
	     nvErr, errString);
	bfree(filter->last_error);
	filter->last_error = bstrdup(errString);
	return true;
}

static void nv_move_update(void *data, obs_data_t *settings)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;

	size_t actions = (size_t)obs_data_get_int(settings, "actions");
	if (actions < filter->actions.num) {
		for (size_t i = actions; i < filter->actions.num; i++) {
			struct nvidia_move_action *action =
				filter->actions.array + i;
			obs_weak_source_release(action->target);
			action->target = NULL;
			bfree(action->name);
			action->name = NULL;
		}
	}
	da_resize(filter->actions, actions);

	struct dstr name = {0};
	for (size_t i = 1; i <= actions; i++) {
		struct nvidia_move_action *action =
			filter->actions.array + i - 1;
		dstr_printf(&name, "action_%lld_action", i);
		action->action =
			(uint32_t)obs_data_get_int(settings, name.array);
		obs_weak_source_release(action->target);
		action->target = NULL;
		bfree(action->name);
		action->name = NULL;
		if (action->action == ACTION_MOVE_SOURCE ||
		    action->action == ACTION_SOURCE_VISIBILITY) {
			dstr_printf(&name, "action_%lld_scene", i);
			const char *scene_name =
				obs_data_get_string(settings, name.array);
			obs_source_t *source =
				obs_get_source_by_name(scene_name);
			if (source) {
				if (obs_source_is_scene(source))
					action->target =
						obs_source_get_weak_source(
							source);
				obs_source_release(source);
			}
			dstr_printf(&name, "action_%lld_sceneitem", i);
			action->name = bstrdup(
				obs_data_get_string(settings, name.array));
		}
		if (action->action == ACTION_MOVE_SOURCE) {
			dstr_printf(&name, "action_%lld_sceneitem_property", i);
			action->property = (uint32_t)obs_data_get_int(
				settings, name.array);
		} else if (action->action == ACTION_ENABLE_FILTER ||
			   action->action == ACTION_SOURCE_VISIBILITY) {
			dstr_printf(&name, "action_%lld_enable", i);
			action->property = (uint32_t)obs_data_get_int(
				settings, name.array);

			dstr_printf(&name, "action_%lld_threshold", i);
			action->threshold = (float)obs_data_get_double(
				settings, name.array);
		}
		if (action->action == ACTION_MOVE_VALUE ||
		    action->action == ACTION_ENABLE_FILTER) {
			dstr_printf(&name, "action_%lld_source", i);
			const char *source_name =
				obs_data_get_string(settings, name.array);
			obs_source_t *source =
				obs_get_source_by_name(source_name);
			if (source) {
				dstr_printf(&name, "action_%lld_filter", i);
				const char *filter_name = obs_data_get_string(
					settings, name.array);
				obs_source_t *f = obs_source_get_filter_by_name(
					source, filter_name);
				if (f ||
				    action->action == ACTION_ENABLE_FILTER) {
					obs_source_release(source);
					source = f;
				}
				action->target =
					obs_source_get_weak_source(source);

				if (action->action == ACTION_MOVE_VALUE) {
					dstr_printf(&name,
						    "action_%lld_property", i);
					action->name =
						bstrdup(obs_data_get_string(
							settings, name.array));

					obs_properties_t *sp =
						obs_source_properties(source);
					obs_property_t *p = obs_properties_get(
						sp, action->name);
					action->is_int =
						(obs_property_get_type(p) ==
						 OBS_PROPERTY_INT);

					obs_properties_destroy(sp);
				}

				obs_source_release(source);
			}
		}

		dstr_printf(&name, "action_%lld_feature", i);
		action->feature =
			(uint32_t)obs_data_get_int(settings, name.array);

		if (action->feature == FEATURE_BOUNDINGBOX) {
			dstr_printf(&name, "action_%lld_bounding_box", i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);
		} else if (action->feature == FEATURE_LANDMARK) {
			dstr_printf(&name, "action_%lld_landmark", i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);

			dstr_printf(&name, "action_%lld_landmark_1", i);
			if (obs_data_get_int(settings, name.array))
				action->feature_number[0] =
					(uint32_t)obs_data_get_int(settings,
								   name.array) -
					1;
			dstr_printf(&name, "action_%lld_landmark_2", i);
			if (obs_data_get_int(settings, name.array))
				action->feature_number[1] =
					(uint32_t)obs_data_get_int(settings,
								   name.array) -
					1;
		}
		dstr_printf(&name, "action_%lld_factor", i);
		obs_data_set_default_double(settings, name.array, 100.0f);

		if (action->action == ACTION_MOVE_SOURCE ||
		    action->action == ACTION_MOVE_VALUE) {
			action->factor = (float)obs_data_get_double(
						 settings, name.array) /
					 100.0f;
			dstr_printf(&name, "action_%lld_diff", i);
			action->diff = (float)obs_data_get_double(settings,
								  name.array);
		} else {
			action->factor = 1.0f;
			action->diff = 0.0f;
		}
	}
	dstr_free(&name);

	if (!filter->handle) {
		bfree(filter->last_error);
		filter->last_error = NULL;
		if (filter->handle) {
			nv_move_log_error(filter, NvAR_Destroy(filter->handle),
					  "Destroy");
			filter->handle = NULL;
		}
		if (nv_move_log_error(filter,
				      NvAR_Create(NvAR_Feature_LandmarkDetection,
						  &filter->handle),
				      "Create"))
			return;

		//char modelDir[MAX_PATH];
		//nvar_get_model_path(modelDir, MAX_PATH);
		if (nv_move_log_error(
			    filter,
			    NvAR_SetString(filter->handle,
					   NvAR_Parameter_Config(ModelDir), ""),
			    "Set ModelDir"))
			return;

		if (nv_move_log_error(filter,
				      NvAR_SetCudaStream(
					      filter->handle,
					      NvAR_Parameter_Config(CUDAStream),
					      filter->stream),
				      "Set CUDAStream"))
			return;

		if (filter->BGR_src_img) {
			if (nv_move_log_error(
				    filter,
				    NvAR_SetObject(filter->handle,
						   NvAR_Parameter_Input(Image),
						   filter->BGR_src_img,
						   sizeof(NvCVImage)),
				    "Set Image"))
				return;
		}

		unsigned int OUTPUT_SIZE_KPTS = 0;
		NvCV_Status nvErr = NvAR_GetU32(
			filter->handle, NvAR_Parameter_Config(Landmarks_Size),
			&OUTPUT_SIZE_KPTS);
		if (nvErr == NVCV_SUCCESS) {
			da_resize(filter->landmarks, OUTPUT_SIZE_KPTS);

			nvErr = NvAR_SetObject(filter->handle,
					       NvAR_Parameter_Output(Landmarks),
					       filter->landmarks.array,
					       sizeof(NvAR_Point2f));
		}

		unsigned int OUTPUT_SIZE_KPTS_CONF = 0;
		nvErr = NvAR_GetU32(
			filter->handle,
			NvAR_Parameter_Config(LandmarksConfidence_Size),
			&OUTPUT_SIZE_KPTS_CONF);
		if (nvErr == NVCV_SUCCESS) {
			da_resize(filter->landmarks_confidence,
				  OUTPUT_SIZE_KPTS_CONF);

			nvErr = NvAR_SetF32Array(
				filter->handle,
				NvAR_Parameter_Output(LandmarksConfidence),
				filter->landmarks_confidence.array,
				OUTPUT_SIZE_KPTS_CONF);
		}

		if (!filter->bboxes.max_boxes) {
			filter->bboxes.boxes = (struct NvAR_Rect *)bzalloc(
				sizeof(struct NvAR_Rect) * BBOXES_COUNT);
			filter->bboxes.max_boxes = BBOXES_COUNT;
			filter->bboxes.num_boxes = 0;
		}

		nvErr = NvAR_SetObject(filter->handle,
				       NvAR_Parameter_Output(BoundingBoxes),
				       &filter->bboxes, sizeof(NvAR_BBoxes));

		if (nv_move_log_error(filter, NvAR_Load(filter->handle),
				      "Load"))
			return;
	}
}

static void nv_move_actual_destroy(void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	if (filter->handle)
		nv_move_log_error(filter, NvAR_Destroy(filter->handle),
				  "Destroy");

	if (filter->stream)
		nv_move_log_error(filter,
				  NvAR_CudaStreamDestroy(filter->stream),
				  "Destroy Cuda Stream");

	obs_enter_graphics();
	gs_texrender_destroy(filter->render);
	gs_texrender_destroy(filter->render_unorm);
	obs_leave_graphics();
	if (filter->src_img)
		NvCVImage_Destroy(filter->src_img);
	if (filter->BGR_src_img)
		NvCVImage_Destroy(filter->BGR_src_img);
	if (filter->stage)
		NvCVImage_Destroy(filter->stage);

	bfree(filter);
}

static void nv_move_destroy(void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	obs_queue_task(OBS_TASK_GRAPHICS, nv_move_actual_destroy, data, false);
}

static void *nv_move_create(obs_data_t *settings, obs_source_t *context)
{
	if (!nvar_loaded)
		return NULL;
	struct nvidia_move_info *filter =
		(struct nvidia_move_info *)bzalloc(sizeof(*filter));
	filter->source = context;

	//NvCV_Status nvErr = NvVFX_CudaStreamCreate(&filter->stream);

	if (nv_move_log_error(filter, NvAR_CudaStreamCreate(&filter->stream),
			      "Create Cuda Stream"))
		return NULL;

	/* 3. Load alpha mask effect. */
	char *effect_path = obs_module_file("effects/unorm.effect");

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	bfree(effect_path);
	if (filter->effect) {
		//filter->mask_param = gs_effect_get_param_by_name(filter->effect, "mask");
		filter->image_param =
			gs_effect_get_param_by_name(filter->effect, "image");
		//filter->threshold_param = gs_effect_get_param_by_name(filter->effect, "threshold");
		filter->multiplier_param = gs_effect_get_param_by_name(
			filter->effect, "multiplier");
	}
	obs_leave_graphics();

	nv_move_update(filter, settings);

	return filter;
}

bool list_add_scene(void *data, obs_source_t *source)
{
	obs_property_t *p = data;
	size_t idx = 1;
	const size_t count = obs_property_list_item_count(p);
	const char *name = obs_source_get_name(source);
	while (idx < count) {
		if (strcmp(obs_property_list_item_string(p, idx), name) > 0)
			break;
		idx++;
	}
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

bool nv_move_landmark_changed(void *priv, obs_properties_t *props,
			      obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	long long landmark = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_landmark", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_landmark_1", action_number);
	obs_property_t *landmark1 = obs_properties_get(props, name.array);
	obs_property_set_visible(landmark1, true);
	dstr_printf(&name, "action_%lld_landmark_2", action_number);
	obs_property_t *landmark2 = obs_properties_get(props, name.array);
	obs_property_set_visible(landmark2,
				 landmark != FEATURE_LANDMARK_POS &&
					 landmark >= FEATURE_LANDMARK_DISTANCE);

	return true;
}

bool nv_move_feature_changed(void *priv, obs_properties_t *props,
			     obs_property_t *property, obs_data_t *settings)
{
	const char *prop_name = obs_property_name(property);
	long long feature = obs_data_get_int(settings, prop_name);
	long long action_number = 0;
	if (sscanf(prop_name, "action_%lld_feature", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_bounding_box", action_number);
	obs_property_t *bounding_box = obs_properties_get(props, name.array);
	obs_property_set_visible(bounding_box, false);

	dstr_printf(&name, "action_%lld_landmark", action_number);
	obs_property_t *landmark = obs_properties_get(props, name.array);
	obs_property_set_visible(landmark, false);
	dstr_printf(&name, "action_%lld_landmark_1", action_number);
	obs_property_t *landmark1 = obs_properties_get(props, name.array);
	obs_property_set_visible(landmark1, false);
	dstr_printf(&name, "action_%lld_landmark_2", action_number);
	obs_property_t *landmark2 = obs_properties_get(props, name.array);
	obs_property_set_visible(landmark2, false);

	if (feature == FEATURE_BOUNDINGBOX) {
		obs_property_set_visible(bounding_box, true);
	} else if (feature == FEATURE_LANDMARK) {
		obs_property_set_visible(landmark, true);
		nv_move_landmark_changed(priv, props, landmark, settings);
	}
	return true;
}

bool nv_move_actions_changed(void *priv, obs_properties_t *props,
			     obs_property_t *property, obs_data_t *settings)
{
	struct dstr name = {0};
	long long actions = obs_data_get_int(settings, "actions");
	bool changed = false;
	for (long long i = 1; i <= MAX_ACTIONS; i++) {
		dstr_printf(&name, "action_%lld_group", i);
		obs_property_t *group = obs_properties_get(props, name.array);
		if (obs_property_visible(group) == (i <= actions))
			continue;
		obs_property_set_visible(group, i <= actions);
		changed = true;
	}
	dstr_free(&name);
	return changed;
}

bool nv_move_action_changed(void *priv, obs_properties_t *props,
			    obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	long long action = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_action", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	//dstr_printf(&name, "action_%lld_group", action_number);
	//obs_property_t *group = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_scene", action_number);
	obs_property_t *scene = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_sceneitem", action_number);
	obs_property_t *sceneitem = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_source", action_number);
	obs_property_t *source = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_filter", action_number);
	obs_property_t *filter = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_sceneitem_property", action_number);
	obs_property_t *sceneitem_property =
		obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_enable", action_number);
	obs_property_t *enable = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_threshold", action_number);
	obs_property_t *threshold = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_property", action_number);
	obs_property_t *p = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_factor", action_number);
	obs_property_t *factor = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_diff", action_number);
	obs_property_t *diff = obs_properties_get(props, name.array);

	obs_property_set_visible(scene, false);
	obs_property_set_visible(sceneitem, false);
	obs_property_set_visible(source, false);
	obs_property_set_visible(filter, false);
	obs_property_set_visible(sceneitem_property, false);
	obs_property_set_visible(enable, false);
	obs_property_set_visible(threshold, false);
	obs_property_set_visible(p, false);
	obs_property_set_visible(factor, false);
	obs_property_set_visible(diff, false);

	if (action == ACTION_MOVE_SOURCE) {
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
		obs_property_set_visible(sceneitem_property, true);
		obs_property_set_visible(factor, true);
		obs_property_set_visible(diff, true);
	} else {
		dstr_printf(&name, "action_%lld_landmark", action_number);
		obs_property_t *landmark =
			obs_properties_get(props, name.array);
		for (size_t i = FEATURE_LANDMARK_X; i <= FEATURE_LANDMARK_ROT;
		     i++) {
			obs_property_list_item_disable(landmark, i, false);
		}
		for (size_t i = FEATURE_LANDMARK_DIFF;
		     i <= FEATURE_LANDMARK_POS; i++) {
			obs_property_list_item_disable(landmark, i, true);
		}

		dstr_printf(&name, "action_%lld_bounding_box", action_number);
		obs_property_t *bbox = obs_properties_get(props, name.array);
		for (size_t i = FEATURE_BOUNDINGBOX_LEFT;
		     i <= FEATURE_BOUNDINGBOX_HEIGHT; i++) {
			obs_property_list_item_disable(bbox, i, false);
		}
		for (size_t i = FEATURE_BOUNDINGBOX_TOP_LEFT;
		     i <= FEATURE_BOUNDINGBOX_SIZE; i++) {
			obs_property_list_item_disable(bbox, i, true);
		}
	}
	if (action == ACTION_MOVE_VALUE) {
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, true);
		obs_property_set_visible(p, true);
		obs_property_set_visible(factor, true);
		obs_property_set_visible(diff, true);
	} else if (action == ACTION_ENABLE_FILTER) {
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, true);
		obs_property_set_visible(enable, true);
		obs_property_set_visible(threshold, true);
	} else if (action == ACTION_SOURCE_VISIBILITY) {
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
		obs_property_set_visible(enable, true);
		obs_property_set_visible(threshold, true);
	}
	if (obs_property_visible(scene) &&
	    !obs_property_list_item_count(scene)) {
		obs_property_list_add_string(scene, "", "");
		obs_enum_scenes(list_add_scene, scene);
	}
	if (obs_property_visible(source) &&
	    !obs_property_list_item_count(source)) {
		obs_property_list_add_string(source, "", "");
		obs_enum_scenes(list_add_scene, source);
		obs_enum_sources(list_add_scene, source);
	}

	dstr_free(&name);
	//
	return true;
}

bool nv_move_add_sceneitems(obs_scene_t *scene, obs_sceneitem_t *sceneitem,
			    void *data)
{
	obs_property_t *p = data;
	const char *source_name =
		obs_source_get_name(obs_sceneitem_get_source(sceneitem));
	obs_property_list_add_string(p, source_name, source_name);
	return true;
}

bool nv_move_scene_changed(void *priv, obs_properties_t *props,
			   obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	const char *scene_name = obs_data_get_string(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_scene", &action_number) != 1 ||
	    !action_number)
		return false;

	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_sceneitem", action_number);
	obs_property_t *sceneitem = obs_properties_get(props, name.array);
	if (!sceneitem)
		return false;

	obs_property_list_clear(sceneitem);
	obs_property_list_add_string(sceneitem, "", "");

	obs_source_t *scene_source = obs_get_source_by_name(scene_name);
	if (!scene_source)
		return true;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	obs_scene_enum_items(scene, nv_move_add_sceneitems, sceneitem);
	obs_source_release(scene_source);
	return true;
}

void nv_move_add_filter(obs_source_t *parent, obs_source_t *child, void *data)
{
	obs_property_t *p = data;
	const char *source_name = obs_source_get_name(child);
	obs_property_list_add_string(p, source_name, source_name);
}

bool nv_move_sceneitem_property_changed(void *priv, obs_properties_t *props,
					obs_property_t *property,
					obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	long long sceneitem_prop = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_sceneitem_property",
		   &action_number) != 1 ||
	    !action_number)
		return false;

	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_landmark", action_number);
	obs_property_t *landmark = obs_properties_get(props, name.array);

	bool dualprop = (sceneitem_prop == SCENEITEM_PROPERTY_POS ||
			 sceneitem_prop == SCENEITEM_PROPERTY_SCALE);

	for (size_t i = FEATURE_LANDMARK_X; i <= FEATURE_LANDMARK_ROT; i++) {
		obs_property_list_item_disable(landmark, i, dualprop);
	}
	for (size_t i = FEATURE_LANDMARK_DIFF; i <= FEATURE_LANDMARK_POS; i++) {
		obs_property_list_item_disable(landmark, i, !dualprop);
	}

	dstr_printf(&name, "action_%lld_bounding_box", action_number);
	obs_property_t *bbox = obs_properties_get(props, name.array);
	for (size_t i = FEATURE_BOUNDINGBOX_LEFT;
	     i <= FEATURE_BOUNDINGBOX_HEIGHT; i++) {
		obs_property_list_item_disable(bbox, i, dualprop);
	}
	for (size_t i = FEATURE_BOUNDINGBOX_TOP_LEFT;
	     i <= FEATURE_BOUNDINGBOX_SIZE; i++) {
		obs_property_list_item_disable(bbox, i, !dualprop);
	}

	return true;
}

bool nv_move_filter_changed(void *priv, obs_properties_t *props,
			    obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	const char *filter_name = obs_data_get_string(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_filter", &action_number) != 1 ||
	    !action_number)
		return false;

	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_property", action_number);
	obs_property_t *prop = obs_properties_get(props, name.array);
	if (!prop)
		return false;

	obs_property_list_clear(prop);
	obs_property_list_add_string(prop, "", "");

	dstr_printf(&name, "action_%lld_source", action_number);
	const char *source_name = obs_data_get_string(settings, name.array);
	obs_source_t *source = obs_get_source_by_name(source_name);
	if (!source)
		return true;

	obs_source_t *f = obs_source_get_filter_by_name(source, filter_name);
	if (f) {
		obs_source_release(source);
		source = f;
	}
	obs_properties_t *sp = obs_source_properties(source);
	obs_source_release(source);
	if (!sp)
		return true;
	obs_property_t *p = obs_properties_first(sp);
	for (; p != NULL; obs_property_next(&p)) {
		if (!obs_property_visible(p))
			continue;
		const enum obs_property_type prop_type =
			obs_property_get_type(p);
		if (prop_type == OBS_PROPERTY_INT ||
		    prop_type == OBS_PROPERTY_FLOAT) {
			const char *n = obs_property_name(p);
			const char *d = obs_property_description(p);
			obs_property_list_add_string(prop, d, n);
		}
	}

	obs_properties_destroy(sp);

	return true;
}

bool nv_move_source_changed(void *priv, obs_properties_t *props,
			    obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	const char *source_name = obs_data_get_string(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_source", &action_number) != 1 ||
	    !action_number)
		return false;

	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_filter", action_number);
	obs_property_t *filter = obs_properties_get(props, name.array);
	if (!filter)
		return false;

	obs_property_list_clear(filter);
	obs_property_list_add_string(filter, "", "");

	obs_source_t *source = obs_get_source_by_name(source_name);
	if (!source)
		return true;
	obs_source_enum_filters(source, nv_move_add_filter, filter);

	obs_source_release(source);
	nv_move_filter_changed(priv, props, filter, settings);
	return true;
}

static obs_properties_t *nv_move_properties(void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	obs_properties_t *props = obs_properties_create();

	if (filter->last_error) {
		obs_property_text_set_info_type(
			obs_properties_add_text(props, "last_error",
						filter->last_error,
						OBS_TEXT_INFO),
			OBS_TEXT_INFO_WARNING);
	}

	obs_property_t *p = obs_properties_add_int_slider(
		props, "actions", obs_module_text("Actions"), 1, MAX_ACTIONS,
		1);

	obs_property_set_modified_callback2(p, nv_move_actions_changed, data);

	struct dstr name = {0};
	struct dstr description = {0};
	for (long long i = 1; i <= MAX_ACTIONS; i++) {
		obs_properties_t *group = obs_properties_create();
		dstr_printf(&name, "action_%lld_action", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Action"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("MoveSource"),
					  ACTION_MOVE_SOURCE);
		obs_property_list_add_int(p, obs_module_text("MoveValue"),
					  ACTION_MOVE_VALUE);
		obs_property_list_add_int(p, obs_module_text("FilterEnable"),
					  ACTION_ENABLE_FILTER);
		obs_property_list_add_int(p,
					  obs_module_text("SourceVisibility"),
					  ACTION_SOURCE_VISIBILITY);

		obs_property_set_modified_callback2(p, nv_move_action_changed,
						    data);
		dstr_printf(&name, "action_%lld_scene", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Scene"),
					    OBS_COMBO_TYPE_EDITABLE,
					    OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(p, nv_move_scene_changed,
						    data);
		dstr_printf(&name, "action_%lld_sceneitem", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Source"),
					    OBS_COMBO_TYPE_EDITABLE,
					    OBS_COMBO_FORMAT_STRING);
		dstr_printf(&name, "action_%lld_source", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Source"),
					    OBS_COMBO_TYPE_EDITABLE,
					    OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(p, nv_move_source_changed,
						    data);

		dstr_printf(&name, "action_%lld_filter", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Filter"),
					    OBS_COMBO_TYPE_EDITABLE,
					    OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(p, nv_move_filter_changed,
						    data);

		dstr_printf(&name, "action_%lld_property", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Property"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_STRING);

		dstr_printf(&name, "action_%lld_sceneitem_property", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Property"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(p, obs_module_text("All"),
						  SCENEITEM_PROPERTY_ALL),
			true);

		obs_property_list_add_int(p, obs_module_text("Pos"),
					  SCENEITEM_PROPERTY_POS);
		obs_property_list_add_int(p, obs_module_text("PosX"),
					  SCENEITEM_PROPERTY_POSX);
		obs_property_list_add_int(p, obs_module_text("PosY"),
					  SCENEITEM_PROPERTY_POSY);
		obs_property_list_add_int(p, obs_module_text("Scale"),
					  SCENEITEM_PROPERTY_SCALE);
		obs_property_list_add_int(p, obs_module_text("ScaleX"),
					  SCENEITEM_PROPERTY_SCALEX);
		obs_property_list_add_int(p, obs_module_text("ScaleY"),
					  SCENEITEM_PROPERTY_SCALEY);
		obs_property_list_add_int(p, obs_module_text("Rotation"),
					  SCENEITEM_PROPERTY_ROT);
		obs_property_set_modified_callback2(
			p, nv_move_sceneitem_property_changed, data);

		dstr_printf(&name, "action_%lld_enable", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Enable"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(
				p, obs_module_text("ThresholdAction.None"),
				FEATURE_THRESHOLD_NONE),
			true);
		obs_property_list_add_int(
			p, obs_module_text("ThresholdAction.EnableOver"),
			FEATURE_THRESHOLD_ENABLE_OVER);
		obs_property_list_add_int(
			p, obs_module_text("ThresholdAction.EnableUnder"),
			FEATURE_THRESHOLD_ENABLE_UNDER);
		obs_property_list_add_int(
			p, obs_module_text("ThresholdAction.DisableOver"),
			FEATURE_THRESHOLD_DISABLE_OVER);
		obs_property_list_add_int(
			p, obs_module_text("ThresholdAction.DisableUnder"),
			FEATURE_THRESHOLD_DISABLE_UNDER);
		obs_property_list_add_int(
			p,
			obs_module_text(
				"ThresholdAction.EnableOverDisableUnder"),
			FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER);
		obs_property_list_add_int(
			p,
			obs_module_text(
				"ThresholdAction.EnableUnderDisableOver"),
			FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER);

		dstr_printf(&name, "action_%lld_feature", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Feature"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("BoundingBox"),
					  FEATURE_BOUNDINGBOX);
		obs_property_list_add_int(p, obs_module_text("Landmark"),
					  FEATURE_LANDMARK);
		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(p, obs_module_text("Pose"),
						  FEATURE_POSE),
			true);
		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(p,
						  obs_module_text("Expression"),
						  FEATURE_EXPRESSION),
			true);
		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(p, obs_module_text("Gaze"),
						  FEATURE_GAZE),
			true);
		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(p,
						  obs_module_text("Keypoints"),
						  FEATURE_KEYPOINTS),
			true);
		obs_property_list_item_disable(
			p,
			obs_property_list_add_int(
				p, obs_module_text("JointAngles"),
				FEATURE_JOINT_ANGLES),
			true);

		obs_property_set_modified_callback2(p, nv_move_feature_changed,
						    data);

		dstr_printf(&name, "action_%lld_bounding_box", i);
		p = obs_properties_add_list(
			group, name.array,
			obs_module_text("BoundingBoxProperty"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Left"),
					  FEATURE_BOUNDINGBOX_LEFT);
		obs_property_list_add_int(
			p, obs_module_text("HorizontalCenter"),
			FEATURE_BOUNDINGBOX_HORIZONTAL_CENTER);
		obs_property_list_add_int(p, obs_module_text("Right"),
					  FEATURE_BOUNDINGBOX_RIGHT);
		obs_property_list_add_int(p, obs_module_text("Width"),
					  FEATURE_BOUNDINGBOX_WIDTH);
		obs_property_list_add_int(p, obs_module_text("Top"),
					  FEATURE_BOUNDINGBOX_TOP);
		obs_property_list_add_int(p, obs_module_text("VerticalCenter"),
					  FEATURE_BOUNDINGBOX_VERTICAL_CENTER);
		obs_property_list_add_int(p, obs_module_text("Bottom"),
					  FEATURE_BOUNDINGBOX_BOTOM);
		obs_property_list_add_int(p, obs_module_text("Height"),
					  FEATURE_BOUNDINGBOX_HEIGHT);
		obs_property_list_add_int(p, obs_module_text("TopLeft"),
					  FEATURE_BOUNDINGBOX_TOP_LEFT);
		obs_property_list_add_int(p, obs_module_text("TopCenter"),
					  FEATURE_BOUNDINGBOX_TOP_CENTER);
		obs_property_list_add_int(p, obs_module_text("TopRight"),
					  FEATURE_BOUNDINGBOX_TOP_RIGHT);
		obs_property_list_add_int(p, obs_module_text("CenterRight"),
					  FEATURE_BOUNDINGBOX_CENTER_RIGHT);
		obs_property_list_add_int(p, obs_module_text("BottomRight"),
					  FEATURE_BOUNDINGBOX_BOTTOM_RIGHT);
		obs_property_list_add_int(p, obs_module_text("BottomCenter"),
					  FEATURE_BOUNDINGBOX_BOTTOM_CENTER);
		obs_property_list_add_int(p, obs_module_text("BottomLeft"),
					  FEATURE_BOUNDINGBOX_BOTTOM_LEFT);
		obs_property_list_add_int(p, obs_module_text("CenterLeft"),
					  FEATURE_BOUNDINGBOX_CENTER_LEFT);
		obs_property_list_add_int(p, obs_module_text("Center"),
					  FEATURE_BOUNDINGBOX_CENTER);
		obs_property_list_add_int(p, obs_module_text("Size"),
					  FEATURE_BOUNDINGBOX_SIZE);

		dstr_printf(&name, "action_%lld_landmark", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("LandmarkProperty"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("X"),
					  FEATURE_LANDMARK_X);
		obs_property_list_add_int(p, obs_module_text("Y"),
					  FEATURE_LANDMARK_Y);
		obs_property_list_add_int(p, obs_module_text("Confidence"),
					  FEATURE_LANDMARK_CONFIDENCE);
		obs_property_list_add_int(p, obs_module_text("Distance"),
					  FEATURE_LANDMARK_DISTANCE);
		obs_property_list_add_int(p, obs_module_text("DiffX"),
					  FEATURE_LANDMARK_DIFF_X);
		obs_property_list_add_int(p, obs_module_text("DiffY"),
					  FEATURE_LANDMARK_DIFF_Y);
		obs_property_list_add_int(p, obs_module_text("Rotation"),
					  FEATURE_LANDMARK_ROT);
		obs_property_list_add_int(p, obs_module_text("Difference"),
					  FEATURE_LANDMARK_DIFF);
		obs_property_list_add_int(p, obs_module_text("Position"),
					  FEATURE_LANDMARK_POS);

		obs_property_set_modified_callback2(p, nv_move_landmark_changed,
						    data);

		dstr_printf(&name, "action_%lld_landmark_1", i);
		obs_properties_add_int_slider(group, name.array,
					      obs_module_text("Landmark"), 1,
					      (int)filter->landmarks.num, 1);

		dstr_printf(&name, "action_%lld_landmark_2", i);
		obs_properties_add_int_slider(group, name.array,
					      obs_module_text("Landmark"), 1,
					      (int)filter->landmarks.num, 1);

		dstr_printf(&name, "action_%lld_factor", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Factor"),
					     -10000.0, 10000.0, 1.0);
		obs_property_float_set_suffix(p, "%");

		dstr_printf(&name, "action_%lld_diff", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Diff"), -10000.0,
					     10000.0, 1.0);

		dstr_printf(&name, "action_%lld_threshold", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Threshold"),
					     -10000.0, 10000, 1.0);

		dstr_printf(&name, "action_%lld_group", i);
		dstr_printf(&description, "%s %lld", obs_module_text("Action"),
			    i);
		obs_properties_add_group(props, name.array, description.array,
					 OBS_GROUP_NORMAL, group);
	}
	dstr_free(&name);
	dstr_free(&description);

	return props;
}

static void nv_move_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "actions", 1);
}

static struct obs_source_frame *nv_move_video(void *data,
					      struct obs_source_frame *frame)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	filter->got_new_frame = true;
	return frame;
}

static const char *
get_tech_name_and_multiplier(enum gs_color_space current_space,
			     enum gs_color_space source_space,
			     float *multiplier)
{
	const char *tech_name = "Draw";
	*multiplier = 1.f;

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
		if (current_space == GS_CS_709_SCRGB) {
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
		}
		break;
	case GS_CS_709_EXTENDED:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawTonemap";
			break;
		case GS_CS_709_SCRGB:
			tech_name = "DrawMultiply";
			*multiplier = obs_get_video_sdr_white_level() / 80.0f;
			break;
		default:
			break;
		}
		break;
	case GS_CS_709_SCRGB:
		switch (current_space) {
		case GS_CS_SRGB:
		case GS_CS_SRGB_16F:
			tech_name = "DrawMultiplyTonemap";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		case GS_CS_709_EXTENDED:
			tech_name = "DrawMultiply";
			*multiplier = 80.0f / obs_get_video_sdr_white_level();
			break;
		default:
			break;
		}
	}

	return tech_name;
}

void nv_move_draw_frame(struct nvidia_move_info *context, uint32_t w,
			uint32_t h)
{
	const enum gs_color_space current_space = gs_get_color_space();
	float multiplier;
	const char *technique = get_tech_name_and_multiplier(
		current_space, context->space, &multiplier);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texture_t *tex = gs_texrender_get_texture(context->render);
	if (!tex)
		return;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"),
				   tex);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "multiplier"),
			    multiplier);

	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(tex, 0, w, h);

	gs_enable_framebuffer_srgb(previous);
	gs_blend_state_pop();
}

static float nv_move_action_get_float(struct nvidia_move_info *filter,
				      struct nvidia_move_action *action)
{
	float value = 0.0f;
	if (action->feature == FEATURE_BOUNDINGBOX) {
		if (action->feature_property == FEATURE_BOUNDINGBOX_LEFT) {
			value = filter->bboxes.boxes[0].x;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_HORIZONTAL_CENTER) {
			value = filter->bboxes.boxes[0].x +
				filter->bboxes.boxes[0].width / 2.0f;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_RIGHT) {
			value = filter->bboxes.boxes[0].x +
				filter->bboxes.boxes[0].width;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_WIDTH) {
			value = filter->bboxes.boxes[0].width;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP) {
			value = filter->bboxes.boxes[0].y;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_VERTICAL_CENTER) {
			value = filter->bboxes.boxes[0].y +
				filter->bboxes.boxes[0].height / 2.0f;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTOM) {
			value = filter->bboxes.boxes[0].y +
				filter->bboxes.boxes[0].height;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_HEIGHT) {
			value = filter->bboxes.boxes[0].height;
		}
	} else if (action->feature == FEATURE_LANDMARK) {
		if (action->feature_property == FEATURE_LANDMARK_X) {
			value = filter->landmarks
					.array[action->feature_number[0]]
					.x;
		} else if (action->feature_property == FEATURE_LANDMARK_Y) {
			value = filter->landmarks
					.array[action->feature_number[0]]
					.y;
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_CONFIDENCE) {
			value = filter->landmarks_confidence
					.array[action->feature_number[0]];
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_DISTANCE) {
			float x = filter->landmarks
					  .array[action->feature_number[0]]
					  .x -
				  filter->landmarks
					  .array[action->feature_number[1]]
					  .x;
			float y = filter->landmarks
					  .array[action->feature_number[0]]
					  .y -
				  filter->landmarks
					  .array[action->feature_number[1]]
					  .y;
			value = sqrtf(x * x + y * y);
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_DIFF_X) {
			value = filter->landmarks
					.array[action->feature_number[1]]
					.x -
				filter->landmarks
					.array[action->feature_number[0]]
					.x;
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_DIFF_Y) {
			value = filter->landmarks
					.array[action->feature_number[1]]
					.y -
				filter->landmarks
					.array[action->feature_number[0]]
					.y;
		} else if (action->feature_property == FEATURE_LANDMARK_ROT) {
			value = DEG(atan2f(
				(filter->landmarks
					 .array[action->feature_number[1]]
					 .y -
				 filter->landmarks
					 .array[action->feature_number[0]]
					 .y),
				(filter->landmarks
					 .array[action->feature_number[1]]
					 .x -
				 filter->landmarks
					 .array[action->feature_number[0]]
					 .x)));
		}
	}
	value *= action->factor;
	value += action->diff;
	return value;
}

static void nv_move_action_get_vec2(struct nvidia_move_info *filter,
				    struct nvidia_move_action *action,
				    struct vec2 *value)
{
	value->x = 0.0f;
	value->y = 0.0f;
	if (action->feature == FEATURE_BOUNDINGBOX) {
		if (action->feature_property == FEATURE_BOUNDINGBOX_TOP_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_SIZE) {
			value->x = filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].height;
		}
	} else if (action->feature == FEATURE_LANDMARK) {
		if (action->feature_property == FEATURE_LANDMARK_DIFF) {
			value->x = filter->landmarks
					   .array[action->feature_number[1]]
					   .x -
				   filter->landmarks
					   .array[action->feature_number[0]]
					   .x;
			value->y = filter->landmarks
					   .array[action->feature_number[1]]
					   .y -
				   filter->landmarks
					   .array[action->feature_number[0]]
					   .y;
		} else if (action->feature_property == FEATURE_LANDMARK_POS) {
			value->x = filter->landmarks
					   .array[action->feature_number[0]]
					   .x;
			value->y = filter->landmarks
					   .array[action->feature_number[0]]
					   .y;
		}
	}
	value->x *= action->factor;
	value->x += action->diff;
	value->y *= action->factor;
	value->y += action->diff;
}

static void nv_move_render(void *data, gs_effect_t *effect)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	if (filter->processing_stop) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	obs_source_t *const target = obs_filter_get_target(filter->source);
	obs_source_t *const parent = obs_filter_get_parent(filter->source);
	uint32_t base_width = obs_source_get_base_width(target);
	uint32_t base_height = obs_source_get_base_height(target);
	if (!base_width || !base_height || !target || !parent) {
		obs_source_skip_video_filter(filter->source);
		return;
	}
	if (filter->processed_frame) {
		nv_move_draw_frame(filter, base_width, base_height);
		return;
	}
	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};
	const enum gs_color_space space = obs_source_get_color_space(
		target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	if (filter->space != space) {
		filter->space = space;
		//init_images_greenscreen(filter);
		filter->initial_render = false;
	}
	const enum gs_color_format format = gs_get_format_from_space(space);
	if (!filter->render ||
	    gs_texrender_get_format(filter->render) != format) {
		gs_texrender_destroy(filter->render);
		filter->render = gs_texrender_create(format, GS_ZS_NONE);
	} else {
		gs_texrender_reset(filter->render);
	}

	//if (!filter->render) {
	//	obs_source_skip_video_filter(filter->context);
	//	return;
	//}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	if (gs_texrender_begin_with_color_space(filter->render, base_width,
						base_height, space)) {
		const float w = (float)base_width;
		const float h = (float)base_height;
		uint32_t parent_flags = obs_source_get_output_flags(target);
		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, w, 0.0f, h, -100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);
		gs_texrender_end(filter->render);
		//filter->space = space;
	}
	if (!filter->render_unorm) {
		filter->render_unorm =
			gs_texrender_create(GS_BGRA_UNORM, GS_ZS_NONE);
	} else {
		gs_texrender_reset(filter->render_unorm);
	}

	if (gs_texrender_begin_with_color_space(filter->render_unorm,
						base_width, base_height,
						GS_CS_SRGB)) {
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);
		gs_enable_blending(false);

		gs_ortho(0.0f, (float)filter->width, 0.0f,
			 (float)filter->height, -100.0f, 100.0f);

		const char *tech_name = "ConvertUnorm";
		float multiplier = 1.f;
		switch (space) {
		case GS_CS_709_EXTENDED:
			tech_name = "ConvertUnormTonemap";
			break;
		case GS_CS_709_SCRGB:
			tech_name = "ConvertUnormMultiplyTonemap";
			multiplier = 80.0f / obs_get_video_sdr_white_level();
		}

		gs_effect_set_texture_srgb(
			filter->image_param,
			gs_texrender_get_texture(filter->render));
		gs_effect_set_float(filter->multiplier_param, multiplier);

		while (gs_effect_loop(filter->effect, tech_name)) {
			gs_draw(GS_TRIS, 0, 3);
		}

		gs_texrender_end(filter->render_unorm);

		gs_enable_blending(true);
		gs_enable_framebuffer_srgb(previous);
	}
	gs_blend_state_pop();

	if (!filter->src_img) {
		if (nv_move_log_error(filter,
				      NvCVImage_Create(base_width, base_height,
						       NVCV_BGRA, NVCV_U8,
						       NVCV_CHUNKY, NVCV_GPU, 1,
						       &filter->src_img),
				      "Image Create"))
			os_atomic_set_bool(&filter->processing_stop, true);

		struct ID3D11Texture2D *d11texture2 =
			(struct ID3D11Texture2D *)gs_texture_get_obj(
				gs_texrender_get_texture(filter->render_unorm));
		if (d11texture2) {
			if (nv_move_log_error(filter,
					      NvCVImage_InitFromD3D11Texture(
						      filter->src_img,
						      d11texture2),
					      "ID3D11Texture"))
				os_atomic_set_bool(&filter->processing_stop,
						   true);
			filter->initial_render = true;
		}
	}

	if (!filter->BGR_src_img) {
		if (NvCVImage_Create(base_width, base_height, NVCV_BGR, NVCV_U8,
				     NVCV_CHUNKY, NVCV_GPU, 1,
				     &filter->BGR_src_img) != NVCV_SUCCESS) {
			//goto fail;
		}
		if (NvCVImage_Alloc(filter->BGR_src_img, base_width,
				    base_height, NVCV_BGR, NVCV_U8, NVCV_CHUNKY,
				    NVCV_GPU, 1) != NVCV_SUCCESS) {
			//goto fail;
		}
		nv_move_log_error(filter,
				  NvAR_SetObject(filter->handle,
						 NvAR_Parameter_Input(Image),
						 filter->BGR_src_img,
						 sizeof(NvCVImage)),
				  "Set Image");
	}
	if (!filter->stage) {
		if (NvCVImage_Create(base_width, base_height, NVCV_RGBA,
				     NVCV_U8, NVCV_PLANAR, NVCV_GPU, 1,
				     &filter->stage) != NVCV_SUCCESS) {
			//goto fail;
		}
		if (NvCVImage_Alloc(filter->stage, base_width, base_height,
				    NVCV_RGBA, NVCV_U8, NVCV_PLANAR, NVCV_GPU,
				    1) != NVCV_SUCCESS) {
			//goto fail;
		}
	}

	nv_move_log_error(
		filter, NvCVImage_MapResource(filter->src_img, filter->stream),
		"Map Resource");

	/* 2. Convert to BGR. */
	nv_move_log_error(filter,
			  NvCVImage_Transfer(filter->src_img,
					     filter->BGR_src_img, 1.0f,
					     filter->stream, filter->stage),
			  "Transfer");

	nv_move_log_error(filter,
			  NvCVImage_UnmapResource(filter->src_img,
						  filter->stream),
			  "Unmap Resource");

	nv_move_log_error(filter, NvAR_Run(filter->handle), "Run");

	for (size_t i = 0; i < filter->actions.num; i++) {
		struct nvidia_move_action *action = filter->actions.array + i;
		if (action->action == ACTION_MOVE_SOURCE) {
			if (!action->name)
				continue;
			obs_source_t *scene_source =
				obs_weak_source_get_source(action->target);
			if (!scene_source)
				continue;
			obs_scene_t *scene =
				obs_scene_from_source(scene_source);
			obs_source_release(scene_source);
			if (!scene)
				continue;
			obs_sceneitem_t *item =
				obs_scene_find_source(scene, action->name);
			if (!item)
				continue;

			// SCENEITEM_PROPERTY_ALL
			if (action->property == SCENEITEM_PROPERTY_POS) {
				struct vec2 pos;
				nv_move_action_get_vec2(filter, action, &pos);
				obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_POSX) {
				struct vec2 pos;
				obs_sceneitem_get_pos(item, &pos);
				pos.x = nv_move_action_get_float(filter,
								 action);
				obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_POSY) {
				struct vec2 pos;
				obs_sceneitem_get_pos(item, &pos);
				pos.y = nv_move_action_get_float(filter,
								 action);
				obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALE) {
				struct vec2 scale;
				nv_move_action_get_vec2(filter, action, &scale);
				if (obs_sceneitem_get_bounds_type(item) ==
				    OBS_BOUNDS_NONE) {
					scale.x /= (float)obs_source_get_width(
						obs_sceneitem_get_source(item));
					scale.y /= (float)obs_source_get_height(
						obs_sceneitem_get_source(item));
					obs_sceneitem_set_scale(item, &scale);
				} else {
					obs_sceneitem_set_bounds(item, &scale);
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALEX) {
				struct vec2 scale;
				if (obs_sceneitem_get_bounds_type(item) ==
				    OBS_BOUNDS_NONE) {
					obs_sceneitem_get_scale(item, &scale);
					scale.x =
						nv_move_action_get_float(
							filter, action) /
						(float)obs_source_get_width(
							obs_sceneitem_get_source(
								item));
					obs_sceneitem_set_scale(item, &scale);
				} else {
					obs_sceneitem_get_bounds(item, &scale);
					scale.x = nv_move_action_get_float(
						filter, action);
					obs_sceneitem_set_bounds(item, &scale);
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALEY) {
				struct vec2 scale;
				if (obs_sceneitem_get_bounds_type(item) ==
				    OBS_BOUNDS_NONE) {
					obs_sceneitem_get_scale(item, &scale);
					scale.y =
						nv_move_action_get_float(
							filter, action) /
						(float)obs_source_get_height(
							obs_sceneitem_get_source(
								item));
					obs_sceneitem_set_scale(item, &scale);
				} else {
					obs_sceneitem_get_bounds(item, &scale);
					scale.y = nv_move_action_get_float(
						filter, action);
					obs_sceneitem_set_bounds(item, &scale);
				}
			} else if (action->property == SCENEITEM_PROPERTY_ROT) {
				obs_sceneitem_set_rot(
					item, nv_move_action_get_float(filter,
								       action));
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_LEFT) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				crop.left = (int)nv_move_action_get_float(
					filter, action);
				obs_sceneitem_set_crop(item, &crop);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_RIGHT) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				crop.right = (int)nv_move_action_get_float(
					filter, action);
				obs_sceneitem_set_crop(item, &crop);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_TOP) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				crop.top = (int)nv_move_action_get_float(
					filter, action);
				obs_sceneitem_set_crop(item, &crop);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_BOTTOM) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				crop.bottom = (int)nv_move_action_get_float(
					filter, action);
				obs_sceneitem_set_crop(item, &crop);
			}
		} else if (action->action == ACTION_MOVE_VALUE) {
			obs_source_t *source =
				obs_weak_source_get_source(action->target);
			if (!source)
				continue;
			obs_data_t *d = obs_data_create();
			if (action->is_int) {
				obs_data_set_int(
					d, action->name,
					(long long)nv_move_action_get_float(
						filter, action));
			} else {
				obs_data_set_double(
					d, action->name,
					(double)nv_move_action_get_float(
						filter, action));
			}
			obs_source_update(source, d);
			obs_data_release(d);
			obs_source_release(source);
		} else if (action->action == ACTION_ENABLE_FILTER) {
			obs_source_t *source =
				obs_weak_source_get_source(action->target);
			if (!source)
				continue;
			float value = nv_move_action_get_float(filter, action);
			if ((action->property ==
				     FEATURE_THRESHOLD_ENABLE_OVER ||
			     action->property ==
				     FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
			    value >= action->threshold &&
			    !obs_source_enabled(source)) {
				obs_source_set_enabled(source, true);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
				   value < action->threshold &&
				   !obs_source_enabled(source)) {
				obs_source_set_enabled(source, true);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_DISABLE_OVER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
				   value >= action->threshold &&
				   obs_source_enabled(source)) {
				obs_source_set_enabled(source, false);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_DISABLE_UNDER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
				   value < action->threshold &&
				   obs_source_enabled(source)) {
				obs_source_set_enabled(source, false);
			}
			obs_source_release(source);
		} else if (action->action == ACTION_SOURCE_VISIBILITY) {
			if (!action->name)
				continue;
			obs_source_t *scene_source =
				obs_weak_source_get_source(action->target);
			if (!scene_source)
				continue;
			obs_scene_t *scene =
				obs_scene_from_source(scene_source);
			obs_source_release(scene_source);
			if (!scene)
				continue;
			obs_sceneitem_t *item =
				obs_scene_find_source(scene, action->name);
			if (!item)
				continue;
			float value = nv_move_action_get_float(filter, action);
			if ((action->property ==
				     FEATURE_THRESHOLD_ENABLE_OVER ||
			     action->property ==
				     FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
			    value >= action->threshold) {
				obs_sceneitem_set_visible(item, true);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
				   value < action->threshold) {
				obs_sceneitem_set_visible(item, true);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_DISABLE_OVER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_UNDER_DISABLE_OVER) &&
				   value >= action->threshold) {
				obs_sceneitem_set_visible(item, false);
			} else if ((action->property ==
					    FEATURE_THRESHOLD_DISABLE_UNDER ||
				    action->property ==
					    FEATURE_THRESHOLD_ENABLE_OVER_DISABLE_UNDER) &&
				   value < action->threshold) {
				obs_sceneitem_set_visible(item, false);
			}
		}
	}

	filter->processed_frame = true;
	nv_move_draw_frame(filter, base_width, base_height);
}

static void nv_move_tick(void *data, float t)
{
	UNUSED_PARAMETER(t);
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	if (filter->processing_stop)
		return;
	if (!obs_filter_get_target(filter->source))
		return;

	obs_source_t *target = obs_filter_get_target(filter->source);

	filter->target_valid = true;

	const uint32_t cx = obs_source_get_base_width(target);
	const uint32_t cy = obs_source_get_base_height(target);

	// initially the sizes are 0
	if (!cx && !cy) {
		filter->target_valid = false;
		return;
	}

	if (cx != filter->width && cy != filter->height) {
		filter->images_allocated = false;
		filter->width = cx;
		filter->height = cy;
	}
	if (!filter->images_allocated) {
		//obs_enter_graphics();
		//init_images_greenscreen(filter);
		//obs_leave_graphics();
		filter->initial_render = false;
	}

	filter->processed_frame = false;
}

static enum gs_color_space
nv_move_get_color_space(void *data, size_t count,
			const enum gs_color_space *preferred_spaces)
{
	const enum gs_color_space potential_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->source),
		OBS_COUNTOF(potential_spaces), potential_spaces);

	enum gs_color_space space = source_space;
	for (size_t i = 0; i < count; ++i) {
		space = preferred_spaces[i];
		if (space == source_space)
			break;
	}

	return space;
}

bool load_nvar(void)
{
	bool old_sdk_loaded = false;
	unsigned int version = get_lib_version();
	uint8_t major = (version >> 24) & 0xff;
	uint8_t minor = (version >> 16) & 0x00ff;
	uint8_t build = (version >> 8) & 0x0000ff;
	uint8_t revision = (version >> 0) & 0x000000ff;
	if (version) {
		blog(LOG_INFO,
		     "[Move Transition]: NVIDIA AR version: %i.%i.%i.%i", major,
		     minor, build, revision);
	}
	if (!load_nv_ar_libs()) {
		//blog(LOG_INFO, "[NVIDIA VIDEO FX]: FX disabled, redistributable not found or could not be loaded.");
		return false;
	}

#define LOAD_SYM_FROM_LIB(sym, lib, dll)                                     \
	if (!(sym = (sym##_t)GetProcAddress(lib, #sym))) {                   \
		DWORD err = GetLastError();                                  \
		printf("[NVIDIA VIDEO FX]: Couldn't load " #sym " from " dll \
		       ": %lu (0x%lx)",                                      \
		       err, err);                                            \
		release_nv_ar();                                             \
		goto unload_everything;                                      \
	}

#define LOAD_SYM_FROM_LIB2(sym, lib, dll)                                    \
	if (!(sym = (sym##_t)GetProcAddress(lib, #sym))) {                   \
		DWORD err = GetLastError();                                  \
		printf("[NVIDIA VIDEO FX]: Couldn't load " #sym " from " dll \
		       ": %lu (0x%lx)",                                      \
		       err, err);                                            \
		nvvfx_new_sdk = false;                                       \
	} else {                                                             \
		nvvfx_new_sdk = true;                                        \
	}

#define LOAD_SYM(sym) LOAD_SYM_FROM_LIB(sym, nv_ar, "nvARPose.dll")
	LOAD_SYM(NvAR_Create);
	LOAD_SYM(NvAR_Load);
	LOAD_SYM(NvAR_Run);
	LOAD_SYM(NvAR_Destroy);
	LOAD_SYM(NvAR_CudaStreamCreate);
	LOAD_SYM(NvAR_CudaStreamDestroy);
	LOAD_SYM(NvAR_SetU32);
	LOAD_SYM(NvAR_SetS32);
	LOAD_SYM(NvAR_SetF32);
	LOAD_SYM(NvAR_SetF64);
	LOAD_SYM(NvAR_SetU64);
	LOAD_SYM(NvAR_SetObject);
	LOAD_SYM(NvAR_SetCudaStream);
	LOAD_SYM(NvAR_SetF32Array);
	LOAD_SYM(NvAR_SetString);
	LOAD_SYM(NvAR_GetU32);
	LOAD_SYM(NvAR_GetS32);
	LOAD_SYM(NvAR_GetF32);
	LOAD_SYM(NvAR_GetF64);
	LOAD_SYM(NvAR_GetU64);
	LOAD_SYM(NvAR_GetObject);
	LOAD_SYM(NvAR_GetCudaStream);
	LOAD_SYM(NvAR_GetF32Array);
	LOAD_SYM(NvAR_GetString);
	LOAD_SYM(NvAR_GetObject);

	old_sdk_loaded = true;
#undef LOAD_SYM

#define LOAD_SYM(sym) LOAD_SYM_FROM_LIB(sym, nv_cvimage, "NVCVImage.dll")
	LOAD_SYM(NvCV_GetErrorStringFromCode);
	LOAD_SYM(NvCVImage_Init);
	LOAD_SYM(NvCVImage_InitView);
	LOAD_SYM(NvCVImage_Alloc);
	LOAD_SYM(NvCVImage_Realloc);
	LOAD_SYM(NvCVImage_Dealloc);
	LOAD_SYM(NvCVImage_Create);
	LOAD_SYM(NvCVImage_Destroy);
	LOAD_SYM(NvCVImage_ComponentOffsets);
	LOAD_SYM(NvCVImage_Transfer);
	LOAD_SYM(NvCVImage_TransferRect);
	LOAD_SYM(NvCVImage_TransferFromYUV);
	LOAD_SYM(NvCVImage_TransferToYUV);
	LOAD_SYM(NvCVImage_MapResource);
	LOAD_SYM(NvCVImage_UnmapResource);
	LOAD_SYM(NvCVImage_Composite);
	LOAD_SYM(NvCVImage_CompositeRect);
	LOAD_SYM(NvCVImage_CompositeOverConstant);
	LOAD_SYM(NvCVImage_FlipY);
	LOAD_SYM(NvCVImage_GetYUVPointers);
	LOAD_SYM(NvCVImage_InitFromD3D11Texture);
	LOAD_SYM(NvCVImage_ToD3DFormat);
	LOAD_SYM(NvCVImage_FromD3DFormat);
	LOAD_SYM(NvCVImage_ToD3DColorSpace);
	LOAD_SYM(NvCVImage_FromD3DColorSpace);
#undef LOAD_SYM

#define LOAD_SYM(sym) LOAD_SYM_FROM_LIB(sym, nv_cudart, "cudart64_110.dll")
	LOAD_SYM(cudaMalloc);
	LOAD_SYM(cudaStreamSynchronize);
	LOAD_SYM(cudaFree);
	LOAD_SYM(cudaMemcpy);
	LOAD_SYM(cudaMemsetAsync);
#undef LOAD_SYM

	/*
#define LOAD_SYM(sym) LOAD_SYM_FROM_LIB2(sym, nv_videofx, "NVVideoEffects.dll")
	LOAD_SYM(NvVFX_SetStateObjectHandleArray);
	LOAD_SYM(NvVFX_AllocateState);
	LOAD_SYM(NvVFX_DeallocateState);
	LOAD_SYM(NvVFX_ResetState);
	if (!nvvfx_new_sdk) {
		//blog(LOG_INFO,  "[NVIDIA VIDEO FX]: sdk loaded but old redistributable detected; please upgrade.");
	}
#undef LOAD_SYM
*/

	nvar_loaded = true;
	//blog(LOG_INFO, "[NVIDIA VIDEO FX]: enabled, redistributable found");
	return true;

unload_everything:
	nvar_loaded = false;
	//blog(LOG_INFO,  "[NVIDIA VIDEO FX]: disabled, redistributable not found");
	release_nv_ar();
	return false;
}

void unload_nvar(void)
{
	release_nv_ar();
}

struct obs_source_info nvidia_move_filter = {
	.id = "nv_move_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = nv_move_name,
	.create = nv_move_create,
	.destroy = nv_move_destroy,
	.get_defaults = nv_move_defaults,
	.get_properties = nv_move_properties,
	.update = nv_move_update,
	.load = nv_move_update,
	.filter_video = nv_move_video,
	.video_render = nv_move_render,
	.video_tick = nv_move_tick,
	.video_get_color_space = nv_move_get_color_space,
};
