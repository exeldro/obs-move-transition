extern "C" {
#include "move-transition.h"
}
#include <dshow.h>
#include <string>
#include <map>
#include <strmif.h>
#include <util/dstr.hpp>
#include "util/threading.h"

#define PROP_MAX 100

struct directshow_property {
	long int_from;
	long int_to;
	long flags;
};

struct move_directshow_info {
	struct move_filter move_filter;

	char *device;
	char *single_setting_name;

	IAMCameraControl *camControl;
	IAMVideoProcAmp *procAmp;
	int move_value_type;

	long int_to;
	long int_value;
	long int_from;
	long int_min;
	long int_max;

	std::map<long, struct directshow_property> *camControlProps;
	std::map<long, struct directshow_property> *procAmpProps;

	pthread_mutex_t mutex;
	pthread_t thread;
	bool run;
	os_event_t *start_event;
};

static inline void encode_dstr(struct dstr *str)
{
	dstr_replace(str, "#", "#22");
	dstr_replace(str, ":", "#3A");
}

static inline void decode_dstr(struct dstr *str)
{
	dstr_replace(str, "#3A", ":");
	dstr_replace(str, "#22", "#");
}

void LoadDevice(struct move_directshow_info *move_directshow)
{
	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->camControl) {
		move_directshow->camControl->Release();
		move_directshow->camControl = nullptr;
	}
	if (move_directshow->procAmp) {
		move_directshow->procAmp->Release();
		move_directshow->procAmp = nullptr;
	}
	pthread_mutex_unlock(&move_directshow->mutex);

	if (!strlen(move_directshow->device))
		return;

	ICreateDevEnum *deviceEnum;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **)&deviceEnum);
	if (FAILED(hr)) {
		//WarningHR(L"GetFilterByMedium: Failed to create device enum",			  hr);
		return;
	}

	IEnumMoniker *enumMoniker;
	hr = deviceEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
	if (hr != S_OK) {
		deviceEnum->Release();
		//WarningHR(L"GetFilterByMedium: Failed to create enum moniker",  hr);
		return;
	}
	IMoniker *deviceInfo;
	DWORD count = 0;
	while (enumMoniker->Next(1, &deviceInfo, &count) == S_OK) {

		IPropertyBag *propertyData;
		hr = deviceInfo->BindToStorage(0, 0, IID_IPropertyBag, (void **)&propertyData);
		if (hr == S_OK) {
			VARIANT deviceName, devicePath;
			deviceName.vt = VT_BSTR;
			devicePath.vt = VT_BSTR;
			devicePath.bstrVal = nullptr;

			DStr name;
			hr = propertyData->Read(L"FriendlyName", &deviceName, nullptr);
			if (hr == S_OK) {
				dstr_from_wcs(name, deviceName.bstrVal);
			}
			DStr path;
			hr = propertyData->Read(L"DevicePath", &devicePath, nullptr);
			if (hr == S_OK && devicePath.bstrVal != nullptr) {
				dstr_from_wcs(path, devicePath.bstrVal);

				encode_dstr(path);
			}
			propertyData->Release();

			DStr device_id;
			dstr_copy_dstr(device_id, name);
			encode_dstr(device_id);
			dstr_cat(device_id, ":");
			dstr_cat_dstr(device_id, path);
			if (strcmp(device_id, move_directshow->device) == 0) {
				break;
			}
		}
		deviceInfo->Release();
		deviceInfo = nullptr;
	}
	enumMoniker->Release();
	deviceEnum->Release();
	if (!deviceInfo)
		return;

	IBaseFilter *filter;
	hr = deviceInfo->BindToObject(nullptr, 0, IID_IBaseFilter, (void **)&filter);
	deviceInfo->Release();
	if (hr != S_OK)
		return;

	filter->QueryInterface(IID_IAMCameraControl, (void **)&move_directshow->camControl);
	filter->QueryInterface(IID_IAMVideoProcAmp, (void **)&move_directshow->procAmp);
	filter->Release();
}

void LoadProperties(move_directshow_info *move_directshow, obs_data_t *settings, bool overwrite)
{
	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->camControl) {
		for (int i = 0; i < PROP_MAX; i++) {
			char number[4];
			snprintf(number, 4, "%i", i);
			std::string prop_id = "camera_control_";
			prop_id += number;
			std::string prop_enabled = prop_id;
			prop_enabled += "_enabled";
			auto m = move_directshow->camControlProps->find(i);

			if (!obs_data_get_bool(settings, prop_enabled.c_str())) {
				if (m != move_directshow->camControlProps->end())
					move_directshow->camControlProps->erase(m);
				continue;
			}

			long val, flags;
			HRESULT hr = move_directshow->camControl->Get(i, &val, &flags);
			if (hr == S_OK) {
				if (m == move_directshow->camControlProps->end()) {
					struct directshow_property p;
					p.int_from = val;
					if (settings) {
						p.int_to = (long)obs_data_get_int(settings, prop_id.c_str());
					} else {
						p.int_to = val;
					}
					move_directshow->camControlProps->emplace(i, p);
				} else {
					if (overwrite)
						m->second.int_from = val;
					if (settings) {
						m->second.int_to = (long)obs_data_get_int(settings, prop_id.c_str());
					}
				}
			} else if (m != move_directshow->camControlProps->end()) {
				move_directshow->camControlProps->erase(m);
			}
		}
	}
	if (move_directshow->procAmp) {
		for (int i = 0; i < PROP_MAX; i++) {
			char number[4];
			snprintf(number, 4, "%i", i);
			std::string prop_id = "video_proc_amp_";
			prop_id += number;
			std::string prop_enabled = prop_id;
			prop_enabled += "_enabled";

			auto m = move_directshow->procAmpProps->find(i);

			if (!obs_data_get_bool(settings, prop_enabled.c_str())) {
				if (m != move_directshow->procAmpProps->end())
					move_directshow->procAmpProps->erase(m);
				continue;
			}

			long val, flags;
			HRESULT hr = move_directshow->procAmp->Get(i, &val, &flags);
			if (hr == S_OK) {
				if (m == move_directshow->procAmpProps->end()) {
					struct directshow_property p;
					p.int_from = val;
					if (settings) {
						p.int_to = (long)obs_data_get_int(settings, prop_id.c_str());
					} else {
						p.int_to = val;
					}

					move_directshow->procAmpProps->emplace(i, p);

				} else {
					if (overwrite)
						m->second.int_from = val;
					if (settings) {
						m->second.int_to = (long)obs_data_get_int(settings, prop_id.c_str());
					}
				}
			} else if (m != move_directshow->procAmpProps->end()) {
				move_directshow->procAmpProps->erase(m);
			}
		}
	}
	pthread_mutex_unlock(&move_directshow->mutex);
}

void move_directshow_update(void *data, obs_data_t *settings)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	move_filter_update(&move_directshow->move_filter, settings);
	auto device = obs_data_get_string(settings, "device");
	if (!move_directshow->device || strcmp(move_directshow->device, device) != 0) {
		bfree(move_directshow->device);
		move_directshow->device = bstrdup(device);
		LoadDevice(move_directshow);
	}
	move_directshow->move_value_type = (int)obs_data_get_int(settings, S_MOVE_VALUE_TYPE);
	if (move_directshow->move_value_type == MOVE_VALUE_TYPE_SETTINGS) {
		LoadProperties(move_directshow, settings, false);
	} else {
		move_directshow->int_value = (long)obs_data_get_int(settings, S_SETTING_INT);
		move_directshow->int_min = (long)obs_data_get_int(settings, S_SETTING_INT_MIN);
		move_directshow->int_max = (long)obs_data_get_int(settings, S_SETTING_INT_MAX);
		auto single_setting_name = obs_data_get_string(settings, S_SETTING_NAME);
		if (!move_directshow->single_setting_name ||
		    strcmp(move_directshow->single_setting_name, single_setting_name) != 0) {
			bfree(move_directshow->single_setting_name);
			move_directshow->single_setting_name = bstrdup(single_setting_name);
		}
	}
}

static const char *move_directshow_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("MoveDirectshowFilter");
}

long rand_between(long a, long b)
{
	if (a == b)
		return a;
	return b > a ? a + rand() % (b - a) : b + rand() % (a - b);
}

void move_directshow_start(void *data)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	if (!move_filter_start_internal(&move_directshow->move_filter))
		return;

	if (move_directshow->move_filter.reverse)
		return;

	if (move_directshow->move_value_type == MOVE_VALUE_TYPE_SETTINGS) {
		obs_data_t *settings = obs_source_get_settings(move_directshow->move_filter.source);
		LoadProperties(move_directshow, settings, true);
		obs_data_release(settings);
	} else if (move_directshow->single_setting_name) {
		long i;
		pthread_mutex_lock(&move_directshow->mutex);
		if (move_directshow->camControl && 1 == sscanf(move_directshow->single_setting_name, "camera_control_%i", &i)) {
			long val, flags;
			HRESULT hr = move_directshow->camControl->Get(i, &val, &flags);
			if (hr == S_OK) {
				move_directshow->int_from = val;
				if (move_directshow->move_value_type == MOVE_VALUE_TYPE_SETTING_ADD) {
					move_directshow->int_to = move_directshow->int_from + move_directshow->int_value;
					long min, max, delta, default_val, caps;
					hr = move_directshow->camControl->GetRange(i, &min, &max, &delta, &default_val, &caps);
					if (hr == S_OK) {
						if (move_directshow->int_to < min)
							move_directshow->int_to = min;
						if (move_directshow->int_to > max)
							move_directshow->int_to = max;
					}
				}
			}
		} else if (move_directshow->procAmp && 1 == sscanf(move_directshow->single_setting_name, "video_proc_amp_%i", &i)) {
			long val, flags;
			HRESULT hr = move_directshow->procAmp->Get(i, &val, &flags);
			if (hr == S_OK) {
				move_directshow->int_from = val;
				if (move_directshow->move_value_type == MOVE_VALUE_TYPE_SETTING_ADD) {
					move_directshow->int_to = move_directshow->int_from + move_directshow->int_value;
					long min, max, delta, default_val, caps;
					hr = move_directshow->procAmp->GetRange(i, &min, &max, &delta, &default_val, &caps);
					if (hr == S_OK) {
						if (move_directshow->int_to < min)
							move_directshow->int_to = min;
						if (move_directshow->int_to > max)
							move_directshow->int_to = max;
					}
				}
			}
		}
		pthread_mutex_unlock(&move_directshow->mutex);
		if (move_directshow->move_value_type == MOVE_VALUE_TYPE_RANDOM) {
			move_directshow->int_to = rand_between(move_directshow->int_min, move_directshow->int_max);
		} else if (move_directshow->move_value_type != MOVE_VALUE_TYPE_SETTING_ADD) {
			move_directshow->int_to = move_directshow->int_value;
		}
	}

	os_event_signal(move_directshow->start_event);
}

static void move_directshow_to_time(struct move_directshow_info *move_directshow, float t)
{
	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->move_value_type == MOVE_VALUE_TYPE_SETTINGS) {
		if (move_directshow->camControl) {
			for (auto prop = move_directshow->camControlProps->begin(); prop != move_directshow->camControlProps->end();
			     ++prop) {
				const long value_int =
					(long long)((1.0f - t) * (float)(prop->second.int_from) + t * (float)prop->second.int_to);
				long val, flags;
				HRESULT hr = move_directshow->camControl->Get(prop->first, &val, &flags);
				if (hr == S_OK && val != value_int) {
					move_directshow->camControl->Set(prop->first, value_int, flags);
				}
			}
		}
		if (move_directshow->procAmp) {
			for (auto prop = move_directshow->procAmpProps->begin(); prop != move_directshow->procAmpProps->end();
			     ++prop) {
				const long value_int =
					(long long)((1.0f - t) * (float)(prop->second.int_from) + t * (float)prop->second.int_to);
				long val, flags;
				HRESULT hr = move_directshow->procAmp->Get(prop->first, &val, &flags);
				if (hr == S_OK && val != value_int) {
					move_directshow->procAmp->Set(prop->first, value_int, flags);
				}
			}
		}
	} else if (move_directshow->single_setting_name) {
		const long value_int =
			(long long)((1.0f - t) * (float)move_directshow->int_from + t * (float)move_directshow->int_to);
		long i;
		if (move_directshow->camControl && 1 == sscanf(move_directshow->single_setting_name, "camera_control_%i", &i)) {
			long val, flags;
			HRESULT hr = move_directshow->camControl->Get(i, &val, &flags);
			if (hr == S_OK && val != value_int) {
				move_directshow->camControl->Set(i, value_int, flags);
			}
		} else if (move_directshow->procAmp && 1 == sscanf(move_directshow->single_setting_name, "video_proc_amp_%i", &i)) {
			long val, flags;
			HRESULT hr = move_directshow->procAmp->Get(i, &val, &flags);
			if (hr == S_OK && val != value_int) {
				move_directshow->procAmp->Set(i, value_int, flags);
			}
		}
	}
	pthread_mutex_unlock(&move_directshow->mutex);
}

extern "C" {
float get_eased(float f, long long easing, long long easing_function);
}

static void *move_directshow_thread(void *data)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;

	os_set_thread_name("move_directshow_thread");

	while (move_directshow->run) {
		os_event_wait(move_directshow->start_event);

		if (!move_directshow->run)
			break;

		while (move_directshow->run && move_directshow->move_filter.moving) {
			float t = (move_directshow->move_filter.running_duration * 1000.0f -
				   (float)(move_directshow->move_filter.reverse ? move_directshow->move_filter.end_delay
										: move_directshow->move_filter.start_delay)) /
				  (float)move_directshow->move_filter.duration;
			if (t >= 1.0f) {
				t = 1.0f;
			}
			if (move_directshow->move_filter.reverse) {
				t = 1.0f - t;
			}
			t = get_eased(t, move_directshow->move_filter.easing, move_directshow->move_filter.easing_function);
			move_directshow_to_time(move_directshow, t);
			Sleep(0);
		}
		move_directshow_to_time(move_directshow, 1.0f);
	}
	return NULL;
}

static void *move_directshow_create(obs_data_t *settings, obs_source_t *source)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)bzalloc(sizeof(struct move_directshow_info));
	move_filter_init(&move_directshow->move_filter, source, move_directshow_start);
	move_directshow->camControlProps = new std::map<long, directshow_property>();
	move_directshow->procAmpProps = new std::map<long, directshow_property>();
	pthread_mutex_init(&move_directshow->mutex, nullptr);
	os_event_init(&move_directshow->start_event, OS_EVENT_TYPE_AUTO);
	obs_source_update(source, NULL);
	move_directshow->run = true;
	pthread_create(&move_directshow->thread, NULL, move_directshow_thread, move_directshow);
	return move_directshow;
}

static void move_directshow_destroy(void *data)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	move_directshow->run = false;
	os_event_signal(move_directshow->start_event);
	pthread_join(move_directshow->thread, NULL);

	move_filter_destroy(&move_directshow->move_filter);

	bfree(move_directshow->device);
	bfree(move_directshow->single_setting_name);
	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->camControl)
		move_directshow->camControl->Release();
	if (move_directshow->procAmp)
		move_directshow->procAmp->Release();

	move_directshow->camControlProps->clear();
	move_directshow->procAmpProps->clear();
	pthread_mutex_unlock(&move_directshow->mutex);
	delete move_directshow->camControlProps;
	delete move_directshow->procAmpProps;
	pthread_mutex_destroy(&move_directshow->mutex);
	os_event_destroy(move_directshow->start_event);
	bfree(move_directshow);
}

static bool device_modified(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)priv;
	auto device = obs_data_get_string(settings, "device");

	bool changed = false;
	auto single_setting_name = obs_data_get_string(settings, S_SETTING_NAME);
	bool single_setting_changed = false;
	if (!move_directshow->single_setting_name || strcmp(move_directshow->single_setting_name, single_setting_name) != 0) {
		bfree(move_directshow->single_setting_name);
		move_directshow->single_setting_name = bstrdup(single_setting_name);
		single_setting_changed = true;
	}
	auto single = obs_properties_get(props, S_SETTING_NAME);
	auto single_int = obs_properties_get(props, S_SETTING_INT);
	auto prop_int_min = obs_properties_get(props, S_SETTING_INT_MIN);
	auto prop_int_max = obs_properties_get(props, S_SETTING_INT_MAX);

	auto move_type = (int)obs_data_get_int(settings, S_MOVE_VALUE_TYPE);
	if (move_type != move_directshow->move_value_type) {
		changed = true;
		move_directshow->move_value_type = move_type;
	}
	if (move_type == MOVE_VALUE_TYPE_SETTINGS) {
		if (obs_property_visible(single)) {
			obs_property_set_visible(single, false);
			changed = true;
		}
		single = nullptr;
		if (obs_property_visible(single_int)) {
			obs_property_set_visible(single_int, false);
			changed = true;
		}
		if (obs_property_visible(prop_int_min)) {
			obs_property_set_visible(prop_int_min, false);
			changed = true;
		}
		if (obs_property_visible(prop_int_max)) {
			obs_property_set_visible(prop_int_max, false);
			changed = true;
		}
	} else {
		if (!obs_property_visible(single)) {
			obs_property_set_visible(single, true);
			changed = true;
		}
		if (move_type == MOVE_VALUE_TYPE_RANDOM) {
			if (obs_property_visible(single_int)) {
				obs_property_set_visible(single_int, false);
				changed = true;
			}
			if (!obs_property_visible(prop_int_min)) {
				obs_property_set_visible(prop_int_min, true);
				changed = true;
			}
			if (!obs_property_visible(prop_int_max)) {
				obs_property_set_visible(prop_int_max, true);
				changed = true;
			}
		} else {
			if (!obs_property_visible(single_int)) {
				obs_property_set_visible(single_int, true);
				changed = true;
			}
			if (obs_property_visible(prop_int_min)) {
				obs_property_set_visible(prop_int_min, false);
				changed = true;
			}
			if (obs_property_visible(prop_int_max)) {
				obs_property_set_visible(prop_int_max, false);
				changed = true;
			}
		}
		auto p = obs_properties_get(props, "camcontrol_group");
		if (p && obs_property_visible(p)) {
			obs_property_set_visible(p, false);
			changed = true;
		}
		p = obs_properties_get(props, "procamp_group");
		if (p && obs_property_visible(p)) {
			obs_property_set_visible(p, false);
			changed = true;
		}
	}
	if (!move_directshow->device || strcmp(move_directshow->device, device) != 0) {
		bfree(move_directshow->device);
		move_directshow->device = bstrdup(device);
		LoadDevice(move_directshow);
		changed = true;
	}

	if (!device || !strlen(device)) {
		if (single)
			obs_property_list_clear(single);
		auto p = obs_properties_get(props, "camcontrol_group");
		if (p && obs_property_visible(p)) {
			obs_property_set_visible(p, false);
			changed = true;
		}
		p = obs_properties_get(props, "procamp_group");
		if (p && obs_property_visible(p)) {
			obs_property_set_visible(p, false);
			changed = true;
		}
		return changed;
	}
	if (!changed)
		return changed;
	obs_property_list_clear(single);
	auto camcontrol_prop = obs_properties_get(props, "camcontrol_group");
	obs_properties_t *camcontrolGroup = nullptr;
	if (camcontrol_prop)
		camcontrolGroup = obs_property_group_content(camcontrol_prop);

	if (!camcontrolGroup) {
		camcontrolGroup = obs_properties_create();
		camcontrol_prop = obs_properties_add_group(props, "camcontrol_group", obs_module_text("CameraControl"),
							   OBS_GROUP_NORMAL, camcontrolGroup);
	} else {
		auto p = obs_properties_first(camcontrolGroup);
		while (p) {
			obs_properties_remove_by_name(camcontrolGroup, obs_property_name(p));
			p = obs_properties_first(camcontrolGroup);
		}
	}
	auto procAmp_prop = obs_properties_get(props, "procamp_group");
	obs_properties_t *procAmpGroup = nullptr;
	if (procAmp_prop)
		procAmpGroup = obs_property_group_content(procAmp_prop);

	if (!procAmpGroup) {
		procAmpGroup = obs_properties_create();
		procAmp_prop = obs_properties_add_group(props, "procamp_group", obs_module_text("VideoProcAmp"), OBS_GROUP_NORMAL,
							procAmpGroup);
	} else {
		auto p = obs_properties_first(procAmpGroup);
		while (p) {
			obs_properties_remove_by_name(procAmpGroup, obs_property_name(p));
			p = obs_properties_first(procAmpGroup);
		}
	}
	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->camControl) {
		for (int i = 0; i < PROP_MAX; i++) {
			long min, max, delta, default_val, caps;
			HRESULT hr = move_directshow->camControl->GetRange(i, &min, &max, &delta, &default_val, &caps);
			if (hr == S_OK) {
				char number[4];
				snprintf(number, 4, "%i", i);
				std::string name = obs_module_text("Property");
				name += " ";
				name += number;
				if (i == 0) {
					name = obs_module_text("Pan");
				} else if (i == 1) {
					name = obs_module_text("Tilt");
				} else if (i == 2) {
					name = obs_module_text("Roll");
				} else if (i == 3) {
					name = obs_module_text("Zoom");
				} else if (i == 4) {
					name = obs_module_text("Exposure");
				} else if (i == 5) {
					name = obs_module_text("Iris");
				} else if (i == 6) {
					name = obs_module_text("Focus");
				} else {
					int j = 0;
				}
				std::string prop_id = "camera_control_";
				prop_id += number;
				if (single) {
					std::string prop = obs_module_text("CameraControl");
					prop += " ";
					prop += name;
					obs_property_list_add_string(single, prop.c_str(), prop_id.c_str());
					if (strcmp(single_setting_name, prop_id.c_str()) == 0) {
						auto p = obs_properties_get(props, S_SETTING_INT);
						if (move_type == MOVE_VALUE_TYPE_SETTING_ADD) {
							obs_property_int_set_limits(p, -(max - min), max - min, delta);
						} else {
							obs_property_int_set_limits(p, min, max, delta);
						}
						p = obs_properties_get(props, S_SETTING_INT_MIN);
						obs_property_int_set_limits(p, min, max, delta);
						p = obs_properties_get(props, S_SETTING_INT_MAX);
						obs_property_int_set_limits(p, min, max, delta);
					}
				} else {
					std::string prop_enabled = prop_id;
					prop_enabled += "_enabled";
					obs_properties_t *prop_group = obs_properties_create();
					obs_properties_add_int_slider(prop_group, prop_id.c_str(), name.c_str(), min, max, delta);
					obs_properties_add_group(camcontrolGroup, prop_enabled.c_str(), name.c_str(),
								 OBS_GROUP_CHECKABLE, prop_group);
				}
			}
		}
	}
	if (move_directshow->procAmp) {
		for (int i = 0; i < PROP_MAX; i++) {
			long min, max, delta, default_val, caps;
			HRESULT hr = move_directshow->procAmp->GetRange(i, &min, &max, &delta, &default_val, &caps);
			if (hr == S_OK) {
				char number[4];
				snprintf(number, 4, "%i", i);
				std::string name = obs_module_text("Property");
				name += " ";
				name += number;
				if (i == 0) {
					name = obs_module_text("Brightness");
				} else if (i == 1) {
					name = obs_module_text("Contrast");
				} else if (i == 2) {
					name = obs_module_text("Hue");
				} else if (i == 3) {
					name = obs_module_text("Saturation");
				} else if (i == 4) {
					name = obs_module_text("Sharpness");
				} else if (i == 5) {
					name = obs_module_text("Gamma");
				} else if (i == 6) {
					name = obs_module_text("ColorEnable");
				} else if (i == 7) {
					name = obs_module_text("WhiteBalance");
				} else if (i == 8) {
					name = obs_module_text("BacklightCompensation");
				} else if (i == 9) {
					name = obs_module_text("Gain");
				} else {
					int j = 0;
				}
				std::string prop_id = "video_proc_amp_";
				prop_id += number;
				if (single) {
					std::string prop = obs_module_text("VideoProcAmp");
					prop += " ";
					prop += name;
					obs_property_list_add_string(single, prop.c_str(), prop_id.c_str());
					if (strcmp(single_setting_name, prop_id.c_str()) == 0) {
						auto p = obs_properties_get(props, S_SETTING_INT);
						if (move_type == MOVE_VALUE_TYPE_SETTING_ADD) {
							obs_property_int_set_limits(p, -(max - min), max - min, delta);
						} else {
							obs_property_int_set_limits(p, min, max, delta);
						}
						p = obs_properties_get(props, S_SETTING_INT_MIN);
						obs_property_int_set_limits(p, min, max, delta);
						p = obs_properties_get(props, S_SETTING_INT_MAX);
						obs_property_int_set_limits(p, min, max, delta);
					}
				} else {
					std::string prop_enabled = prop_id;
					prop_enabled += "_enabled";
					obs_properties_t *prop_group = obs_properties_create();
					obs_properties_add_int_slider(prop_group, prop_id.c_str(), name.c_str(), min, max, delta);

					obs_properties_add_group(procAmpGroup, prop_enabled.c_str(), name.c_str(),
								 OBS_GROUP_CHECKABLE, prop_group);
				}
			}
		}
	}
	pthread_mutex_unlock(&move_directshow->mutex);
	if (!single) {
		if (!obs_property_visible(camcontrol_prop))
			obs_property_set_visible(camcontrol_prop, true);
	} else {
		if (obs_property_visible(camcontrol_prop))
			obs_property_set_visible(camcontrol_prop, false);
	}

	if (!single) {
		if (!obs_property_visible(procAmp_prop))
			obs_property_set_visible(procAmp_prop, true);
	} else {
		if (obs_property_visible(procAmp_prop))
			obs_property_set_visible(procAmp_prop, false);
	}

	return true;
}

bool move_directshow_get_value(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	const auto settings = obs_source_get_settings(move_directshow->move_filter.source);

	pthread_mutex_lock(&move_directshow->mutex);
	if (move_directshow->camControl) {
		for (int i = 0; i < PROP_MAX; i++) {
			long val, flags;
			HRESULT hr = move_directshow->camControl->Get(i, &val, &flags);
			if (hr == S_OK) {
				char number[4];
				snprintf(number, 4, "%i", i);
				std::string prop_id = "camera_control_";
				prop_id += number;
				if (move_directshow->single_setting_name &&
				    strcmp(move_directshow->single_setting_name, prop_id.c_str()) == 0) {
					obs_data_set_int(settings, S_SETTING_INT, val);
					if (move_directshow->move_value_type == MOVE_VALUE_TYPE_RANDOM) {
						long min, max, delta, default_val, caps;
						hr = move_directshow->camControl->GetRange(i, &min, &max, &delta, &default_val,
											   &caps);
						if (hr == S_OK) {
							obs_data_set_int(settings, S_SETTING_INT_MIN, min);
							obs_data_set_int(settings, S_SETTING_INT_MAX, max);
						}
					}
				}
				obs_data_set_int(settings, prop_id.c_str(), val);
			}
		}
	}
	if (move_directshow->procAmp) {
		for (int i = 0; i < PROP_MAX; i++) {
			long val, flags;
			HRESULT hr = move_directshow->procAmp->Get(i, &val, &flags);
			if (hr == S_OK) {
				char number[4];
				snprintf(number, 4, "%i", i);
				std::string prop_id = "video_proc_amp_";
				prop_id += number;
				if (move_directshow->single_setting_name &&
				    strcmp(move_directshow->single_setting_name, prop_id.c_str()) == 0) {
					obs_data_set_int(settings, S_SETTING_INT, val);
					if (move_directshow->move_value_type == MOVE_VALUE_TYPE_RANDOM) {
						long min, max, delta, default_val, caps;
						hr = move_directshow->procAmp->GetRange(i, &min, &max, &delta, &default_val, &caps);
						if (hr == S_OK) {
							obs_data_set_int(settings, S_SETTING_INT_MIN, min);
							obs_data_set_int(settings, S_SETTING_INT_MAX, max);
						}
					}
				}
				obs_data_set_int(settings, prop_id.c_str(), val);
			}
		}
	}
	pthread_mutex_unlock(&move_directshow->mutex);
	move_directshow_update(data, settings);
	obs_data_release(settings);
	return true;
}

static obs_properties_t *move_directshow_properties(void *data)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(ppts, "device", obs_module_text("Device"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	ICreateDevEnum *deviceEnum;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **)&deviceEnum);
	if (FAILED(hr)) {
		//WarningHR(L"GetFilterByMedium: Failed to create device enum",			  hr);
		return ppts;
	}

	IEnumMoniker *enumMoniker;
	hr = deviceEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
	if (hr != S_OK) {
		deviceEnum->Release();
		//WarningHR(L"GetFilterByMedium: Failed to create enum moniker",  hr);
		return ppts;
	}
	IMoniker *deviceInfo;
	DWORD count = 0;
	while (enumMoniker->Next(1, &deviceInfo, &count) == S_OK) {
		IPropertyBag *propertyData;
		hr = deviceInfo->BindToStorage(0, 0, IID_IPropertyBag, (void **)&propertyData);
		if (hr == S_OK) {
			VARIANT deviceName, devicePath;
			deviceName.vt = VT_BSTR;
			devicePath.vt = VT_BSTR;
			devicePath.bstrVal = nullptr;

			DStr name;
			hr = propertyData->Read(L"FriendlyName", &deviceName, nullptr);
			if (hr == S_OK) {
				dstr_from_wcs(name, deviceName.bstrVal);
			}
			DStr path;
			hr = propertyData->Read(L"DevicePath", &devicePath, nullptr);
			if (hr == S_OK && devicePath.bstrVal != nullptr) {
				dstr_from_wcs(path, devicePath.bstrVal);

				encode_dstr(path);
			}
			DStr device_id;
			dstr_copy_dstr(device_id, name);
			encode_dstr(device_id);
			dstr_cat(device_id, ":");
			dstr_cat_dstr(device_id, path);

			obs_property_list_add_string(p, name, device_id);

			propertyData->Release();
		}

		deviceInfo->Release();
	}
	enumMoniker->Release();
	deviceEnum->Release();

	obs_property_set_modified_callback2(p, device_modified, data);

	p = obs_properties_add_list(ppts, S_MOVE_VALUE_TYPE, obs_module_text("MoveValueType"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("MoveValueType.SingleSetting"), MOVE_VALUE_TYPE_SINGLE_SETTING);
	obs_property_list_add_int(p, obs_module_text("MoveValueType.Settings"), MOVE_VALUE_TYPE_SETTINGS);
	obs_property_list_add_int(p, obs_module_text("MoveValueType.Random"), MOVE_VALUE_TYPE_RANDOM);
	obs_property_list_add_int(p, obs_module_text("MoveValueType.SettingAdd"), MOVE_VALUE_TYPE_SETTING_ADD);

	obs_property_set_modified_callback2(p, device_modified, data);

	p = obs_properties_add_list(ppts, S_SETTING_NAME, obs_module_text("Setting"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback2(p, device_modified, data);

	p = obs_properties_add_int_slider(ppts, S_SETTING_INT, obs_module_text("Value"), 0, 0, 1);
	obs_property_set_visible(p, false);
	p = obs_properties_add_int_slider(ppts, S_SETTING_INT_MIN, obs_module_text("MinValue"), 0, 0, 1);
	obs_property_set_visible(p, false);
	p = obs_properties_add_int_slider(ppts, S_SETTING_INT_MAX, obs_module_text("MaxValue"), 0, 0, 1);
	obs_property_set_visible(p, false);

	obs_properties_add_group(ppts, "camcontrol_group", obs_module_text("CameraControl"), OBS_GROUP_NORMAL,
				 obs_properties_create());

	obs_properties_add_group(ppts, "procamp_group", obs_module_text("VideoProcAmp"), OBS_GROUP_NORMAL, obs_properties_create());

	obs_properties_add_button(ppts, "value_get", obs_module_text("GetValue"), move_directshow_get_value);

	move_filter_properties(&move_directshow->move_filter, ppts);

	return ppts;
}

void move_directshow_tick(void *data, float seconds)
{
	struct move_directshow_info *move_directshow = (struct move_directshow_info *)data;
	float t;
	if (!move_filter_tick(&move_directshow->move_filter, seconds, &t))
		return;
	if (!move_directshow->move_filter.moving)
		move_filter_ended(&move_directshow->move_filter);
}

extern "C" {
void SetMoveDirectShowFilter(struct obs_source_info *info)
{
	info->id = MOVE_DIRECTSHOW_FILTER_ID;
	info->type = OBS_SOURCE_TYPE_FILTER;
	info->output_flags = OBS_SOURCE_VIDEO;
	info->get_name = move_directshow_get_name;
	info->create = move_directshow_create;
	info->destroy = move_directshow_destroy;
	info->get_properties = move_directshow_properties;
	info->video_tick = move_directshow_tick;
	info->update = move_directshow_update;
	info->load = move_directshow_update;
	info->get_defaults = move_filter_defaults;
	info->activate = move_filter_activate;
	info->deactivate = move_filter_deactivate;
	info->show = move_filter_show;
	info->hide = move_filter_hide;
}
};
