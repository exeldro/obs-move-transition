#include "nvar-load.h"
#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include "easing.h"
#include "move-transition.h"

#define BBOXES_COUNT 25
#define MAX_ACTIONS 40

#define ACTION_MOVE_SOURCE 0
#define ACTION_MOVE_VALUE 1
#define ACTION_ENABLE_FILTER 2
#define ACTION_SOURCE_VISIBILITY 3
#define ACTION_ATTACH_SOURCE 4

#define ATTACH_EYES 0
#define ATTACH_LEFT_EYE 1
#define ATTACH_RIGHT_EYE 2
#define ATTACH_EYEBROWS 3
#define ATTACH_LEFT_EYEBROW 4
#define ATTACH_RIGHT_EYEBROW 5
#define ATTACH_EARS 6
#define ATTACH_LEFT_EAR 7
#define ATTACH_RIGHT_EAR 8
#define ATTACH_NOSE 9
#define ATTACH_MOUTH 10
#define ATTACH_UPPER_LIP 11
#define ATTACH_LOWER_LIP 12
#define ATTACH_CHIN 13
#define ATTACH_JAW 14
#define ATTACH_FOREHEAD 15

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
#define SCENEITEM_PROPERTY_CROP_TOP 11
#define SCENEITEM_PROPERTY_CROP_BOTTOM 10

#define FEATURE_BOUNDINGBOX 0
#define FEATURE_LANDMARK 1
#define FEATURE_POSE 2
#define FEATURE_EXPRESSION 3
#define FEATURE_GAZE 4
#define FEATURE_BODY 5

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

#define FEATURE_POSE_X 0
#define FEATURE_POSE_Y 1
#define FEATURE_POSE_Z 2
#define FEATURE_POSE_W 3

#define FEATURE_GAZE_VECTOR 0
#define FEATURE_GAZE_VECTOR_PITCH 1
#define FEATURE_GAZE_VECTOR_YAW 2
#define FEATURE_GAZE_HEADTRANSLATION 3
#define FEATURE_GAZE_HEADTRANSLATION_X 4
#define FEATURE_GAZE_HEADTRANSLATION_Y 5
#define FEATURE_GAZE_HEADTRANSLATION_Z 6
#define FEATURE_GAZE_DIRECTION_CENTER_POINT 7
#define FEATURE_GAZE_DIRECTION_CENTER_POINT_X 8
#define FEATURE_GAZE_DIRECTION_CENTER_POINT_Y 9
#define FEATURE_GAZE_DIRECTION_CENTER_POINT_Z 10
#define FEATURE_GAZE_DIRECTION_VECTOR 11
#define FEATURE_GAZE_DIRECTION_VECTOR_X 12
#define FEATURE_GAZE_DIRECTION_VECTOR_Y 13
#define FEATURE_GAZE_DIRECTION_VECTOR_Z 14

#define FEATURE_EXPRESSION_SINGLE 0
#define FEATURE_EXPRESSION_VECTOR 1
#define FEATURE_EXPRESSION_ADD 2
#define FEATURE_EXPRESSION_SUBSTRACT 3
#define FEATURE_EXPRESSION_DISTANCE 4
#define FEATURE_EXPRESSION_AVG 5

#define BODY_CONFIDENCE 0
#define BODY_2D_POSX 1
#define BODY_2D_POSY 2
#define BODY_2D_DISTANCE 3
#define BODY_2D_ROT 4
#define BODY_2D_DIFF_X 5
#define BODY_2D_DIFF_Y 6
#define BODY_2D_DIFF 7
#define BODY_2D_POS 8
#define BODY_3D_POSX 9
#define BODY_3D_POSY 10
#define BODY_3D_POSZ 11
#define BODY_3D_DISTANCE 12
#define BODY_3D_DIFF_X 13
#define BODY_3D_DIFF_Y 14
#define BODY_3D_DIFF_Z 15
#define BODY_3D_POS 16
#define BODY_3D_DIFF 17
#define BODY_ANGLE_X 18
#define BODY_ANGLE_Y 19
#define BODY_ANGLE_Z 20
#define BODY_ANGLE 21

bool nvar_loaded = false;
bool nvar_new_sdk = false;

struct nvidia_move_action {
	uint32_t action;
	obs_weak_source_t *target;
	char *name; //scene item name or setting name
	bool is_int;
	uint32_t property;
	float diff;
	float diff2;
	float factor;
	float factor2;
	uint32_t feature;
	uint32_t feature_property;
	int32_t feature_number[4];
	float threshold;
	float required_confidence;
	float easing;
	float previous_float;
	struct vec2 previous_vec2;
	bool disabled;
};

struct nvidia_move_info {
	obs_source_t *source;

	DARRAY(struct nvidia_move_action) actions;

	NvAR_FeatureHandle faceDetectHandle;
	NvAR_FeatureHandle landmarkHandle;
	NvAR_FeatureHandle expressionHandle;
	NvAR_FeatureHandle gazeHandle;
	NvAR_FeatureHandle bodyHandle;
	CUstream stream; // CUDA stream
	char *last_error;

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
	DARRAY(NvAR_Point2f) gaze_output_landmarks;
	DARRAY(float) bboxes_confidence;
	NvAR_BBoxes bboxes;
	NvAR_Quaternion pose;
	DARRAY(float) expressions;
	float gaze_angles_vector[2];
	float head_translation[3];
	NvAR_Point3f gaze_direction[2];
	DARRAY(float) keypoints_confidence;
	DARRAY(NvAR_Point2f) keypoints;
	DARRAY(NvAR_Point3f) keypoints3D;
	DARRAY(NvAR_Quaternion) joint_angles;

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

static void nv_move_feature_handle(struct nvidia_move_info *filter,
				   NvAR_FeatureHandle handle)
{
	if (nv_move_log_error(
		    filter,
		    NvAR_SetString(handle, NvAR_Parameter_Config(ModelDir), ""),
		    "Set ModelDir"))
		return;

	if (nv_move_log_error(filter,
			      NvAR_SetCudaStream(
				      handle, NvAR_Parameter_Config(CUDAStream),
				      filter->stream),
			      "Set CUDAStream"))
		return;

	if (filter->BGR_src_img) {
		if (nv_move_log_error(
			    filter,
			    NvAR_SetObject(handle, NvAR_Parameter_Input(Image),
					   filter->BGR_src_img,
					   sizeof(NvCVImage)),
			    "Set Image"))
			return;
	}
	if (!filter->bboxes.max_boxes) {
		filter->bboxes.boxes = (struct NvAR_Rect *)bzalloc(
			sizeof(struct NvAR_Rect) * BBOXES_COUNT);
		filter->bboxes.max_boxes = BBOXES_COUNT;
		filter->bboxes.num_boxes = 0;
	}
	NvAR_SetObject(handle, NvAR_Parameter_Output(BoundingBoxes),
		       &filter->bboxes, sizeof(NvAR_BBoxes));
}

static void nv_move_landmarks(struct nvidia_move_info *filter,
			      NvAR_FeatureHandle handle)
{
	unsigned int OUTPUT_SIZE_KPTS = 0;
	NvCV_Status nvErr = NvAR_GetU32(handle,
					NvAR_Parameter_Config(Landmarks_Size),
					&OUTPUT_SIZE_KPTS);
	if (nvErr != NVCV_SUCCESS)
		return;

	da_resize(filter->landmarks, OUTPUT_SIZE_KPTS);

	nvErr = NvAR_SetObject(handle, NvAR_Parameter_Output(Landmarks),
			       filter->landmarks.array, sizeof(NvAR_Point2f));

	unsigned int OUTPUT_SIZE_KPTS_CONF = 0;
	nvErr = NvAR_GetU32(handle,
			    NvAR_Parameter_Config(LandmarksConfidence_Size),
			    &OUTPUT_SIZE_KPTS_CONF);

	if (nvErr == NVCV_SUCCESS)
		da_resize(filter->landmarks_confidence, OUTPUT_SIZE_KPTS_CONF);
	else
		da_resize(filter->landmarks_confidence, OUTPUT_SIZE_KPTS);

	nvErr = NvAR_SetF32Array(handle,
				 NvAR_Parameter_Output(LandmarksConfidence),
				 filter->landmarks_confidence.array,
				 (int)filter->landmarks_confidence.num);
}

static bool get_expression_value(struct nvidia_move_info *filter,
				 int32_t expression, float *value)
{
	if (expression >= 0) {
		*value = filter->expressions.array[expression];
	} else if (expression == -1) {
		return false;
	} else if (expression > -14) {
		int p = expression * -2 - 4;
		*value = (filter->expressions.array[p] +
			  filter->expressions.array[p + 1]) /
			 2.0f;
	} else if (expression == -14) { // jawSideways
		*value = filter->expressions.array[25] -
			 filter->expressions.array[27];
	} else if (expression == -15) { // mouthSideways
		*value = filter->expressions.array[34] -
			 filter->expressions.array[40];
	} else if (expression == -18) {                  // eyeLookLeft
		*value = filter->expressions.array[16] + // eyeLookOut_L
			 filter->expressions.array[15];  // eyeLookIn_R
	} else if (expression == -21) {                  // eyeLookRight
		*value = filter->expressions.array[17] + // eyeLookOut_R
			 filter->expressions.array[14];  // eyeLookIn_L
	} else if (expression > -28) {
		int p = expression * -2 - 3;
		*value = (filter->expressions.array[p] +
			  filter->expressions.array[p + 1]) /
			 2.0f;
	} else if (expression == -28) {                    // eyeLookSideways
		*value = (filter->expressions.array[16] +  // eyeLookOut_L
			  filter->expressions.array[15]) - // eyeLookIn_R
			 (filter->expressions.array[17] +  // eyeLookOut_R
			  filter->expressions.array[14]);  // eyeLookIn_L
	} else if (expression == -29) {                    // eyeLookUpDown
		*value = (filter->expressions.array[18] +  // eyeLookUp_L
			  filter->expressions.array[19]) - // eyeLookUp_R
			 (filter->expressions.array[12] +  // eyeLookDown_L
			  filter->expressions.array[13]);  // eyeLookDown_R
	} else if (expression == -30) {                    // eyeLookSideways_L
		*value = filter->expressions.array[16] -   // eyeLookOut_L
			 filter->expressions.array[14];    // eyeLookIn_L
	} else if (expression == -31) {                    // eyeLookSideways_R
		*value = filter->expressions.array[17] -   // eyeLookOut_R
			 filter->expressions.array[15];    // eyeLookIn_R
	} else if (expression == -32) {                    //eyeLookUpDown_L
		*value = filter->expressions.array[18] -   // eyeLookUp_L
			 filter->expressions.array[12];    // eyeLookDown_L
	} else if (expression == -33) {                    // eyeLookUpDown_R
		*value = filter->expressions.array[19] -   // eyeLookUp_R
			 filter->expressions.array[13];    // eyeLookDown_R
	} else {
		return false;
	}
	return true;
}

static bool nv_move_action_get_float(struct nvidia_move_info *filter,
				     struct nvidia_move_action *action,
				     bool easing, float *v)
{
	float value = 0.0f;
	bool success = false;
	if (action->feature == FEATURE_BOUNDINGBOX &&
	    filter->bboxes.max_boxes && filter->bboxes.num_boxes) {
		if (filter->bboxes_confidence.array &&
		    filter->bboxes_confidence.array[0] <
			    action->required_confidence) {
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_LEFT) {
			value = filter->bboxes.boxes[0].x;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_HORIZONTAL_CENTER) {
			value = filter->bboxes.boxes[0].x +
				filter->bboxes.boxes[0].width / 2.0f;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_RIGHT) {
			value = filter->bboxes.boxes[0].x +
				filter->bboxes.boxes[0].width;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_WIDTH) {
			value = filter->bboxes.boxes[0].width;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP) {
			value = filter->bboxes.boxes[0].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_VERTICAL_CENTER) {
			value = filter->bboxes.boxes[0].y +
				filter->bboxes.boxes[0].height / 2.0f;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTOM) {
			value = filter->bboxes.boxes[0].y +
				filter->bboxes.boxes[0].height;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_HEIGHT) {
			value = filter->bboxes.boxes[0].height;
			success = true;
		} else {
			success = false;
		}
	} else if (action->feature == FEATURE_LANDMARK &&
		   filter->landmarks.num) {
		if (action->feature_number[0] >=
		    (int32_t)filter->landmarks.num) {
		} else if (filter->landmarks_confidence
				   .array[action->feature_number[0]] <
			   action->required_confidence) {
		} else if (action->feature_property == FEATURE_LANDMARK_X) {
			value = filter->landmarks
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property == FEATURE_LANDMARK_Y) {
			value = filter->landmarks
					.array[action->feature_number[0]]
					.y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_CONFIDENCE) {
			value = filter->landmarks_confidence
					.array[action->feature_number[0]];
			success = true;
		} else if (action->feature_number[1] >=
			   (int32_t)filter->landmarks.num) {
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
			success = true;
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_DIFF_X) {
			value = filter->landmarks
					.array[action->feature_number[1]]
					.x -
				filter->landmarks
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_LANDMARK_DIFF_Y) {
			value = filter->landmarks
					.array[action->feature_number[1]]
					.y -
				filter->landmarks
					.array[action->feature_number[0]]
					.y;
			success = true;
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
			success = true;
		}
	} else if (action->feature == FEATURE_POSE) {
		if (action->feature_property == FEATURE_POSE_X) {
			value = filter->pose.x;
			success = true;
		} else if (action->feature_property == FEATURE_POSE_Y) {
			value = filter->pose.y;
			success = true;
		} else if (action->feature_property == FEATURE_POSE_Z) {
			value = filter->pose.z;
			success = true;
		} else if (action->feature_property == FEATURE_POSE_W) {
			value = filter->pose.w;
			success = true;
		}
	} else if (action->feature == FEATURE_EXPRESSION &&
		   filter->expressions.num) {
		if (action->feature_property == FEATURE_EXPRESSION_SINGLE) {
			success = get_expression_value(
				filter, action->feature_number[0], &value);
			if (success)
				value *= action->factor;
		} else if (action->feature_property !=
			   FEATURE_EXPRESSION_VECTOR) {
			float a = 0.0f;
			float b = 0.0f;
			success =
				get_expression_value(filter,
						     action->feature_number[0],
						     &a) &&
				get_expression_value(
					filter, action->feature_number[1], &b);
			if (success) {
				a *= action->factor;
				b *= action->factor2;
				if (action->feature_property ==
				    FEATURE_EXPRESSION_ADD) {
					value = a + b;
				} else if (action->feature_property ==
					   FEATURE_EXPRESSION_SUBSTRACT) {
					value = a - b;
				} else if (action->feature_property ==
					   FEATURE_EXPRESSION_DISTANCE) {
					value = fabsf(a - b);
				} else if (action->feature_property ==
					   FEATURE_EXPRESSION_AVG) {
					value = (a + b) / 2.0f;
				}
			}
		}
	} else if (action->feature == FEATURE_GAZE) {
		if (action->feature_property == FEATURE_GAZE_VECTOR_PITCH) {
			value = DEG(filter->gaze_angles_vector[0]);
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_VECTOR_YAW) {
			value = DEG(filter->gaze_angles_vector[1]);
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_HEADTRANSLATION_X) {
			value = filter->head_translation[0];
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_HEADTRANSLATION_Y) {
			value = filter->head_translation[1];
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_HEADTRANSLATION_Z) {
			value = filter->head_translation[2];
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_CENTER_POINT_X) {
			value = filter->gaze_direction[0].x;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_CENTER_POINT_Y) {
			value = filter->gaze_direction[0].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_CENTER_POINT_Z) {
			value = filter->gaze_direction[0].z;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_VECTOR_X) {
			value = filter->gaze_direction[1].x;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_VECTOR_Y) {
			value = filter->gaze_direction[1].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_GAZE_DIRECTION_VECTOR_Z) {
			value = filter->gaze_direction[1].z;
			success = true;
		}
	} else if (action->feature == FEATURE_BODY && filter->keypoints.num) {
		if (action->feature_number[0] >=
		    (int32_t)filter->keypoints.num) {
		} else if (filter->keypoints_confidence
				   .array[action->feature_number[0]] <
			   action->required_confidence) {
		} else if (action->feature_property == BODY_CONFIDENCE) {
			value = filter->keypoints_confidence
					.array[action->feature_number[0]];
			success = true;
		} else if (action->feature_property == BODY_2D_POSX) {
			value = filter->keypoints
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property == BODY_2D_POSY) {
			value = filter->keypoints
					.array[action->feature_number[0]]
					.y;
			success = true;
		} else if (action->feature_property == BODY_3D_POSX) {
			value = filter->keypoints3D
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property == BODY_3D_POSY) {
			value = filter->keypoints3D
					.array[action->feature_number[0]]
					.y;
			success = true;
		} else if (action->feature_property == BODY_3D_POSZ) {
			value = filter->keypoints3D
					.array[action->feature_number[0]]
					.z;
			success = true;
		} else if (action->feature_property == BODY_ANGLE_X) {
			filter->joint_angles.array[action->feature_number[0]].x;
			success = true;
		} else if (action->feature_property == BODY_ANGLE_Y) {
			filter->joint_angles.array[action->feature_number[0]].y;
			success = true;
		} else if (action->feature_property == BODY_ANGLE_Z) {
			filter->joint_angles.array[action->feature_number[0]].z;
			success = true;
		} else if (action->feature_number[1] >=
			   (int32_t)filter->keypoints.num) {
		} else if (action->feature_property == BODY_2D_DISTANCE) {
			float x = filter->keypoints
					  .array[action->feature_number[0]]
					  .x -
				  filter->keypoints
					  .array[action->feature_number[1]]
					  .x;
			float y = filter->keypoints
					  .array[action->feature_number[0]]
					  .y -
				  filter->keypoints
					  .array[action->feature_number[1]]
					  .y;
			value = sqrtf(x * x + y * y);
			success = true;
		} else if (action->feature_property == BODY_2D_ROT) {
			value = DEG(atan2f(
				(filter->keypoints
					 .array[action->feature_number[1]]
					 .y -
				 filter->keypoints
					 .array[action->feature_number[0]]
					 .y),
				(filter->keypoints
					 .array[action->feature_number[1]]
					 .x -
				 filter->keypoints
					 .array[action->feature_number[0]]
					 .x)));
			success = true;
		} else if (action->feature_property == BODY_2D_DIFF_X) {
			value = filter->keypoints
					.array[action->feature_number[1]]
					.x -
				filter->keypoints
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property == BODY_2D_DIFF_Y) {
			value = filter->keypoints
					.array[action->feature_number[1]]
					.y -
				filter->keypoints
					.array[action->feature_number[0]]
					.y;
			success = true;
		} else if (action->feature_property == BODY_3D_DISTANCE) {
			float x = filter->keypoints3D
					  .array[action->feature_number[0]]
					  .x -
				  filter->keypoints3D
					  .array[action->feature_number[1]]
					  .x;
			float y = filter->keypoints3D
					  .array[action->feature_number[0]]
					  .y -
				  filter->keypoints3D
					  .array[action->feature_number[1]]
					  .y;
			float z = filter->keypoints3D
					  .array[action->feature_number[0]]
					  .z -
				  filter->keypoints3D
					  .array[action->feature_number[1]]
					  .z;
			value = sqrtf(x * x + y * y);
			value = sqrtf(value * value + z * z);
			success = true;
		} else if (action->feature_property == BODY_3D_DIFF_X) {
			value = filter->keypoints3D
					.array[action->feature_number[1]]
					.x -
				filter->keypoints3D
					.array[action->feature_number[0]]
					.x;
			success = true;
		} else if (action->feature_property == BODY_3D_DIFF_Y) {
			value = filter->keypoints3D
					.array[action->feature_number[1]]
					.y -
				filter->keypoints3D
					.array[action->feature_number[0]]
					.y;
			success = true;
		} else if (action->feature_property == BODY_3D_DIFF_Z) {
			value = filter->keypoints3D
					.array[action->feature_number[1]]
					.z -
				filter->keypoints3D
					.array[action->feature_number[0]]
					.z;
			success = true;
		}
	}
	if (action->feature != FEATURE_EXPRESSION)
		value *= action->factor;
	value += action->diff;
	if (success && easing) {
		value = action->previous_float * action->easing +
			value * (1.0f - action->easing);
		action->previous_float = value;
	}
	*v = value;
	return success;
}

static bool nv_move_action_get_vec2(struct nvidia_move_info *filter,
				    struct nvidia_move_action *action,
				    bool easing, struct vec2 *value)
{
	bool success = false;
	value->x = 0.0f;
	value->y = 0.0f;
	if (action->feature == FEATURE_BOUNDINGBOX &&
	    filter->bboxes.max_boxes && filter->bboxes.num_boxes) {
		if (filter->bboxes_confidence.array &&
		    filter->bboxes_confidence.array[0] <
			    action->required_confidence) {
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_TOP_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_RIGHT) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_BOTTOM_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER_LEFT) {
			value->x = filter->bboxes.boxes[0].x;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_CENTER) {
			value->x = filter->bboxes.boxes[0].x +
				   filter->bboxes.boxes[0].width / 2.0f;
			value->y = filter->bboxes.boxes[0].y +
				   filter->bboxes.boxes[0].height / 2.0f;
			success = true;
		} else if (action->feature_property ==
			   FEATURE_BOUNDINGBOX_SIZE) {
			value->x = filter->bboxes.boxes[0].width;
			value->y = filter->bboxes.boxes[0].height;
			success = true;
		}
	} else if (action->feature == FEATURE_LANDMARK &&
		   filter->landmarks.num) {
		if (action->feature_number[0] >=
		    (int32_t)filter->landmarks.num) {
		} else if (filter->landmarks_confidence
				   .array[action->feature_number[0]] <
			   action->required_confidence) {
		} else if (action->feature_property == FEATURE_LANDMARK_POS) {
			value->x = filter->landmarks
					   .array[action->feature_number[0]]
					   .x;
			value->y = filter->landmarks
					   .array[action->feature_number[0]]
					   .y;
			success = true;
		} else if (action->feature_number[1] >=
			   (int32_t)filter->landmarks.num) {
		} else if (action->feature_property == FEATURE_LANDMARK_DIFF) {
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
			success = true;
		}
	} else if (action->feature == FEATURE_GAZE) {
		if (action->feature_property == FEATURE_GAZE_VECTOR) {
			value->x = DEG(filter->gaze_angles_vector[0]);
			value->y = DEG(filter->gaze_angles_vector[1]);
			success = true;
		}
	} else if (action->feature == FEATURE_EXPRESSION &&
		   filter->expressions.num) {
		if (action->feature_property == FEATURE_EXPRESSION_VECTOR) {
			success =
				get_expression_value(filter,
						     action->feature_number[0],
						     &value->x) &&
				get_expression_value(filter,
						     action->feature_number[1],
						     &value->y);
		}
	} else if (action->feature == FEATURE_BODY && filter->keypoints.num) {
		if (action->feature_number[0] >=
		    (int32_t)filter->keypoints.num) {
		} else if (filter->keypoints_confidence
				   .array[action->feature_number[0]] <
			   action->required_confidence) {
		} else if (action->feature_property == BODY_2D_POS) {
			value->x = filter->keypoints
					   .array[action->feature_number[0]]
					   .x;
			value->y = filter->keypoints
					   .array[action->feature_number[0]]
					   .y;
			success = true;
		} else if (action->feature_number[1] >=
			   (int32_t)filter->keypoints.num) {
		} else if (filter->keypoints_confidence
				   .array[action->feature_number[1]] <
			   action->required_confidence) {
		} else if (action->feature_property == BODY_2D_DIFF) {
			value->x = filter->keypoints
					   .array[action->feature_number[1]]
					   .x -
				   filter->keypoints
					   .array[action->feature_number[0]]
					   .x;
			value->y = filter->keypoints
					   .array[action->feature_number[1]]
					   .y -
				   filter->keypoints
					   .array[action->feature_number[0]]
					   .y;
			success = true;
		}
	}
	value->x *= action->factor;
	value->x += action->diff;
	value->y *= action->factor2;
	value->y += action->diff2;

	if (success && easing) {
		value->x = action->previous_vec2.x * action->easing +
			   value->x * (1.0f - action->easing);
		action->previous_vec2.x = value->x;
		value->y = action->previous_vec2.y * action->easing +
			   value->y * (1.0f - action->easing);
		action->previous_vec2.y = value->y;
	}
	return success;
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

	uint64_t feature_flags = 0;
	struct dstr name = {0};
	for (size_t i = 1; i <= actions; i++) {
		struct nvidia_move_action *action =
			filter->actions.array + i - 1;
		dstr_printf(&name, "action_%lld_disabled", i);
		action->disabled = obs_data_get_bool(settings, name.array);
		dstr_printf(&name, "action_%lld_action", i);
		action->action =
			(uint32_t)obs_data_get_int(settings, name.array);
		obs_weak_source_release(action->target);
		action->target = NULL;
		bfree(action->name);
		action->name = NULL;
		if (action->action == ACTION_MOVE_SOURCE ||
		    action->action == ACTION_SOURCE_VISIBILITY ||
		    action->action == ACTION_ATTACH_SOURCE) {
			dstr_printf(&name, "action_%lld_scene", i);
			const char *scene_name =
				obs_data_get_string(settings, name.array);
			obs_source_t *source =
				obs_get_source_by_name(scene_name);
			if (source) {
				if (obs_source_is_scene(source) ||
				    obs_source_is_group(source))
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
		} else if (action->action == ACTION_ATTACH_SOURCE) {
			dstr_printf(&name, "action_%lld_attach", i);
			action->property = (uint32_t)obs_data_get_int(
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

		if (action->action == ACTION_ATTACH_SOURCE) {
			action->feature = FEATURE_LANDMARK;
		} else {
			dstr_printf(&name, "action_%lld_feature", i);
			action->feature = (uint32_t)obs_data_get_int(
				settings, name.array);
		}

		feature_flags |= (1ull << action->feature);

		if (action->feature == FEATURE_BOUNDINGBOX) {
			dstr_printf(&name, "action_%lld_bounding_box", i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);
			dstr_printf(&name, "action_%lld_required_confidence",
				    i);
			action->required_confidence =
				(float)obs_data_get_double(settings,
							   name.array);
		} else if (action->feature == FEATURE_LANDMARK) {
			dstr_printf(&name, "action_%lld_landmark", i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);

			dstr_printf(&name, "action_%lld_landmark_1", i);
			if (obs_data_get_int(settings, name.array))
				action->feature_number[0] =
					(int32_t)obs_data_get_int(settings,
								  name.array) -
					1;
			dstr_printf(&name, "action_%lld_landmark_2", i);
			if (obs_data_get_int(settings, name.array))
				action->feature_number[1] =
					(int32_t)obs_data_get_int(settings,
								  name.array) -
					1;
			dstr_printf(&name, "action_%lld_required_confidence",
				    i);
			action->required_confidence =
				(float)obs_data_get_double(settings,
							   name.array);
		} else if (action->feature == FEATURE_POSE) {
			dstr_printf(&name, "action_%lld_pose", i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);
		} else if (action->feature == FEATURE_EXPRESSION) {
			dstr_printf(&name, "action_%lld_expression_property",
				    i);
			action->feature_property = (uint32_t)obs_data_get_int(
				settings, name.array);

			dstr_printf(&name, "action_%lld_expression", i);
			long long old = obs_data_get_int(settings, name.array);
			if (old) {
				obs_data_unset_user_value(settings, name.array);
				dstr_printf(&name, "action_%lld_expression_1",
					    i);
				obs_data_set_int(settings, name.array, old);
			}
			dstr_printf(&name, "action_%lld_expression_1", i);
			action->feature_number[0] =
				(int32_t)obs_data_get_int(settings,
							  name.array) -
				1;
			dstr_printf(&name, "action_%lld_expression_2", i);
			action->feature_number[1] =
				(int32_t)obs_data_get_int(settings,
							  name.array) -
				1;
		} else if (action->feature == FEATURE_GAZE) {
			dstr_printf(&name, "action_%lld_gaze", i);
			action->feature_property =
				(int32_t)obs_data_get_int(settings, name.array);
		} else if (action->feature == FEATURE_BODY) {
			dstr_printf(&name, "action_%lld_body", i);
			action->feature_property =
				(int32_t)obs_data_get_int(settings, name.array);

			dstr_printf(&name, "action_%lld_body_1", i);
			action->feature_number[0] =
				(int32_t)obs_data_get_int(settings, name.array);
			dstr_printf(&name, "action_%lld_body_2", i);
			action->feature_number[1] =
				(int32_t)obs_data_get_int(settings, name.array);
			dstr_printf(&name, "action_%lld_required_confidence",
				    i);
			action->required_confidence =
				(float)obs_data_get_double(settings,
							   name.array);
		}
		dstr_printf(&name, "action_%lld_factor2", i);
		obs_data_set_default_double(settings, name.array, 100.0f);
		dstr_printf(&name, "action_%lld_factor", i);
		obs_data_set_default_double(settings, name.array, 100.0f);

		action->factor =
			(float)obs_data_get_double(settings, name.array) /
			100.0f;
		dstr_printf(&name, "action_%lld_factor2", i);
		action->factor2 =
			(float)obs_data_get_double(settings, name.array) /
			100.0f;
		dstr_printf(&name, "action_%lld_diff", i);
		action->diff = (float)obs_data_get_double(settings, name.array);
		dstr_printf(&name, "action_%lld_diff2", i);
		action->diff2 =
			(float)obs_data_get_double(settings, name.array);

		dstr_printf(&name, "action_%lld_easing", i);
		action->easing = ExponentialEaseOut(
			(float)obs_data_get_double(settings, name.array) /
			100.0f);
		if (action->action != ACTION_ATTACH_SOURCE) {
			nv_move_action_get_float(filter, action, false,
						 &action->previous_float);
			nv_move_action_get_vec2(filter, action, false,
						&action->previous_vec2);
		}
	}
	dstr_free(&name);

	bfree(filter->last_error);
	filter->last_error = NULL;

	if (feature_flags & (1ull << FEATURE_BODY)) {
		if (!filter->bodyHandle) {
			if (nv_move_log_error(
				    filter,
				    NvAR_Create(NvAR_Feature_BodyPoseEstimation,
						&filter->bodyHandle),
				    "Create body"))
				return;
			nv_move_feature_handle(filter, filter->bodyHandle);
			unsigned int numKeyPoints = 0;
			NvCV_Status nvErr =
				NvAR_GetU32(filter->bodyHandle,
					    NvAR_Parameter_Config(NumKeyPoints),
					    &numKeyPoints);

			if (nvErr == NVCV_SUCCESS) {
				da_resize(filter->keypoints, numKeyPoints);

				nvErr = NvAR_SetObject(
					filter->bodyHandle,
					NvAR_Parameter_Output(KeyPoints),
					filter->keypoints.array,
					sizeof(NvAR_Point2f));

				da_resize(filter->keypoints3D, numKeyPoints);

				nvErr = NvAR_SetObject(
					filter->bodyHandle,
					NvAR_Parameter_Output(KeyPoints3D),
					filter->keypoints3D.array,
					sizeof(NvAR_Point3f));

				da_resize(filter->joint_angles, numKeyPoints);

				nvErr = NvAR_SetObject(
					filter->bodyHandle,
					NvAR_Parameter_Output(JointAngles),
					filter->joint_angles.array,
					sizeof(NvAR_Quaternion));

				da_resize(filter->keypoints_confidence,
					  numKeyPoints);

				nvErr = NvAR_SetF32Array(
					filter->bodyHandle,
					NvAR_Parameter_Output(
						KeyPointsConfidence),
					filter->keypoints_confidence.array,
					sizeof(float));
			} else {
				nv_move_log_error(filter, nvErr,
						  "NumKeyPoints");
			}
			if (nv_move_log_error(filter,
					      NvAR_Load(filter->bodyHandle),
					      "Load body"))
				return;
		}
	} else if (filter->bodyHandle) {
		nv_move_log_error(filter, NvAR_Destroy(filter->bodyHandle),
				  "Destroy body");
		filter->bodyHandle = NULL;
	}
	if (feature_flags & (1ull << FEATURE_GAZE)) {
		if (!filter->gazeHandle) {
			if (nv_move_log_error(
				    filter,
				    NvAR_Create(NvAR_Feature_GazeRedirection,
						&filter->gazeHandle),
				    "Create gaze"))
				return;
			nv_move_feature_handle(filter, filter->gazeHandle);
			nv_move_landmarks(filter, filter->gazeHandle);
			NvAR_SetObject(filter->gazeHandle,
				       NvAR_Parameter_Output(HeadPose),
				       &filter->pose, sizeof(NvAR_Quaternion));
			NvAR_SetF32Array(
				filter->gazeHandle,
				NvAR_Parameter_Output(OutputGazeVector),
				filter->gaze_angles_vector, 2);

			NvAR_SetF32Array(
				filter->gazeHandle,
				NvAR_Parameter_Output(OutputHeadTranslation),
				filter->head_translation, 3);

			NvAR_SetObject(filter->gazeHandle,
				       NvAR_Parameter_Output(GazeDirection),
				       &filter->gaze_direction,
				       sizeof(NvAR_Point3f));

			da_resize(filter->gaze_output_landmarks,
				  filter->landmarks.num);

			NvAR_SetObject(
				filter->gazeHandle,
				NvAR_Parameter_Output(GazeOutputLandmarks),
				filter->gaze_output_landmarks.array,
				sizeof(NvAR_Point2f));

			NvAR_SetU32(filter->gazeHandle,
				    NvAR_Parameter_Config(Temporal), -1);

			NvAR_SetU32(filter->gazeHandle,
				    NvAR_Parameter_Config(GazeRedirect), false);

			NvAR_SetU32(filter->gazeHandle,
				    NvAR_Parameter_Config(UseCudaGraph), true);

			NvAR_SetU32(filter->gazeHandle,
				    NvAR_Parameter_Config(EyeSizeSensitivity),
				    3);

			if (nv_move_log_error(filter,
					      NvAR_Load(filter->gazeHandle),
					      "Load gaze"))
				return;
		}
	} else if (filter->gazeHandle) {
		nv_move_log_error(filter, NvAR_Destroy(filter->gazeHandle),
				  "Destroy gaze");
		filter->gazeHandle = NULL;
	}
	if (feature_flags & (1ull << FEATURE_EXPRESSION)) {
		if (!filter->expressionHandle) {
			if (nv_move_log_error(
				    filter,
				    NvAR_Create(NvAR_Feature_FaceExpressions,
						&filter->expressionHandle),
				    "Create expression"))
				return;
			nv_move_feature_handle(filter,
					       filter->expressionHandle);
			nv_move_landmarks(filter, filter->expressionHandle);
			NvAR_SetObject(filter->expressionHandle,
				       NvAR_Parameter_Output(Pose),
				       &filter->pose, sizeof(NvAR_Quaternion));
			uint32_t expressionCount = 0;
			NvCV_Status nvErr = NvAR_GetU32(
				filter->expressionHandle,
				NvAR_Parameter_Config(ExpressionCount),
				&expressionCount);
			if (nvErr == NVCV_SUCCESS) {
				da_resize(filter->expressions, expressionCount);

				nvErr = NvAR_SetF32Array(
					filter->expressionHandle,
					NvAR_Parameter_Output(
						ExpressionCoefficients),
					filter->expressions.array,
					expressionCount);
			}

			if (nv_move_log_error(
				    filter, NvAR_Load(filter->expressionHandle),
				    "Load expression"))
				return;
		}
	} else if (filter->expressionHandle) {
		nv_move_log_error(filter,
				  NvAR_Destroy(filter->expressionHandle),
				  "Destroy expression");
		filter->expressionHandle = NULL;
	}

	if ((feature_flags & (1ull << FEATURE_BOUNDINGBOX))) {
		if (!filter->bboxes_confidence.num)
			da_resize(filter->bboxes_confidence, BBOXES_COUNT);
		if (filter->bodyHandle) {
			NvAR_SetF32Array(
				filter->bodyHandle,
				NvAR_Parameter_Output(BoundingBoxesConfidence),
				filter->bboxes_confidence.array, BBOXES_COUNT);
		} else if (filter->gazeHandle) {
			NvAR_SetF32Array(
				filter->gazeHandle,
				NvAR_Parameter_Output(BoundingBoxesConfidence),
				filter->bboxes_confidence.array, BBOXES_COUNT);
		} else if (filter->expressionHandle) {
			NvAR_SetF32Array(
				filter->expressionHandle,
				NvAR_Parameter_Output(BoundingBoxesConfidence),
				filter->bboxes_confidence.array, BBOXES_COUNT);
		} else {
			if (!filter->faceDetectHandle) {
				if (nv_move_log_error(
					    filter,
					    NvAR_Create(
						    NvAR_Feature_FaceBoxDetection,
						    &filter->faceDetectHandle),
					    "Create faceDetect"))
					return;
				nv_move_feature_handle(
					filter, filter->faceDetectHandle);
				NvAR_SetF32Array(
					filter->faceDetectHandle,
					NvAR_Parameter_Output(
						BoundingBoxesConfidence),
					filter->bboxes_confidence.array,
					BBOXES_COUNT);

				if (nv_move_log_error(
					    filter,
					    NvAR_Load(filter->faceDetectHandle),
					    "Load faceDetect"))
					return;
			}
		}
	} else if (filter->faceDetectHandle) {
		nv_move_log_error(filter,
				  NvAR_Destroy(filter->faceDetectHandle),
				  "Destroy faceDetect");
		filter->faceDetectHandle = NULL;
	}

	if ((((feature_flags & (1ull << FEATURE_LANDMARK)) ||
	      (feature_flags & (1ull << FEATURE_POSE))) &&
	     !filter->gazeHandle && !filter->expressionHandle)) {
		if (!filter->landmarkHandle) {
			if (nv_move_log_error(
				    filter,
				    NvAR_Create(NvAR_Feature_LandmarkDetection,
						&filter->landmarkHandle),
				    "Create landmark"))
				return;

			nv_move_feature_handle(filter, filter->landmarkHandle);
			nv_move_landmarks(filter, filter->landmarkHandle);
			NvAR_SetObject(filter->landmarkHandle,
				       NvAR_Parameter_Output(Pose),
				       &filter->pose, sizeof(NvAR_Quaternion));
			if (nv_move_log_error(filter,
					      NvAR_Load(filter->landmarkHandle),
					      "Load expression"))
				return;
		}
	} else if (filter->landmarkHandle) {
		nv_move_log_error(filter, NvAR_Destroy(filter->landmarkHandle),
				  "Destroy landmark");
		filter->landmarkHandle = NULL;
	}
}

static void nv_move_actual_destroy(void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	if (filter->faceDetectHandle)
		nv_move_log_error(filter,
				  NvAR_Destroy(filter->faceDetectHandle),
				  "Destroy faceDetect");
	if (filter->landmarkHandle)
		nv_move_log_error(filter, NvAR_Destroy(filter->landmarkHandle),
				  "Destroy landmark");
	if (filter->expressionHandle)
		nv_move_log_error(filter,
				  NvAR_Destroy(filter->expressionHandle),
				  "Destroy expression");
	if (filter->gazeHandle)
		nv_move_log_error(filter, NvAR_Destroy(filter->gazeHandle),
				  "Destroy gaze");
	if (filter->bodyHandle)
		nv_move_log_error(filter, NvAR_Destroy(filter->bodyHandle),
				  "Destroy body");

	if (filter->stream)
		nv_move_log_error(filter,
				  NvAR_CudaStreamDestroy(filter->stream),
				  "Destroy Cuda Stream");

	bfree(filter->bboxes.boxes);

	da_free(filter->bboxes_confidence);
	da_free(filter->landmarks_confidence);
	da_free(filter->landmarks);
	da_free(filter->expressions);
	da_free(filter->gaze_output_landmarks);
	da_free(filter->keypoints_confidence);
	da_free(filter->keypoints);
	da_free(filter->keypoints3D);
	da_free(filter->joint_angles);

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
	for (size_t i = 0; i < filter->actions.num; i++) {
		struct nvidia_move_action *action = filter->actions.array + i;
		obs_weak_source_release(action->target);
		action->target = NULL;
		bfree(action->name);
		action->name = NULL;
	}
	da_free(filter->actions);
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

bool nv_move_expression_changed(void *priv, obs_properties_t *props,
				obs_property_t *property, obs_data_t *settings)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)priv;
	const char *action_prop = obs_property_name(property);
	long long expression = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_expression_property",
		   &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_feature", action_number);
	bool visible = obs_data_get_int(settings, name.array) ==
		       FEATURE_EXPRESSION;
	bool changed = false;
	dstr_printf(&name, "action_%lld_expression_1", action_number);
	obs_property_t *expression1 = obs_properties_get(props, name.array);
	if (obs_property_visible(expression1) != visible) {
		obs_property_set_visible(expression1, visible);
		changed = true;
	}

	dstr_printf(&name, "action_%lld_expression_2", action_number);
	obs_property_t *expression2 = obs_properties_get(props, name.array);
	if (obs_property_visible(expression2) !=
	    (visible && expression != FEATURE_EXPRESSION_SINGLE)) {
		obs_property_set_visible(
			expression2,
			visible && expression != FEATURE_EXPRESSION_SINGLE);
		changed = true;
	}
	return changed;
}

bool nv_move_landmark_changed(void *priv, obs_properties_t *props,
			      obs_property_t *property, obs_data_t *settings)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)priv;
	const char *action_prop = obs_property_name(property);
	long long landmark = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_landmark", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_action", action_number);
	long long action = obs_data_get_int(settings, name.array);
	dstr_printf(&name, "action_%lld_feature", action_number);
	bool visible =
		(obs_data_get_int(settings, name.array) == FEATURE_LANDMARK &&
		 action != ACTION_ATTACH_SOURCE);

	bool changed = false;

	dstr_printf(&name, "action_%lld_landmark_1", action_number);
	obs_property_t *landmark1 = obs_properties_get(props, name.array);
	if (obs_property_visible(landmark1) != visible) {
		obs_property_set_visible(landmark1, visible);
		changed = true;
	}
	obs_property_int_set_limits(landmark1, 1, (int)filter->landmarks.num,
				    1);
	dstr_printf(&name, "action_%lld_landmark_2", action_number);
	obs_property_t *landmark2 = obs_properties_get(props, name.array);
	if (obs_property_visible(landmark2) !=
	    (visible && landmark != FEATURE_LANDMARK_POS &&
	     landmark >= FEATURE_LANDMARK_DISTANCE)) {
		obs_property_set_visible(
			landmark2,
			visible && landmark != FEATURE_LANDMARK_POS &&
				landmark >= FEATURE_LANDMARK_DISTANCE);
		changed = true;
	}
	obs_property_int_set_limits(landmark2, 1, (int)filter->landmarks.num,
				    1);
	dstr_free(&name);
	return changed;
}

bool nv_move_body_changed(void *priv, obs_properties_t *props,
			  obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	long long body = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_body", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_feature", action_number);
	bool visible = obs_data_get_int(settings, name.array) == FEATURE_BODY;
	bool changed = false;
	dstr_printf(&name, "action_%lld_body_1", action_number);
	obs_property_t *body1 = obs_properties_get(props, name.array);
	if (obs_property_visible(body1) != visible) {
		obs_property_set_visible(body1, visible);
		changed = true;
	}
	dstr_printf(&name, "action_%lld_body_2", action_number);
	obs_property_t *body2 = obs_properties_get(props, name.array);
	bool v2 = (visible &&
		   (body == BODY_2D_DIFF || body == BODY_2D_DISTANCE ||
		    body == BODY_2D_ROT || body == BODY_2D_DIFF_X ||
		    body == BODY_2D_DIFF_Y || body == BODY_3D_DISTANCE ||
		    body == BODY_3D_DIFF_X || body == BODY_3D_DIFF_Y ||
		    body == BODY_3D_DIFF_Z || body == BODY_3D_DIFF));
	if (obs_property_visible(body2) != v2) {
		obs_property_set_visible(body2, v2);
		changed = true;
	}
	dstr_free(&name);
	return changed;
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
	dstr_printf(&name, "action_%lld_action", action_number);
	long long action = obs_data_get_int(settings, name.array);

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
	dstr_printf(&name, "action_%lld_pose", action_number);
	obs_property_t *pose = obs_properties_get(props, name.array);
	obs_property_set_visible(pose, false);
	dstr_printf(&name, "action_%lld_expression_property", action_number);
	obs_property_t *expression = obs_properties_get(props, name.array);
	obs_property_set_visible(expression, false);
	dstr_printf(&name, "action_%lld_expression_1", action_number);
	obs_property_t *expression1 = obs_properties_get(props, name.array);
	obs_property_set_visible(expression1, false);
	dstr_printf(&name, "action_%lld_expression_2", action_number);
	obs_property_t *expression2 = obs_properties_get(props, name.array);
	obs_property_set_visible(expression2, false);
	dstr_printf(&name, "action_%lld_gaze", action_number);
	obs_property_t *gaze = obs_properties_get(props, name.array);
	obs_property_set_visible(gaze, false);
	dstr_printf(&name, "action_%lld_body", action_number);
	obs_property_t *body = obs_properties_get(props, name.array);
	obs_property_set_visible(body, false);
	dstr_printf(&name, "action_%lld_body_1", action_number);
	obs_property_t *body1 = obs_properties_get(props, name.array);
	obs_property_set_visible(body1, false);
	dstr_printf(&name, "action_%lld_body_2", action_number);
	obs_property_t *body2 = obs_properties_get(props, name.array);
	obs_property_set_visible(body2, false);
	dstr_printf(&name, "action_%lld_required_confidence", action_number);
	obs_property_t *required_confidence =
		obs_properties_get(props, name.array);
	obs_property_set_visible(required_confidence, false);

	if (action == ACTION_ATTACH_SOURCE) {
		obs_property_set_visible(required_confidence, true);
	} else if (feature == FEATURE_BOUNDINGBOX) {
		obs_property_set_visible(bounding_box, true);
		obs_property_set_visible(required_confidence, true);
	} else if (feature == FEATURE_LANDMARK) {
		obs_property_set_visible(required_confidence, true);
		obs_property_set_visible(landmark, true);
	} else if (feature == FEATURE_POSE) {
		obs_property_set_visible(pose, true);
	} else if (feature == FEATURE_EXPRESSION) {
		obs_property_set_visible(expression, true);
	} else if (feature == FEATURE_GAZE) {
		obs_property_set_visible(gaze, true);
	} else if (feature == FEATURE_BODY) {
		obs_property_set_visible(body, true);
		obs_property_set_visible(required_confidence, true);
	}
	nv_move_expression_changed(priv, props, expression, settings);
	nv_move_body_changed(priv, props, body, settings);
	nv_move_landmark_changed(priv, props, landmark, settings);
	dstr_free(&name);
	return true;
}

bool nv_move_actions_changed(void *priv, obs_properties_t *props,
			     obs_property_t *property, obs_data_t *settings)
{
	struct dstr name = {0};
	long long actions = obs_data_get_int(settings, "actions");
	bool changed = false;
	obs_property_t *show = obs_properties_get(props, "show");
	long long f = obs_data_get_int(settings, "show");
	if (obs_property_list_item_disabled(show, 0) != (actions > 10))
		obs_property_list_item_disable(show, 0, actions > 10);
	if (!f && actions > 10) {
		f = 1;
	}
	for (long long i = 1; i <= MAX_ACTIONS; i++) {
		dstr_printf(&name, "action_%lld_group", i);
		obs_property_t *group = obs_properties_get(props, name.array);

		if (i > actions) {
			obs_property_list_item_remove(show, (size_t)i);
		} else {
			const char *od =
				obs_property_list_item_name(show, (size_t)i);
			dstr_printf(&name, "action_%lld_disabled", i);
			bool disabled = obs_data_get_bool(settings, name.array);
			dstr_printf(&name, "action_%lld_description", i);
			const char *nd =
				obs_data_get_string(settings, name.array);
			if (strlen(nd)) {
				dstr_copy(&name, nd);
			} else {
				dstr_printf(&name, "%s %lld",
					    obs_module_text("Action"), i);
			}
			if (disabled) {
				dstr_cat(&name, " (");
				dstr_cat(&name, obs_module_text("Disabled"));
				dstr_cat(&name, ")");
			}
			if (!od || strcmp(od, name.array) != 0) {
				obs_property_list_item_remove(show, (size_t)i);
				obs_property_list_insert_int(show, (size_t)i,
							     name.array, i);
				changed = true;
			}
		}
		bool visible = (i <= actions) && (f > 0 ? i == f : true);
		if (obs_property_visible(group) == visible)
			continue;
		obs_property_set_visible(group, visible);
		changed = true;
	}
	dstr_free(&name);
	return changed;
}

static void nv_move_prop_number_floats(uint32_t number, long long action_number,
				       obs_properties_t *props)
{
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_landmark", action_number);
	obs_property_t *landmark = obs_properties_get(props, name.array);

	for (size_t i = FEATURE_LANDMARK_X; i <= FEATURE_LANDMARK_ROT; i++) {
		obs_property_list_item_disable(landmark, i, number != 1);
	}
	for (size_t i = FEATURE_LANDMARK_DIFF; i <= FEATURE_LANDMARK_POS; i++) {
		obs_property_list_item_disable(landmark, i, number != 2);
	}

	dstr_printf(&name, "action_%lld_expression_property", action_number);
	obs_property_t *expression = obs_properties_get(props, name.array);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_SINGLE,
				       number != 1);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_VECTOR,
				       number != 2);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_ADD,
				       number != 1);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_SUBSTRACT,
				       number != 1);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_DISTANCE,
				       number != 1);
	obs_property_list_item_disable(expression, FEATURE_EXPRESSION_AVG,
				       number != 1);

	dstr_printf(&name, "action_%lld_bounding_box", action_number);
	obs_property_t *bbox = obs_properties_get(props, name.array);
	for (size_t i = FEATURE_BOUNDINGBOX_LEFT;
	     i <= FEATURE_BOUNDINGBOX_HEIGHT; i++) {
		obs_property_list_item_disable(bbox, i, number != 1);
	}
	for (size_t i = FEATURE_BOUNDINGBOX_TOP_LEFT;
	     i <= FEATURE_BOUNDINGBOX_SIZE; i++) {
		obs_property_list_item_disable(bbox, i, number != 2);
	}
	dstr_printf(&name, "action_%lld_pose", action_number);
	obs_property_t *pose = obs_properties_get(props, name.array);
	for (size_t i = FEATURE_POSE_X; i <= FEATURE_POSE_W; i++) {
		obs_property_list_item_disable(pose, i, number != 1);
	}

	dstr_printf(&name, "action_%lld_gaze", action_number);
	obs_property_t *gaze = obs_properties_get(props, name.array);

	obs_property_list_item_disable(gaze, FEATURE_GAZE_VECTOR, number != 2);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_VECTOR_PITCH,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_VECTOR_YAW,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_HEADTRANSLATION,
				       number != 3);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_HEADTRANSLATION_X,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_HEADTRANSLATION_Y,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_HEADTRANSLATION_Z,
				       number != 1);
	obs_property_list_item_disable(
		gaze, FEATURE_GAZE_DIRECTION_CENTER_POINT, number != 3);
	obs_property_list_item_disable(
		gaze, FEATURE_GAZE_DIRECTION_CENTER_POINT_X, number != 1);
	obs_property_list_item_disable(
		gaze, FEATURE_GAZE_DIRECTION_CENTER_POINT_Y, number != 1);
	obs_property_list_item_disable(
		gaze, FEATURE_GAZE_DIRECTION_CENTER_POINT_Z, number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_DIRECTION_VECTOR,
				       number != 3);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_DIRECTION_VECTOR_X,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_DIRECTION_VECTOR_Y,
				       number != 1);
	obs_property_list_item_disable(gaze, FEATURE_GAZE_DIRECTION_VECTOR_Z,
				       number != 1);

	dstr_printf(&name, "action_%lld_body", action_number);
	obs_property_t *body = obs_properties_get(props, name.array);
	for (size_t i = BODY_CONFIDENCE; i <= BODY_2D_DIFF_Y; i++) {
		obs_property_list_item_disable(body, i, number != 1);
	}
	obs_property_list_item_disable(body, BODY_2D_DIFF, number != 2);
	obs_property_list_item_disable(body, BODY_2D_POS, number != 2);
	for (size_t i = BODY_3D_POSX; i <= BODY_3D_DIFF_Z; i++) {
		obs_property_list_item_disable(body, i, number != 1);
	}
	obs_property_list_item_disable(body, BODY_3D_POS, number != 3);
	obs_property_list_item_disable(body, BODY_3D_DIFF, number != 3);
	obs_property_list_item_disable(body, BODY_ANGLE, number != 3);
	obs_property_list_item_disable(body, BODY_ANGLE_X, number != 1);
	obs_property_list_item_disable(body, BODY_ANGLE_Y, number != 1);
	obs_property_list_item_disable(body, BODY_ANGLE_Z, number != 1);
	dstr_free(&name);
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
	dstr_printf(&name, "action_%lld_attach", action_number);
	obs_property_t *attach = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_enable", action_number);
	obs_property_t *enable = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_threshold", action_number);
	obs_property_t *threshold = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_property", action_number);
	obs_property_t *p = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_feature", action_number);
	obs_property_t *feature = obs_properties_get(props, name.array);
	dstr_printf(&name, "action_%lld_get_value", action_number);
	obs_property_t *get_value = obs_properties_get(props, name.array);

	obs_property_set_visible(scene, false);
	obs_property_set_visible(sceneitem, false);
	obs_property_set_visible(source, false);
	obs_property_set_visible(filter, false);
	obs_property_set_visible(sceneitem_property, false);
	obs_property_set_visible(attach, false);
	obs_property_set_visible(enable, false);
	obs_property_set_visible(threshold, false);
	obs_property_set_visible(p, false);
	obs_property_set_visible(feature, true);
	obs_property_set_visible(get_value, true);

	if (action == ACTION_MOVE_SOURCE) {
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
		obs_property_set_visible(sceneitem_property, true);
	} else if (action == ACTION_MOVE_VALUE) {
		obs_property_set_visible(source, true);
		obs_property_set_visible(filter, true);
		obs_property_set_visible(p, true);
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
	} else if (action == ACTION_ATTACH_SOURCE) {
		obs_property_set_visible(attach, true);
		obs_property_set_visible(scene, true);
		obs_property_set_visible(sceneitem, true);
		obs_property_set_visible(feature, false);
		obs_property_set_visible(get_value, false);
		dstr_printf(&name, "action_%lld_feature", action_number);
		if (obs_data_get_int(settings, name.array) !=
		    FEATURE_LANDMARK) {
			obs_data_set_int(settings, name.array,
					 FEATURE_LANDMARK);
			nv_move_feature_changed(priv, props, feature, settings);
		}
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
	return true;
}

bool nv_move_attach_changed(void *priv, obs_properties_t *props,
			    obs_property_t *property, obs_data_t *settings)
{
	const char *action_prop = obs_property_name(property);
	long long attach = obs_data_get_int(settings, action_prop);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_attach", &action_number) != 1 ||
	    !action_number)
		return false;
	struct dstr name = {0};
	dstr_printf(&name, "action_%lld_action", action_number);
	if (obs_data_get_int(settings, name.array) != ACTION_ATTACH_SOURCE) {
		dstr_free(&name);
		return false;
	}

	dstr_printf(&name, "action_%lld_scene", action_number);
	obs_source_t *scene_source = obs_get_source_by_name(
		obs_data_get_string(settings, name.array));
	if (!scene_source) {
		dstr_free(&name);
		return false;
	}
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		scene = obs_group_from_source(scene_source);
	obs_source_release(scene_source);
	if (!scene) {
		dstr_free(&name);
		return false;
	}

	dstr_printf(&name, "action_%lld_sceneitem", action_number);
	obs_sceneitem_t *item = obs_scene_find_source(
		scene, obs_data_get_string(settings, name.array));
	dstr_free(&name);
	if (!item)
		return false;

	if (attach == ATTACH_LEFT_EAR) {
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT);
	} else if (attach == ATTACH_RIGHT_EAR) {
		obs_sceneitem_set_alignment(item, OBS_ALIGN_RIGHT);
	} else if (attach == ATTACH_NOSE || attach == ATTACH_UPPER_LIP ||
		   attach == ATTACH_FOREHEAD) {
		obs_sceneitem_set_alignment(item, OBS_ALIGN_BOTTOM);
	} else if (attach == ATTACH_LOWER_LIP || attach == ATTACH_JAW) {
		obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP);
	} else {
		obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	}
	return false;
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
	dstr_free(&name);
	if (!sceneitem)
		return false;

	obs_property_list_clear(sceneitem);
	obs_property_list_add_string(sceneitem, "", "");

	obs_source_t *scene_source = obs_get_source_by_name(scene_name);
	if (!scene_source)
		return true;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		scene = obs_group_from_source(scene_source);
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

	bool dualprop = (sceneitem_prop == SCENEITEM_PROPERTY_POS ||
			 sceneitem_prop == SCENEITEM_PROPERTY_SCALE);

	nv_move_prop_number_floats(dualprop ? 2 : 1, action_number, props);
	return true;
}
static void add_number_props_to_list(obs_properties_t *sp, obs_property_t *list)
{
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
			obs_property_list_add_string(list, d, n);
		} else if (prop_type == OBS_PROPERTY_GROUP) {
			add_number_props_to_list(obs_property_group_content(p),
						 list);
		}
	}
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
	if (!prop) {
		dstr_free(&name);
		return false;
	}
	obs_property_list_clear(prop);
	obs_property_list_add_string(prop, "", "");

	dstr_printf(&name, "action_%lld_source", action_number);
	const char *source_name = obs_data_get_string(settings, name.array);
	dstr_free(&name);
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
	add_number_props_to_list(sp, prop);

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
	dstr_free(&name);
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

static void nv_move_fill_body_list(obs_property_t *p)
{
	obs_property_list_add_int(p, obs_module_text("Pelvis"), 0);
	obs_property_list_add_int(p, obs_module_text("LeftHip"), 1);
	obs_property_list_add_int(p, obs_module_text("RightHip"), 2);
	obs_property_list_add_int(p, obs_module_text("Torso"), 3);
	obs_property_list_add_int(p, obs_module_text("LeftKnee"), 4);
	obs_property_list_add_int(p, obs_module_text("RightKnee"), 5);
	obs_property_list_add_int(p, obs_module_text("Neck"), 6);
	obs_property_list_add_int(p, obs_module_text("LeftAnkle"), 7);
	obs_property_list_add_int(p, obs_module_text("RightAnkle"), 8);
	obs_property_list_add_int(p, obs_module_text("LeftBigToe"), 9);
	obs_property_list_add_int(p, obs_module_text("RightBigToe"), 10);
	obs_property_list_add_int(p, obs_module_text("LeftSmallToe"), 11);
	obs_property_list_add_int(p, obs_module_text("RightSmallToe"), 12);
	obs_property_list_add_int(p, obs_module_text("LeftHeel"), 13);
	obs_property_list_add_int(p, obs_module_text("RightHeel"), 14);
	obs_property_list_add_int(p, obs_module_text("Nose"), 15);
	obs_property_list_add_int(p, obs_module_text("LeftEye"), 16);
	obs_property_list_add_int(p, obs_module_text("RightEye"), 17);
	obs_property_list_add_int(p, obs_module_text("LeftEar"), 18);
	obs_property_list_add_int(p, obs_module_text("RightEar"), 19);
	obs_property_list_add_int(p, obs_module_text("LeftShoulder"), 20);
	obs_property_list_add_int(p, obs_module_text("RightShoulder"), 21);
	obs_property_list_add_int(p, obs_module_text("LeftElbow"), 22);
	obs_property_list_add_int(p, obs_module_text("RightElbow"), 23);
	obs_property_list_add_int(p, obs_module_text("LeftWrist"), 24);
	obs_property_list_add_int(p, obs_module_text("RightWrist"), 25);
	obs_property_list_add_int(p, obs_module_text("LeftPinkyKnuckle"), 26);
	obs_property_list_add_int(p, obs_module_text("RightPickyKnuckle"), 27);
	obs_property_list_add_int(p, obs_module_text("LeftMiddleTip"), 28);
	obs_property_list_add_int(p, obs_module_text("RightMiddleTip"), 29);
	obs_property_list_add_int(p, obs_module_text("LeftIndexKnuckle"), 30);
	obs_property_list_add_int(p, obs_module_text("RightIndexKnuckle"), 31);
	obs_property_list_add_int(p, obs_module_text("LeftThumbTip"), 32);
	obs_property_list_add_int(p, obs_module_text("RightThumbTip"), 33);
}

static void swap_setting(obs_data_t *settings, char *setting1, char *setting2)
{
	obs_data_item_t *item = obs_data_item_byname(settings, setting1);
	if (!item)
		item = obs_data_item_byname(settings, setting2);
	if (!item)
		return;

	enum obs_data_type t = obs_data_item_gettype(item);
	if (t == OBS_DATA_STRING) {
		char *temp = bstrdup(obs_data_get_string(settings, setting1));
		obs_data_set_string(settings, setting1,
				    obs_data_get_string(settings, setting2));
		obs_data_set_string(settings, setting2, temp);
		bfree(temp);
	} else if (t == OBS_DATA_NUMBER) {
		enum obs_data_number_type nt = obs_data_item_numtype(item);
		if (nt == OBS_DATA_NUM_INT) {
			long long temp = obs_data_get_int(settings, setting1);
			obs_data_set_int(settings, setting1,
					 obs_data_get_int(settings, setting2));
			obs_data_set_int(settings, setting2, temp);
		} else if (nt == OBS_DATA_NUM_DOUBLE) {
			double temp = obs_data_get_double(settings, setting1);
			obs_data_set_double(settings, setting1,
					    obs_data_get_double(settings,
								setting2));
			obs_data_set_double(settings, setting2, temp);
		}
	} else if (t == OBS_DATA_BOOLEAN) {
		bool temp = obs_data_get_bool(settings, setting1);
		obs_data_set_bool(settings, setting1,
				  obs_data_get_bool(settings, setting2));
		obs_data_set_bool(settings, setting2, temp);
	}
	obs_data_item_release(&item);
}

static void swap_action(obs_data_t *settings, long long a, long long b)
{
	char *actions[] = {"action_%lld_disabled",
			   "action_%lld_description",
			   "action_%lld_action",
			   "action_%lld_attach",
			   "action_%lld_scene",
			   "action_%lld_sceneitem",
			   "action_%lld_source",
			   "action_%lld_filter",
			   "action_%lld_property",
			   "action_%lld_sceneitem_property",
			   "action_%lld_enable",
			   "action_%lld_feature",
			   "action_%lld_bounding_box",
			   "action_%lld_landmark",
			   "action_%lld_landmark_1",
			   "action_%lld_landmark_2",
			   "action_%lld_pose",
			   "action_%lld_expression_property",
			   "action_%lld_expression_1",
			   "action_%lld_expression_2",
			   "action_%lld_gaze",
			   "action_%lld_body",
			   "action_%lld_body_1",
			   "action_%lld_body_2",
			   "action_%lld_required_confidence",
			   "action_%lld_factor",
			   "action_%lld_factor2",
			   "action_%lld_diff",
			   "action_%lld_diff2",
			   "action_%lld_threshold",
			   "action_%lld_easing"};
	struct dstr name1 = {0};
	struct dstr name2 = {0};
	for (long long i = 0; i < sizeof(actions) / sizeof(char *); i++) {
		dstr_printf(&name1, actions[i], a);
		dstr_printf(&name2, actions[i], b);
		swap_setting(settings, name1.array, name2.array);
	}
	dstr_free(&name1);
	dstr_free(&name2);
}

static bool nv_move_move_up_clicked(obs_properties_t *props,
				    obs_property_t *property, void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	const char *action_prop = obs_property_name(property);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_move_up", &action_number) != 1 ||
	    action_number <= 1)
		return false;
	obs_data_t *settings = obs_source_get_settings(filter->source);
	swap_action(settings, action_number, action_number - 1);
	long long show = obs_data_get_int(settings, "show");
	if (show == action_number) {
		obs_data_set_int(settings, "show", show - 1);
	}
	nv_move_actions_changed(data, props,
				obs_properties_get(props, "actions"), settings);
	obs_data_release(settings);
	return true;
}

static bool nv_move_move_down_clicked(obs_properties_t *props,
				      obs_property_t *property, void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	const char *action_prop = obs_property_name(property);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_move_down", &action_number) != 1 ||
	    !action_number || action_number >= (long long)filter->actions.num)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->source);
	swap_action(settings, action_number, action_number + 1);
	long long show = obs_data_get_int(settings, "show");
	if (show == action_number) {
		obs_data_set_int(settings, "show", show + 1);
	}
	nv_move_actions_changed(data, props,
				obs_properties_get(props, "actions"), settings);
	obs_data_release(settings);
	return true;
}

static bool nv_move_get_value_clicked(obs_properties_t *props,
				      obs_property_t *property, void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	const char *action_prop = obs_property_name(property);
	long long action_number = 0;
	if (sscanf(action_prop, "action_%lld_get_value", &action_number) != 1 ||
	    !action_number)
		return false;
	float value;
	struct vec2 vec2;
	struct dstr description = {0};
	if (nv_move_action_get_vec2(filter,
				    filter->actions.array + action_number - 1,
				    false, &vec2)) {
		dstr_printf(&description, "%f : %f", vec2.x, vec2.y);
	} else if (nv_move_action_get_float(
			   filter, filter->actions.array + action_number - 1,
			   false, &value)) {
		dstr_printf(&description, "%f", value);
	} else {
		dstr_copy(&description, obs_module_text("Error"));
	}
	obs_property_set_description(property, description.array);
	dstr_free(&description);
	return true;
}

static void add_expressions_to_list(obs_property_t *p)
{
	obs_property_list_add_int(p, "", 0);
	obs_property_list_add_int(p, "browDown", -1);
	obs_property_list_add_int(p, "browDown_L", 1);
	obs_property_list_add_int(p, "browDown_R", 2);
	obs_property_list_add_int(p, "browInnerUp", -2);
	obs_property_list_add_int(p, "browInnerUp_L", 3);
	obs_property_list_add_int(p, "browInnerUp_R", 4);
	obs_property_list_add_int(p, "browOuterUp", -3);
	obs_property_list_add_int(p, "browOuterUp_L", 5);
	obs_property_list_add_int(p, "browOuterUp_R", 6);
	obs_property_list_add_int(p, "cheekPuff", -4);
	obs_property_list_add_int(p, "cheekPuff_L", 7);
	obs_property_list_add_int(p, "cheekPuff_R", 8);
	obs_property_list_add_int(p, "cheekSquint", -5);
	obs_property_list_add_int(p, "cheekSquint_L", 9);
	obs_property_list_add_int(p, "cheekSquint_R", 10);
	obs_property_list_add_int(p, "eyeBlink", -6);
	obs_property_list_add_int(p, "eyeBlink_L", 11);
	obs_property_list_add_int(p, "eyeBlink_R", 12);
	obs_property_list_add_int(p, "eyeLookLeft", -17);
	obs_property_list_add_int(p, "eyeLookRight", -20);
	obs_property_list_add_int(p, "eyeLookSideways", -27);
	obs_property_list_add_int(p, "eyeLookUpDown", -28);
	obs_property_list_add_int(p, "eyeLookSideways_L", -29);
	obs_property_list_add_int(p, "eyeLookSideways_R", -30);
	obs_property_list_add_int(p, "eyeLookUpDown_L", -31);
	obs_property_list_add_int(p, "eyeLookUpDown_R", -32);
	obs_property_list_add_int(p, "eyeLookDown", -7);
	obs_property_list_add_int(p, "eyeLookDown_L", 13);
	obs_property_list_add_int(p, "eyeLookDown_R", 14);
	obs_property_list_add_int(p, "eyeLookIn", -8);
	obs_property_list_add_int(p, "eyeLookIn_L", 15);
	obs_property_list_add_int(p, "eyeLookIn_R", 16);
	obs_property_list_add_int(p, "eyeLookOut", -9);
	obs_property_list_add_int(p, "eyeLookOut_L", 17);
	obs_property_list_add_int(p, "eyeLookOut_R", 18);
	obs_property_list_add_int(p, "eyeLookUp", -10);
	obs_property_list_add_int(p, "eyeLookUp_L", 19);
	obs_property_list_add_int(p, "eyeLookUp_R", 20);
	obs_property_list_add_int(p, "eyeSquint", -11);
	obs_property_list_add_int(p, "eyeSquint_L", 21);
	obs_property_list_add_int(p, "eyeSquint_R", 22);
	obs_property_list_add_int(p, "eyeWide", -12);
	obs_property_list_add_int(p, "eyeWide_L", 23);
	obs_property_list_add_int(p, "eyeWide_R", 24);
	obs_property_list_add_int(p, "jawSideways", -13);
	obs_property_list_add_int(p, "jawForward", 25);
	obs_property_list_add_int(p, "jawLeft", 26);
	obs_property_list_add_int(p, "jawOpen", 27);
	obs_property_list_add_int(p, "jawRight", 28);
	obs_property_list_add_int(p, "mouthSideways", -14);
	obs_property_list_add_int(p, "mouthClose", 29);
	obs_property_list_add_int(p, "mouthDimple", -15);
	obs_property_list_add_int(p, "mouthDimple_L", 30);
	obs_property_list_add_int(p, "mouthDimple_R", 31);
	obs_property_list_add_int(p, "mouthFrown", -16);
	obs_property_list_add_int(p, "mouthFrown_L", 32);
	obs_property_list_add_int(p, "mouthFrown_R", 33);

	obs_property_list_add_int(p, "mouthFunnel", 34);
	obs_property_list_add_int(p, "mouthLeft", 35);
	obs_property_list_add_int(p, "mouthLowerDown", -18);
	obs_property_list_add_int(p, "mouthLowerDown_L", 36);
	obs_property_list_add_int(p, "mouthLowerDown_R", 37);
	obs_property_list_add_int(p, "mouthPress", -19);
	obs_property_list_add_int(p, "mouthPress_L", 38);
	obs_property_list_add_int(p, "mouthPress_R", 39);

	obs_property_list_add_int(p, "mouthPucker", 40);
	obs_property_list_add_int(p, "mouthRight", 41);
	obs_property_list_add_int(p, "mouthRoll", -21);
	obs_property_list_add_int(p, "mouthRollLower", 42);
	obs_property_list_add_int(p, "mouthRollUpper", 43);
	obs_property_list_add_int(p, "mouthShrugLower", -22);
	obs_property_list_add_int(p, "mouthShrugLower", 44);
	obs_property_list_add_int(p, "mouthShrugUpper", 45);
	obs_property_list_add_int(p, "mouthSmile", -23);
	obs_property_list_add_int(p, "mouthSmile_L", 46);
	obs_property_list_add_int(p, "mouthSmile_R", 47);
	obs_property_list_add_int(p, "mouthStretch", -24);
	obs_property_list_add_int(p, "mouthStretch_L", 48);
	obs_property_list_add_int(p, "mouthStretch_R", 49);
	obs_property_list_add_int(p, "mouthUpperUp", -25);
	obs_property_list_add_int(p, "mouthUpperUp_L", 50);
	obs_property_list_add_int(p, "mouthUpperUp_R", 51);
	obs_property_list_add_int(p, "noseSneer", -26);
	obs_property_list_add_int(p, "noseSneer_L", 52);
	obs_property_list_add_int(p, "noseSneer_R", 53);
}

static obs_properties_t *nv_move_properties(void *data)
{
	struct nvidia_move_info *filter = (struct nvidia_move_info *)data;
	obs_properties_t *props = obs_properties_create();

	if (filter && filter->last_error) {
		obs_property_text_set_info_type(
			obs_properties_add_text(props, "last_error",
						filter->last_error,
						OBS_TEXT_INFO),
			OBS_TEXT_INFO_WARNING);
	}

	obs_property_t *p = obs_properties_add_int(props, "actions",
						   obs_module_text("Actions"),
						   1, MAX_ACTIONS, 1);

	obs_property_set_modified_callback2(p, nv_move_actions_changed, data);

	p = obs_properties_add_list(props, "show", obs_module_text("Show"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("All"), 0);

	obs_property_set_modified_callback2(p, nv_move_actions_changed, data);

	struct dstr name = {0};
	struct dstr description = {0};
	for (long long i = 1; i <= MAX_ACTIONS; i++) {
		obs_properties_t *group = obs_properties_create();
		dstr_printf(&name, "action_%lld_disabled", i);
		obs_properties_add_bool(group, name.array,
					obs_module_text("Disabled"));
		dstr_printf(&name, "action_%lld_description", i);
		obs_properties_add_text(group, name.array,
					obs_module_text("ActionDescription"),
					OBS_TEXT_DEFAULT);

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
		obs_property_list_add_int(p, obs_module_text("AttachSource"),
					  ACTION_ATTACH_SOURCE);

		obs_property_set_modified_callback2(p, nv_move_action_changed,
						    data);
		dstr_printf(&name, "action_%lld_attach", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Attach"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("Eyes"),
					  ATTACH_EYES);
		obs_property_list_add_int(p, obs_module_text("LeftEye"),
					  ATTACH_LEFT_EYE);
		obs_property_list_add_int(p, obs_module_text("RightEye"),
					  ATTACH_RIGHT_EYE);
		obs_property_list_add_int(p, obs_module_text("Eyebrows"),
					  ATTACH_EYEBROWS);
		obs_property_list_add_int(p, obs_module_text("LeftEyebrow"),
					  ATTACH_LEFT_EYEBROW);
		obs_property_list_add_int(p, obs_module_text("RightEyebrow"),
					  ATTACH_RIGHT_EYEBROW);
		obs_property_list_add_int(p, obs_module_text("Ears"),
					  ATTACH_EARS);
		obs_property_list_add_int(p, obs_module_text("LeftEar"),
					  ATTACH_LEFT_EAR);
		obs_property_list_add_int(p, obs_module_text("RightEar"),
					  ATTACH_RIGHT_EAR);
		obs_property_list_add_int(p, obs_module_text("Nose"),
					  ATTACH_NOSE);
		obs_property_list_add_int(p, obs_module_text("Mouth"),
					  ATTACH_MOUTH);
		obs_property_list_add_int(p, obs_module_text("UpperLip"),
					  ATTACH_UPPER_LIP);
		obs_property_list_add_int(p, obs_module_text("LowerLip"),
					  ATTACH_LOWER_LIP);
		obs_property_list_add_int(p, obs_module_text("Chin"),
					  ATTACH_CHIN);
		obs_property_list_add_int(p, obs_module_text("Jaw"),
					  ATTACH_JAW);
		obs_property_list_add_int(p, obs_module_text("Forehead"),
					  ATTACH_FOREHEAD);

		obs_property_set_modified_callback2(p, nv_move_attach_changed,
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
		obs_property_list_add_int(p, obs_module_text("CropLeft"),
					  SCENEITEM_PROPERTY_CROP_LEFT);
		obs_property_list_add_int(p, obs_module_text("CropTop"),
					  SCENEITEM_PROPERTY_CROP_TOP);
		obs_property_list_add_int(p, obs_module_text("CropRight"),
					  SCENEITEM_PROPERTY_CROP_RIGHT);
		obs_property_list_add_int(p, obs_module_text("CropBottom"),
					  SCENEITEM_PROPERTY_CROP_BOTTOM);

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
		obs_property_list_add_int(p, obs_module_text("Pose"),
					  FEATURE_POSE);
		obs_property_list_add_int(p, obs_module_text("Expression"),
					  FEATURE_EXPRESSION);
		obs_property_list_add_int(p, obs_module_text("Gaze"),
					  FEATURE_GAZE);
		obs_property_list_add_int(p, obs_module_text("Body"),
					  FEATURE_BODY);

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

		obs_property_list_add_int(p, obs_module_text("PosX"),
					  FEATURE_LANDMARK_X);
		obs_property_list_add_int(p, obs_module_text("PosY"),
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

		dstr_printf(&name, "action_%lld_pose", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("PoseProperty"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("PoseX"),
					  FEATURE_POSE_X);
		obs_property_list_add_int(p, obs_module_text("PoseY"),
					  FEATURE_POSE_Y);
		obs_property_list_add_int(p, obs_module_text("PoseZ"),
					  FEATURE_POSE_Z);
		obs_property_list_add_int(p, obs_module_text("PoseW"),
					  FEATURE_POSE_W);

		dstr_printf(&name, "action_%lld_expression_property", i);
		p = obs_properties_add_list(
			group, name.array,
			obs_module_text("ExpressionProperty"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Single"),
					  FEATURE_EXPRESSION_SINGLE);
		obs_property_list_add_int(p, obs_module_text("Vector"),
					  FEATURE_EXPRESSION_VECTOR);
		obs_property_list_add_int(p, obs_module_text("Add"),
					  FEATURE_EXPRESSION_ADD);
		obs_property_list_add_int(p, obs_module_text("Substract"),
					  FEATURE_EXPRESSION_SUBSTRACT);
		obs_property_list_add_int(p, obs_module_text("Distance"),
					  FEATURE_EXPRESSION_DISTANCE);
		obs_property_list_add_int(p, obs_module_text("Average"),
					  FEATURE_EXPRESSION_AVG);

		obs_property_set_modified_callback2(
			p, nv_move_expression_changed, data);

		dstr_printf(&name, "action_%lld_expression_1", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Expression"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		add_expressions_to_list(p);

		dstr_printf(&name, "action_%lld_expression_2", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Expression"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		add_expressions_to_list(p);

		dstr_printf(&name, "action_%lld_gaze", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("GazeProperty"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("Vector"),
					  FEATURE_GAZE_VECTOR);
		obs_property_list_add_int(p, obs_module_text("VectorPitch"),
					  FEATURE_GAZE_VECTOR_PITCH);
		obs_property_list_add_int(p, obs_module_text("VectorYaw"),
					  FEATURE_GAZE_VECTOR_YAW);
		obs_property_list_add_int(p, obs_module_text("HeadTranslation"),
					  FEATURE_GAZE_HEADTRANSLATION);
		obs_property_list_add_int(p,
					  obs_module_text("HeadTranslationX"),
					  FEATURE_GAZE_HEADTRANSLATION_X);
		obs_property_list_add_int(p,
					  obs_module_text("HeadTranslationY"),
					  FEATURE_GAZE_HEADTRANSLATION_Y);
		obs_property_list_add_int(p,
					  obs_module_text("HeadTranslationZ"),
					  FEATURE_GAZE_HEADTRANSLATION_Z);
		obs_property_list_add_int(p, obs_module_text("CenterPoint"),
					  FEATURE_GAZE_DIRECTION_CENTER_POINT);
		obs_property_list_add_int(
			p, obs_module_text("CenterPointX"),
			FEATURE_GAZE_DIRECTION_CENTER_POINT_X);
		obs_property_list_add_int(
			p, obs_module_text("CenterPointY"),
			FEATURE_GAZE_DIRECTION_CENTER_POINT_Y);
		obs_property_list_add_int(
			p, obs_module_text("CenterPointZ"),
			FEATURE_GAZE_DIRECTION_CENTER_POINT_Z);
		obs_property_list_add_int(p, obs_module_text("DirectionVector"),
					  FEATURE_GAZE_DIRECTION_VECTOR);
		obs_property_list_add_int(p,
					  obs_module_text("DirectionVectorX"),
					  FEATURE_GAZE_DIRECTION_VECTOR_X);
		obs_property_list_add_int(p,
					  obs_module_text("DirectionVectorY"),
					  FEATURE_GAZE_DIRECTION_VECTOR_Y);
		obs_property_list_add_int(p,
					  obs_module_text("DirectionVectorZ"),
					  FEATURE_GAZE_DIRECTION_VECTOR_Z);

		dstr_printf(&name, "action_%lld_body", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("BodyProperty"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);

		obs_property_list_add_int(p, obs_module_text("Confidence"),
					  BODY_CONFIDENCE);
		obs_property_list_add_int(p, obs_module_text("Body2DPosX"),
					  BODY_2D_POSX);
		obs_property_list_add_int(p, obs_module_text("Body2DPosY"),
					  BODY_2D_POSY);
		obs_property_list_add_int(p, obs_module_text("Body2DDistance"),
					  BODY_2D_DISTANCE);
		obs_property_list_add_int(p, obs_module_text("Body2DRotation"),
					  BODY_2D_ROT);
		obs_property_list_add_int(p, obs_module_text("Body2DDiffX"),
					  BODY_2D_DIFF_X);
		obs_property_list_add_int(p, obs_module_text("Body2DDiffY"),
					  BODY_2D_DIFF_Y);
		obs_property_list_add_int(p, obs_module_text("Body2DDiff"),
					  BODY_2D_DIFF);
		obs_property_list_add_int(p, obs_module_text("Body2DPos"),
					  BODY_2D_POS);
		obs_property_list_add_int(p, obs_module_text("Body3DPosX"),
					  BODY_3D_POSX);
		obs_property_list_add_int(p, obs_module_text("Body3DPosY"),
					  BODY_3D_POSY);
		obs_property_list_add_int(p, obs_module_text("Body3DPosZ"),
					  BODY_3D_POSZ);
		obs_property_list_add_int(p, obs_module_text("Body3DDistance"),
					  BODY_3D_DISTANCE);
		obs_property_list_add_int(p, obs_module_text("Body3DDiffX"),
					  BODY_3D_DIFF_X);
		obs_property_list_add_int(p, obs_module_text("Body3DDiffY"),
					  BODY_3D_DIFF_Y);
		obs_property_list_add_int(p, obs_module_text("Body3DDiffZ"),
					  BODY_3D_DIFF_Z);
		obs_property_list_add_int(p, obs_module_text("Body3DPos"),
					  BODY_3D_POS);
		obs_property_list_add_int(p, obs_module_text("Body3DDiff"),
					  BODY_3D_DIFF);
		obs_property_list_add_int(p, obs_module_text("BodyAngleX"),
					  BODY_ANGLE_X);
		obs_property_list_add_int(p, obs_module_text("BodyAngleY"),
					  BODY_ANGLE_Y);
		obs_property_list_add_int(p, obs_module_text("BodyAngleZ"),
					  BODY_ANGLE_Z);
		obs_property_list_add_int(p, obs_module_text("BodyAngle"),
					  BODY_ANGLE);

		obs_property_set_modified_callback2(p, nv_move_body_changed,
						    data);

		dstr_printf(&name, "action_%lld_body_1", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Keypoint"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		nv_move_fill_body_list(p);

		dstr_printf(&name, "action_%lld_body_2", i);
		p = obs_properties_add_list(group, name.array,
					    obs_module_text("Keypoint"),
					    OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		nv_move_fill_body_list(p);

		dstr_printf(&name, "action_%lld_required_confidence", i);
		p = obs_properties_add_float_slider(
			group, name.array, obs_module_text("Confidence"), 0.0,
			25.0, 0.001);

		dstr_printf(&name, "action_%lld_factor", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Factor"),
					     -1000000.0, 1000000.0, 0.001);
		obs_property_float_set_suffix(p, "%");
		dstr_printf(&name, "action_%lld_factor2", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Factor"),
					     -1000000.0, 1000000.0, 0.001);
		obs_property_float_set_suffix(p, "%");

		dstr_printf(&name, "action_%lld_diff", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Difference"),
					     -1000000.0, 1000000.0, 0.001);
		dstr_printf(&name, "action_%lld_diff2", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Difference"),
					     -1000000.0, 1000000.0, 0.001);

		dstr_printf(&name, "action_%lld_threshold", i);
		p = obs_properties_add_float(group, name.array,
					     obs_module_text("Threshold"),
					     -1000000.0, 1000000, 0.001);

		dstr_printf(&name, "action_%lld_get_value", i);
		obs_properties_add_button2(group, name.array,
					   obs_module_text("GetValue"),
					   nv_move_get_value_clicked, filter);

		dstr_printf(&name, "action_%lld_easing", i);
		p = obs_properties_add_float_slider(group, name.array,
						    obs_module_text("Easing"),
						    0.0, 99.0, 0.001);
		obs_property_float_set_suffix(p, "%");

		dstr_printf(&name, "action_%lld_move_up", i);
		obs_properties_add_button2(group, name.array,
					   obs_module_text("MoveUp"),
					   nv_move_move_up_clicked, filter);

		dstr_printf(&name, "action_%lld_move_down", i);
		obs_properties_add_button2(group, name.array,
					   obs_module_text("MoveDown"),
					   nv_move_move_down_clicked, filter);

		dstr_printf(&name, "action_%lld_group", i);
		dstr_printf(&description, "%s %lld", obs_module_text("Action"),
			    i);

		obs_properties_add_group(props, name.array, description.array,
					 OBS_GROUP_NORMAL, group);
	}
	dstr_free(&name);
	dstr_free(&description);

	obs_properties_add_text(props, "plugin_info", PLUGIN_INFO,
				OBS_TEXT_INFO);

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

	if (!filter->render) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

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
		if (filter->faceDetectHandle)
			nv_move_log_error(
				filter,
				NvAR_SetObject(filter->faceDetectHandle,
					       NvAR_Parameter_Input(Image),
					       filter->BGR_src_img,
					       sizeof(NvCVImage)),
				"Set Image");
		if (filter->landmarkHandle)
			nv_move_log_error(
				filter,
				NvAR_SetObject(filter->landmarkHandle,
					       NvAR_Parameter_Input(Image),
					       filter->BGR_src_img,
					       sizeof(NvCVImage)),
				"Set Image");
		if (filter->expressionHandle)
			nv_move_log_error(
				filter,
				NvAR_SetObject(filter->expressionHandle,
					       NvAR_Parameter_Input(Image),
					       filter->BGR_src_img,
					       sizeof(NvCVImage)),
				"Set Image");
		if (filter->gazeHandle)
			nv_move_log_error(
				filter,
				NvAR_SetObject(filter->gazeHandle,
					       NvAR_Parameter_Input(Image),
					       filter->BGR_src_img,
					       sizeof(NvCVImage)),
				"Set Image");
		if (filter->bodyHandle)
			nv_move_log_error(
				filter,
				NvAR_SetObject(filter->bodyHandle,
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
	if (filter->faceDetectHandle)
		nv_move_log_error(filter, NvAR_Run(filter->faceDetectHandle),
				  "Run faceDetect");
	if (filter->landmarkHandle)
		nv_move_log_error(filter, NvAR_Run(filter->landmarkHandle),
				  "Run landmark");
	if (filter->expressionHandle)
		nv_move_log_error(filter, NvAR_Run(filter->expressionHandle),
				  "Run expression");
	if (filter->gazeHandle)
		nv_move_log_error(filter, NvAR_Run(filter->gazeHandle),
				  "Run gaze");
	if (filter->bodyHandle)
		nv_move_log_error(filter, NvAR_Run(filter->bodyHandle),
				  "Run body");

	for (size_t i = 0; i < filter->actions.num; i++) {
		struct nvidia_move_action *action = filter->actions.array + i;
		if (action->disabled)
			continue;
		if (action->action == ACTION_MOVE_SOURCE ||
		    action->action == ACTION_ATTACH_SOURCE) {
			if (!action->name)
				continue;
			obs_source_t *scene_source =
				obs_weak_source_get_source(action->target);
			if (!scene_source)
				continue;
			obs_scene_t *scene =
				obs_scene_from_source(scene_source);
			if (!scene)
				scene = obs_group_from_source(scene_source);
			obs_source_release(scene_source);
			if (!scene)
				continue;
			obs_sceneitem_t *item =
				obs_scene_find_source(scene, action->name);
			if (!item)
				continue;

			// SCENEITEM_PROPERTY_ALL
			if (action->action == ACTION_ATTACH_SOURCE) {
				if (!filter->bboxes.num_boxes) {
					if (obs_sceneitem_visible(item)) {
						obs_sceneitem_set_visible(
							item, false);
					}
					continue;
				}
				bool vert = false;
				bool flip = false;
				bool height_to_pos = false;
				if (action->property == ATTACH_EYES) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							65 - 1;
						action->feature_number[1] =
							86 - 1;
						action->feature_number[2] =
							83 - 1;
						action->feature_number[3] =
							68 - 1;
					} else {
						action->feature_number[0] =
							37 - 1;
						action->feature_number[1] =
							46 - 1;
						action->feature_number[2] =
							44 - 1;
						action->feature_number[3] =
							39 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_RIGHT_EYE) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							65 - 1;
						action->feature_number[1] =
							69 - 1;
						action->feature_number[2] =
							68 - 1;
						action->feature_number[3] =
							70 - 1;
					} else {
						action->feature_number[0] =
							37 - 1;
						action->feature_number[1] =
							40 - 1;
						action->feature_number[2] =
							39 - 1;
						action->feature_number[3] =
							41 - 1;
					}
				} else if (action->property ==
					   ATTACH_LEFT_EYE) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							82 - 1;
						action->feature_number[1] =
							86 - 1;
						action->feature_number[2] =
							84 - 1;
						action->feature_number[3] =
							88 - 1;
					} else {
						action->feature_number[0] =
							43 - 1;
						action->feature_number[1] =
							46 - 1;
						action->feature_number[2] =
							44 - 1;
						action->feature_number[3] =
							48 - 1;
					}
				} else if (action->property ==
					   ATTACH_EYEBROWS) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							34 - 1;
						action->feature_number[1] =
							47 - 1;
						action->feature_number[2] =
							36 - 1;
						action->feature_number[3] =
							45 - 1;
					} else {
						action->feature_number[0] =
							18 - 1;
						action->feature_number[1] =
							27 - 1;
						action->feature_number[2] =
							20 - 1;
						action->feature_number[3] =
							25 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_RIGHT_EYEBROW) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							34 - 1;
						action->feature_number[1] =
							38 - 1;
						action->feature_number[2] =
							36 - 1;
						action->feature_number[3] =
							36 - 1;
					} else {
						action->feature_number[0] =
							18 - 1;
						action->feature_number[1] =
							22 - 1;
						action->feature_number[2] =
							20 - 1;
						action->feature_number[3] =
							20 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_LEFT_EYEBROW) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							43 - 1;
						action->feature_number[1] =
							47 - 1;
						action->feature_number[2] =
							45 - 1;
						action->feature_number[3] =
							45 - 1;
					} else {
						action->feature_number[0] =
							23 - 1;
						action->feature_number[1] =
							27 - 1;
						action->feature_number[2] =
							25 - 1;
						action->feature_number[3] =
							25 - 1;
					}
					height_to_pos = true;
				} else if (action->property == ATTACH_EARS) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							33 - 1;
						action->feature_number[2] =
							1 - 1;
						action->feature_number[3] =
							2 - 1;
					} else {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							17 - 1;
						action->feature_number[2] =
							1 - 1;
						action->feature_number[3] =
							2 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_RIGHT_EAR) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							2 - 1;
						action->feature_number[2] =
							1 - 1;
						action->feature_number[3] =
							2 - 1;
					} else {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							2 - 1;
						action->feature_number[2] =
							1 - 1;
						action->feature_number[3] =
							2 - 1;
					}
					vert = true;
				} else if (action->property ==
					   ATTACH_LEFT_EAR) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							33 - 1;
						action->feature_number[1] =
							32 - 1;
						action->feature_number[2] =
							33 - 1;
						action->feature_number[3] =
							32 - 1;
					} else {
						action->feature_number[0] =
							17 - 1;
						action->feature_number[1] =
							16 - 1;
						action->feature_number[2] =
							17 - 1;
						action->feature_number[3] =
							16 - 1;
					}
					vert = true;
				} else if (action->property == ATTACH_NOSE) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							58 - 1;
						action->feature_number[1] =
							62 - 1;
						action->feature_number[2] =
							52 - 1;
						action->feature_number[3] =
							60 - 1;
					} else {
						action->feature_number[0] =
							32 - 1;
						action->feature_number[1] =
							36 - 1;
						action->feature_number[2] =
							28 - 1;
						action->feature_number[3] =
							34 - 1;
					}
				} else if (action->property == ATTACH_MOUTH) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							99 - 1;
						action->feature_number[1] =
							105 - 1;
						action->feature_number[2] =
							102 - 1;
						action->feature_number[3] =
							108 - 1;
					} else {
						action->feature_number[0] =
							49 - 1;
						action->feature_number[1] =
							55 - 1;
						action->feature_number[2] =
							52 - 1;
						action->feature_number[3] =
							58 - 1;
					}
				} else if (action->property ==
					   ATTACH_UPPER_LIP) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							99 - 1;
						action->feature_number[1] =
							105 - 1;
						action->feature_number[2] =
							101 - 1;
						action->feature_number[3] =
							103 - 1;
					} else {
						action->feature_number[0] =
							49 - 1;
						action->feature_number[1] =
							55 - 1;
						action->feature_number[2] =
							51 - 1;
						action->feature_number[3] =
							53 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_LOWER_LIP) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							99 - 1;
						action->feature_number[1] =
							105 - 1;
						action->feature_number[2] =
							108 - 1;
						action->feature_number[3] =
							108 - 1;
					} else {
						action->feature_number[0] =
							49 - 1;
						action->feature_number[1] =
							55 - 1;
						action->feature_number[2] =
							58 - 1;
						action->feature_number[3] =
							58 - 1;
					}
					height_to_pos = true;
				} else if (action->property == ATTACH_CHIN) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							15 - 1;
						action->feature_number[1] =
							19 - 1;
						action->feature_number[2] =
							17 - 1;
						action->feature_number[3] =
							17 - 1;
					} else {
						action->feature_number[0] =
							8 - 1;
						action->feature_number[1] =
							10 - 1;
						action->feature_number[2] =
							9 - 1;
						action->feature_number[3] =
							9 - 1;
					}
					height_to_pos = true;
				} else if (action->property == ATTACH_JAW) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							33 - 1;
						action->feature_number[2] =
							17 - 1;
						action->feature_number[3] =
							17 - 1;
					} else {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							17 - 1;
						action->feature_number[2] =
							9 - 1;
						action->feature_number[3] =
							9 - 1;
					}
					height_to_pos = true;
				} else if (action->property ==
					   ATTACH_FOREHEAD) {
					if (filter->landmarks.num == 126) {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							33 - 1;
						action->feature_number[2] =
							52 - 1;
						action->feature_number[3] =
							60 - 1;
					} else {
						action->feature_number[0] =
							1 - 1;
						action->feature_number[1] =
							17 - 1;
						action->feature_number[2] =
							28 - 1;
						action->feature_number[3] =
							34 - 1;
					}
					//height_to_pos = true;
				} else {
					continue;
				}
				if (action->feature_number[0] >=
					    (int32_t)filter->landmarks.num ||
				    action->feature_number[1] >=
					    (int32_t)filter->landmarks.num) {
					continue;
				}
				if (filter->landmarks_confidence.array
						    [action->feature_number[0]] <
					    action->required_confidence ||
				    filter->landmarks_confidence.array
						    [action->feature_number[1]] <
					    action->required_confidence) {
					if (obs_sceneitem_visible(item)) {
						obs_sceneitem_set_visible(
							item, false);
					}
					continue;
				}

				struct vec2 pos;
				if (vert) {
					pos.x = filter->landmarks
							.array[action->feature_number
								       [0]]
							.x;
					pos.y = filter->landmarks
							.array[action->feature_number
								       [0]]
							.y;
				} else {
					pos.x = (filter->landmarks
							 .array[action->feature_number
									[1]]
							 .x +
						 filter->landmarks
							 .array[action->feature_number
									[0]]
							 .x) /
						2.0f;
					pos.y = (filter->landmarks
							 .array[action->feature_number
									[1]]
							 .y +
						 filter->landmarks
							 .array[action->feature_number
									[0]]
							 .y) /
						2.0f;
				}
				obs_sceneitem_set_pos(item, &pos);

				float rot = DEG(atan2f(
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

				if (vert)
					rot -= 90.0f;
				obs_sceneitem_set_rot(item, rot);
				obs_source_t *s =
					obs_sceneitem_get_source(item);
				float w = (float)obs_source_get_width(s);
				float h = (float)obs_source_get_height(s);
				if (w > 0.0f && h > 0.0f) {
					float x =
						filter->landmarks
							.array[action->feature_number
								       [0]]
							.x -
						filter->landmarks
							.array[action->feature_number
								       [1]]
							.x;
					float y =
						filter->landmarks
							.array[action->feature_number
								       [0]]
							.y -
						filter->landmarks
							.array[action->feature_number
								       [1]]
							.y;
					float dist = sqrtf(x * x + y * y);
					dist *= action->factor;
					if (height_to_pos) {
						x = (filter->landmarks
							     .array[action->feature_number
									    [3]]
							     .x +
						     filter->landmarks
							     .array[action->feature_number
									    [2]]
							     .x) /
							    2.0f -
						    pos.x;
						y = (filter->landmarks
							     .array[action->feature_number
									    [3]]
							     .y +
						     filter->landmarks
							     .array[action->feature_number
									    [2]]
							     .y) /
							    2.0f -
						    pos.y;
					} else {
						x = filter->landmarks
							    .array[action->feature_number
									   [3]]
							    .x -
						    filter->landmarks
							    .array[action->feature_number
									   [2]]
							    .x;
						y = filter->landmarks
							    .array[action->feature_number
									   [3]]
							    .y -
						    filter->landmarks
							    .array[action->feature_number
									   [2]]
							    .y;
					}
					float dist2 = sqrtf(x * x + y * y);
					dist2 *= action->factor2;

					dist = action->previous_vec2.x *
						       action->easing +
					       dist * (1.0f - action->easing);
					action->previous_vec2.x = dist;
					dist2 = action->previous_vec2.y *
							action->easing +
						dist2 * (1.0f - action->easing);
					action->previous_vec2.y = dist2;

					if (obs_sceneitem_get_bounds_type(
						    item) == OBS_BOUNDS_NONE) {
						struct vec2 scale = {0};
						if (vert) {
							scale.y = dist / h;
							scale.x = dist2 / w;
						} else {
							scale.x = dist / w;
							scale.y = dist2 / h;
						}
						obs_sceneitem_set_scale(item,
									&scale);
					} else {
						struct vec2 bounds = {0};
						if (vert) {
							bounds.y = dist;
							bounds.x = dist2;
						} else {
							bounds.x = dist;
							bounds.y = dist2;
						}
						obs_sceneitem_set_bounds(
							item, &bounds);
					}
				}
				if (!obs_sceneitem_visible(item)) {
					obs_sceneitem_set_visible(item, true);
				}
			} else if (action->property == SCENEITEM_PROPERTY_POS) {
				struct vec2 pos;
				if (nv_move_action_get_vec2(filter, action,
							    true, &pos))
					obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_POSX) {
				struct vec2 pos;
				obs_sceneitem_get_pos(item, &pos);
				if (nv_move_action_get_float(filter, action,
							     true, &pos.x))
					obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_POSY) {
				struct vec2 pos;
				obs_sceneitem_get_pos(item, &pos);
				if (nv_move_action_get_float(filter, action,
							     true, &pos.y))
					obs_sceneitem_set_pos(item, &pos);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALE) {
				struct vec2 scale;
				if (nv_move_action_get_vec2(filter, action,
							    true, &scale)) {
					if (obs_sceneitem_get_bounds_type(
						    item) == OBS_BOUNDS_NONE) {
						scale.x /= (float)obs_source_get_width(
							obs_sceneitem_get_source(
								item));
						scale.y /= (float)obs_source_get_height(
							obs_sceneitem_get_source(
								item));
						obs_sceneitem_set_scale(item,
									&scale);
					} else {
						obs_sceneitem_set_bounds(
							item, &scale);
					}
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALEX) {
				struct vec2 scale;
				if (obs_sceneitem_get_bounds_type(item) ==
				    OBS_BOUNDS_NONE) {
					obs_sceneitem_get_scale(item, &scale);
					if (nv_move_action_get_float(
						    filter, action, true,
						    &scale.x)) {
						scale.x /= (float)obs_source_get_width(
							obs_sceneitem_get_source(
								item));
						obs_sceneitem_set_scale(item,
									&scale);
					}
				} else {
					obs_sceneitem_get_bounds(item, &scale);
					if (nv_move_action_get_float(
						    filter, action, true,
						    &scale.x))
						obs_sceneitem_set_bounds(
							item, &scale);
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_SCALEY) {
				struct vec2 scale;
				if (obs_sceneitem_get_bounds_type(item) ==
				    OBS_BOUNDS_NONE) {
					obs_sceneitem_get_scale(item, &scale);
					if (nv_move_action_get_float(
						    filter, action, true,
						    &scale.y)) {
						scale.y /= (float)obs_source_get_height(
							obs_sceneitem_get_source(
								item));
						obs_sceneitem_set_scale(item,
									&scale);
					}
				} else {
					obs_sceneitem_get_bounds(item, &scale);
					if (nv_move_action_get_float(
						    filter, action, true,
						    &scale.y))
						obs_sceneitem_set_bounds(
							item, &scale);
				}
			} else if (action->property == SCENEITEM_PROPERTY_ROT) {
				float rot;
				if (nv_move_action_get_float(filter, action,
							     true, &rot))
					obs_sceneitem_set_rot(item, rot);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_LEFT) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				float value;
				if (nv_move_action_get_float(filter, action,
							     true, &value)) {
					crop.left = (int)value;
					obs_sceneitem_set_crop(item, &crop);
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_RIGHT) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				float value;
				if (nv_move_action_get_float(filter, action,
							     true, &value)) {
					crop.right = (int)value;
					obs_sceneitem_set_crop(item, &crop);
				}
				obs_sceneitem_set_crop(item, &crop);
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_TOP) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				float value;
				if (nv_move_action_get_float(filter, action,
							     true, &value)) {
					crop.top = (int)value;
					obs_sceneitem_set_crop(item, &crop);
				}
			} else if (action->property ==
				   SCENEITEM_PROPERTY_CROP_BOTTOM) {
				struct obs_sceneitem_crop crop;
				obs_sceneitem_get_crop(item, &crop);
				float value;
				if (nv_move_action_get_float(filter, action,
							     true, &value)) {
					crop.bottom = (int)value;
					obs_sceneitem_set_crop(item, &crop);
				}
			}
		} else if (action->action == ACTION_MOVE_VALUE) {
			if (!action->name || !strlen(action->name))
				continue;
			float value;
			if (!nv_move_action_get_float(filter, action, true,
						      &value))
				continue;
			obs_source_t *source =
				obs_weak_source_get_source(action->target);
			if (!source) {
				if (action->target) {
					obs_weak_source_release(action->target);
					action->target = NULL;
				}
				continue;
			}
			obs_data_t *d = obs_data_create();
			if (action->is_int) {
				obs_data_set_int(d, action->name,
						 (long long)value);
			} else {
				obs_data_set_double(d, action->name,
						    (double)value);
			}
			obs_source_update(source, d);
			obs_data_release(d);
			obs_source_release(source);
		} else if (action->action == ACTION_ENABLE_FILTER) {
			float value;
			if (!nv_move_action_get_float(filter, action, true,
						      &value))
				continue;
			obs_source_t *source =
				obs_weak_source_get_source(action->target);
			if (!source)
				continue;
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
			float value;
			if (!nv_move_action_get_float(filter, action, true,
						      &value))
				continue;
			if (!action->name)
				continue;
			obs_source_t *scene_source =
				obs_weak_source_get_source(action->target);
			if (!scene_source)
				continue;
			obs_scene_t *scene =
				obs_scene_from_source(scene_source);
			if (!scene)
				scene = obs_group_from_source(scene_source);
			obs_source_release(scene_source);
			if (!scene)
				continue;
			obs_sceneitem_t *item =
				obs_scene_find_source(scene, action->name);
			if (!item)
				continue;
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
