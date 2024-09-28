#include "move-transition.h"
#include "obs-frontend-api.h"
#include <util/dstr.h>
#include <util/darray.h>
#include <util/threading.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Mstcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#define SOCKET int
#define closesocket(s) close(s)
#endif

struct udp_server {
	int port;
	pthread_t thread;
	DARRAY(struct move_filter *) start_triggers;
	DARRAY(struct move_filter *) stop_triggers;
};

DARRAY(struct udp_server) udp_servers;
pthread_mutex_t udp_servers_mutex;

bool is_move_filter(const char *filter_id)
{
	if (!filter_id)
		return false;
	return strcmp(filter_id, MOVE_SOURCE_FILTER_ID) == 0 || strcmp(filter_id, MOVE_SOURCE_SWAP_FILTER_ID) == 0 ||
	       strcmp(filter_id, MOVE_VALUE_FILTER_ID) == 0 || strcmp(filter_id, MOVE_AUDIO_VALUE_FILTER_ID) == 0 ||
	       strcmp(filter_id, MOVE_ACTION_FILTER_ID) == 0 || strcmp(filter_id, MOVE_AUDIO_ACTION_FILTER_ID) == 0 ||
	       strcmp(filter_id, MOVE_DIRECTSHOW_FILTER_ID) == 0;
}

void move_filter_init(struct move_filter *move_filter, obs_source_t *source, void (*move_start)(void *data))
{
	move_filter->source = source;
	move_filter->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
	move_filter->move_hold_hotkey = OBS_INVALID_HOTKEY_ID;
	move_filter->move_start = move_start;
}

void stop_udp_thread(struct udp_server *udp_server)
{
	SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd >= 0) {
		struct sockaddr_in server;
		struct hostent *host = gethostbyname("127.0.0.1");

		memset(&server, 0, sizeof(server));
		server.sin_family = AF_INET;
		server.sin_addr = *((struct in_addr *)host->h_addr);
		server.sin_port = htons(udp_server->port);
		char *exit_message = "exit";
		sendto(sockfd, (const char *)exit_message, (int)strlen(exit_message), 0, (const struct sockaddr *)&server,
		       sizeof(server));
		closesocket(sockfd);
	}
}

void move_filter_destroy(struct move_filter *move_filter)
{
	pthread_mutex_lock(&udp_servers_mutex);
	for (size_t i = 0; i < udp_servers.num; i++) {
		for (size_t j = 0; j < udp_servers.array[j].start_triggers.num; j++) {
			if (udp_servers.array[i].start_triggers.array[j] != move_filter)
				continue;
			da_erase(udp_servers.array[i].start_triggers, j);
			if (!udp_servers.array[i].start_triggers.num && !udp_servers.array[j].stop_triggers.num) {
				stop_udp_thread(&udp_servers.array[i]);
			}
			break;
		}
		for (size_t j = 0; j < udp_servers.array[j].stop_triggers.num; j++) {
			if (udp_servers.array[i].stop_triggers.array[j] != move_filter)
				continue;
			da_erase(udp_servers.array[i].stop_triggers, j);
			if (!udp_servers.array[i].start_triggers.num && !udp_servers.array[j].stop_triggers.num) {
				stop_udp_thread(&udp_servers.array[i]);
			}
			break;
		}
	}
	pthread_mutex_unlock(&udp_servers_mutex);

	bfree(move_filter->filter_name);
	bfree(move_filter->simultaneous_move_name);
	bfree(move_filter->next_move_name);

	if (move_filter->move_start_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(move_filter->move_start_hotkey);
	if (move_filter->move_hold_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(move_filter->move_hold_hotkey);
}

void move_filter_start_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	struct move_filter *move_filter = data;

	if (move_filter->next_move_on != NEXT_MOVE_ON_HOTKEY || !move_filter->next_move_name ||
	    !strlen(move_filter->next_move_name)) {
		move_filter_start(move_filter);
		return;
	}
	if (!move_filter->filters_done.num) {
		move_filter_start(move_filter);
		da_push_back(move_filter->filters_done, &move_filter->source);
		return;
	}

	if (move_filter->moving && obs_source_enabled(move_filter->source) && move_filter->next_move_name &&
	    strcmp(move_filter->next_move_name, NEXT_MOVE_REVERSE) != 0) {
		move_filter->moving = false;
		if (move_filter->enabled_match_moving)
			obs_source_set_enabled(move_filter->source, false);
	}

	char *next_move_name = move_filter->next_move_name;
	obs_source_t *filter = move_filter->source;
	obs_source_t *parent = obs_filter_get_parent(filter);
	long long next_move_on = move_filter->next_move_on;
	size_t i = 0;
	while (i < move_filter->filters_done.num) {
		if (!next_move_name || !strlen(next_move_name)) {
			move_filter_start(move_filter);
			move_filter->filters_done.num = 0;
			da_push_back(move_filter->filters_done, &move_filter->source);
			return;
		}
		if (next_move_on != NEXT_MOVE_ON_HOTKEY) {
			da_push_back(move_filter->filters_done, &filter);
		} else if (strcmp(next_move_name, NEXT_MOVE_REVERSE) == 0 && !obs_source_removed(filter) &&
			   is_move_filter(obs_source_get_unversioned_id(filter))) {
			move_filter_start(obs_obj_get_data(filter));
			move_filter->filters_done.num = 0;
			return;
		}
		filter = obs_source_get_filter_by_name(parent, next_move_name);
		if (!filter && move_filter->get_alternative_filter) {
			filter = move_filter->get_alternative_filter(move_filter, next_move_name);
		}

		if (filter) {
			if (!obs_source_removed(filter) && is_move_filter(obs_source_get_unversioned_id(filter))) {
				struct move_filter *filter_data = obs_obj_get_data(filter);
				if (filter_data->moving && obs_source_enabled(filter_data->source) &&
				    (filter_data->reverse || !filter_data->next_move_name ||
				     strcmp(filter_data->next_move_name, NEXT_MOVE_REVERSE) != 0)) {
					filter_data->moving = false;
					if (filter_data->enabled_match_moving)
						obs_source_set_enabled(filter_data->source, false);
				}
				parent = obs_filter_get_parent(filter);
				next_move_name = filter_data->next_move_name;
				next_move_on = filter_data->next_move_on;
			} else {
				obs_source_release(filter);
				move_filter_start(move_filter);
				move_filter->filters_done.num = 0;
				da_push_back(move_filter->filters_done, &move_filter->source);
				return;
			}
			obs_source_release(filter);
		}

		i++;
	}
	for (i = 0; i < move_filter->filters_done.num; i++) {
		if (move_filter->filters_done.array[i] == filter) {
			move_filter_start(move_filter);
			move_filter->filters_done.num = 0;
			da_push_back(move_filter->filters_done, &move_filter->source);
			return;
		}
	}
	if (!obs_source_removed(filter) && is_move_filter(obs_source_get_unversioned_id(filter))) {
		move_filter_start(obs_obj_get_data(filter));
	}
	da_push_back(move_filter->filters_done, &filter);
}

void move_filter_hold_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	struct move_filter *move_filter = data;
	if (pressed) {
		move_filter_start(move_filter);
		move_filter->holding = true;
		return;
	}
	move_filter->running_duration = (float)(move_filter->start_delay + move_filter->duration) / 1000.0f;

	move_filter->holding = false;
}

static void *udp_server_thread(void *data)
{
	os_set_thread_name("move_udp_server_thread");

	struct udp_server *udp_server = data;

	SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0)
		return NULL;

	struct sockaddr_in si_me;
	memset((char *)&si_me, 0, sizeof(si_me));

	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(udp_server->port);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sockfd, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
		return NULL;

	struct sockaddr_in si_other;
	int recv_len;
	int port = udp_server->port;
	pthread_t self = pthread_self();

#define BUFLEN 512
	char buf[BUFLEN];
	socklen_t slen = sizeof(si_other);
	while (true) {
		recv_len = recvfrom(sockfd, buf, BUFLEN, 0, (struct sockaddr *)&si_other, &slen);
		pthread_mutex_lock(&udp_servers_mutex);
		if (recv_len == -1)
			break;

		udp_server = NULL;
		for (size_t i = 0; i < udp_servers.num; i++) {
			if (udp_servers.array[i].port == port) {
				udp_server = &udp_servers.array[i];
				break;
			} else if (pthread_equal(udp_servers.array[i].thread, self)) {
				udp_server = &udp_servers.array[i];
				break;
			}
		}
		if (!udp_server)
			break;

		if (!udp_server->start_triggers.num && !udp_server->stop_triggers.num)
			break;

		if (recv_len < BUFLEN) {
			buf[recv_len] = 0;
			for (size_t i = 0; i < udp_server->start_triggers.num; i++) {
				struct move_filter *move_filter = udp_server->start_triggers.array[i];
				obs_data_t *settings = obs_source_get_settings(move_filter->source);
				const char *packet = obs_data_get_string(settings, "start_trigger_udp_packet");
				if (!strlen(packet) || strcmp(packet, buf) == 0) {
					move_filter_start(move_filter);
				}
				obs_data_release(settings);
			}
			for (size_t i = 0; i < udp_server->stop_triggers.num; i++) {
				struct move_filter *move_filter = udp_server->stop_triggers.array[i];
				obs_data_t *settings = obs_source_get_settings(move_filter->source);
				const char *packet = obs_data_get_string(settings, "stop_trigger_udp_packet");
				if (!strlen(packet) || strcmp(packet, buf) == 0) {
					move_filter_stop(move_filter);
				}
				obs_data_release(settings);
			}
		}
		pthread_mutex_unlock(&udp_servers_mutex);
	}
	da_free(udp_server->start_triggers);
	da_free(udp_server->stop_triggers);
	da_erase_item(udp_servers, udp_server);
	if (!udp_servers.num)
		da_free(udp_servers);
	pthread_mutex_unlock(&udp_servers_mutex);
	closesocket(sockfd);
	return NULL;
}

void move_filter_update(struct move_filter *move_filter, obs_data_t *settings)
{
	const char *filter_name = obs_source_get_name(move_filter->source);
	if (!move_filter->filter_name || strcmp(move_filter->filter_name, filter_name) != 0) {
		bfree(move_filter->filter_name);
		move_filter->filter_name = bstrdup(filter_name);
		if (move_filter->move_start_hotkey != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(move_filter->move_start_hotkey);
			move_filter->move_start_hotkey = OBS_INVALID_HOTKEY_ID;
		}
		if (move_filter->move_hold_hotkey != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(move_filter->move_hold_hotkey);
			move_filter->move_hold_hotkey = OBS_INVALID_HOTKEY_ID;
		}
	}
	obs_source_t *parent = obs_filter_get_parent(move_filter->source);
	if (parent && move_filter->filter_name) {
		if (move_filter->move_start_hotkey == OBS_INVALID_HOTKEY_ID) {
			move_filter->move_start_hotkey = obs_hotkey_register_source(
				parent, move_filter->filter_name, move_filter->filter_name, move_filter_start_hotkey, move_filter);
		}
		if (move_filter->move_hold_hotkey == OBS_INVALID_HOTKEY_ID) {
			struct dstr hotkey_name = {0};
			dstr_init_copy(&hotkey_name, move_filter->filter_name);
			dstr_cat(&hotkey_name, " ");
			dstr_cat(&hotkey_name, obs_module_text("Hold"));
			move_filter->move_hold_hotkey = obs_hotkey_register_source(parent, hotkey_name.array, hotkey_name.array,
										   move_filter_hold_hotkey, move_filter);
			dstr_free(&hotkey_name);
		}
	}
	move_filter->enabled_match_moving = obs_data_get_bool(settings, S_ENABLED_MATCH_MOVING);
	if (move_filter->enabled_match_moving && !move_filter->moving && obs_source_enabled(move_filter->source))
		move_filter_start(move_filter);

	move_filter->custom_duration = obs_data_get_bool(settings, S_CUSTOM_DURATION);
	if (move_filter->custom_duration)
		move_filter->duration = obs_data_get_int(settings, S_DURATION);
	move_filter->start_delay = obs_data_get_int(settings, S_START_DELAY);
	move_filter->end_delay = obs_data_get_int(settings, S_END_DELAY);

	move_filter->easing = obs_data_get_int(settings, S_EASING_MATCH);
	move_filter->easing_function = obs_data_get_int(settings, S_EASING_FUNCTION_MATCH);

	pthread_mutex_lock(&udp_servers_mutex);
	move_filter->start_trigger = (uint32_t)obs_data_get_int(settings, S_START_TRIGGER);
	if (move_filter->start_trigger == START_TRIGGER_UDP) {
		int port = (int)obs_data_get_int(settings, S_START_TRIGGER_UDP_PORT);
		if (!port)
			port = 3000;
		struct udp_server *udp_server = NULL;
		for (size_t i = 0; i < udp_servers.num; i++) {
			if (udp_servers.array[i].port == port) {
				udp_server = &udp_servers.array[i];
			} else {
				for (size_t j = 0; j < udp_servers.array[i].start_triggers.num; j++) {
					if (udp_servers.array[i].start_triggers.array[j] == move_filter) {
						da_erase(udp_servers.array[i].start_triggers, j);
						if (!udp_servers.array[i].start_triggers.num &&
						    !udp_servers.array[i].stop_triggers.num) {
							stop_udp_thread(&udp_servers.array[i]);
						}
						break;
					}
				}
			}
		}
		if (!udp_server) {
			udp_server = da_push_back_new(udp_servers);
			udp_server->port = port;
			da_init(udp_server->start_triggers);
			da_init(udp_server->stop_triggers);
			da_push_back(udp_server->start_triggers, &move_filter);
			pthread_create(&udp_server->thread, NULL, udp_server_thread, udp_server);
		}
		bool found = false;
		for (size_t i = 0; i < udp_server->start_triggers.num; i++) {
			if (udp_server->start_triggers.array[i] == move_filter) {
				found = true;
				break;
			}
		}
		if (!found)
			da_push_back(udp_server->start_triggers, &move_filter);

	} else {
		// stop existing udp server
		for (size_t i = 0; i < udp_servers.num; i++) {
			for (size_t j = 0; j < udp_servers.array[j].start_triggers.num; j++) {
				if (udp_servers.array[i].start_triggers.array[j] != move_filter)
					continue;
				da_erase(udp_servers.array[i].start_triggers, j);
				if (!udp_servers.array[i].start_triggers.num && !udp_servers.array[j].stop_triggers.num) {
					stop_udp_thread(&udp_servers.array[i]);
				}
				break;
			}
		}
	}
	move_filter->stop_trigger = (uint32_t)obs_data_get_int(settings, S_STOP_TRIGGER);
	if (move_filter->stop_trigger == START_TRIGGER_UDP) {
		int port = (int)obs_data_get_int(settings, S_STOP_TRIGGER_UDP_PORT);
		if (!port)
			port = 3000;
		struct udp_server *udp_server = NULL;
		for (size_t i = 0; i < udp_servers.num; i++) {
			if (udp_servers.array[i].port == port) {
				udp_server = &udp_servers.array[i];
			} else {
				for (size_t j = 0; j < udp_servers.array[i].stop_triggers.num; j++) {
					if (udp_servers.array[i].stop_triggers.array[j] == move_filter) {
						da_erase(udp_servers.array[i].stop_triggers, j);
						if (!udp_servers.array[i].start_triggers.num &&
						    !udp_servers.array[i].stop_triggers.num) {
							stop_udp_thread(&udp_servers.array[i]);
						}
						break;
					}
				}
			}
		}
		if (!udp_server) {
			udp_server = da_push_back_new(udp_servers);
			udp_server->port = port;
			da_init(udp_server->start_triggers);
			da_init(udp_server->stop_triggers);
			da_push_back(udp_server->stop_triggers, &move_filter);
			pthread_create(&udp_server->thread, NULL, udp_server_thread, udp_server);
		}
		bool found = false;
		for (size_t i = 0; i < udp_server->stop_triggers.num; i++) {
			if (udp_server->stop_triggers.array[i] == move_filter) {
				found = true;
				break;
			}
		}
		if (!found)
			da_push_back(udp_server->stop_triggers, &move_filter);
	} else {
		// stop existing udp server
		for (size_t i = 0; i < udp_servers.num; i++) {
			for (size_t j = 0; j < udp_servers.array[j].stop_triggers.num; j++) {
				if (udp_servers.array[i].stop_triggers.array[j] != move_filter)
					continue;
				da_erase(udp_servers.array[i].stop_triggers, j);
				if (!udp_servers.array[i].start_triggers.num && !udp_servers.array[j].stop_triggers.num) {
					stop_udp_thread(&udp_servers.array[i]);
				}
				break;
			}
		}
	}
	pthread_mutex_unlock(&udp_servers_mutex);

	const char *simultaneous_move_name = obs_data_get_string(settings, S_SIMULTANEOUS_MOVE);
	if (!move_filter->simultaneous_move_name || strcmp(move_filter->simultaneous_move_name, simultaneous_move_name) != 0) {
		bfree(move_filter->simultaneous_move_name);
		move_filter->simultaneous_move_name = bstrdup(simultaneous_move_name);
	}

	const char *next_move_name = obs_data_get_string(settings, S_NEXT_MOVE);
	if (!move_filter->next_move_name || strcmp(move_filter->next_move_name, next_move_name) != 0) {
		bfree(move_filter->next_move_name);
		move_filter->next_move_name = bstrdup(next_move_name);
		move_filter->reverse = false;
	}
	move_filter->next_move_on = obs_data_get_int(settings, S_NEXT_MOVE_ON);
}

void move_filter_start(struct move_filter *move_filter)
{
	move_filter->move_start(move_filter);
}

bool move_filter_start_internal(struct move_filter *move_filter)
{
	if (!move_filter->custom_duration)
		move_filter->duration = obs_frontend_get_transition_duration();
	if (move_filter->moving && !move_filter->holding && obs_source_enabled(move_filter->source)) {
		if (move_filter->next_move_on == NEXT_MOVE_ON_HOTKEY && move_filter->next_move_name &&
		    strcmp(move_filter->next_move_name, NEXT_MOVE_REVERSE) == 0) {
			move_filter->reverse = !move_filter->reverse;
			move_filter->running_duration =
				(float)(move_filter->duration + move_filter->start_delay + move_filter->end_delay) / 1000.0f -
				move_filter->running_duration;
		}
		return false;
	}
	move_filter->running_duration = 0.0f;
	move_filter->moving = true;

	if (move_filter->enabled_match_moving && !obs_source_enabled(move_filter->source)) {
		move_filter->enabled = true;
		obs_source_set_enabled(move_filter->source, true);
	}

	if (move_filter->simultaneous_move_name && strlen(move_filter->simultaneous_move_name) &&
	    (!move_filter->filter_name || strcmp(move_filter->filter_name, move_filter->simultaneous_move_name) != 0)) {
		obs_source_t *parent = obs_filter_get_parent(move_filter->source);
		if (parent) {
			obs_source_t *filter = obs_source_get_filter_by_name(parent, move_filter->simultaneous_move_name);
			if (!filter && move_filter->get_alternative_filter) {
				filter = move_filter->get_alternative_filter(move_filter, move_filter->simultaneous_move_name);
			}

			if (filter) {
				if (!obs_source_removed(filter) && is_move_filter(obs_source_get_unversioned_id(filter))) {
					move_filter_start(obs_obj_get_data(filter));
				}
				obs_source_release(filter);
			}
		}
	}
	return true;
}

void move_filter_stop(struct move_filter *move_filter)
{
	if (move_filter->holding)
		return;
	move_filter->moving = false;
	if (move_filter->enabled_match_moving && obs_source_enabled(move_filter->source)) {
		obs_source_set_enabled(move_filter->source, false);
	}
}

extern void move_filter_ended(struct move_filter *move_filter)
{
	if (move_filter->enabled_match_moving &&
	    (move_filter->reverse || move_filter->next_move_on == NEXT_MOVE_ON_HOTKEY || !move_filter->next_move_name ||
	     strcmp(move_filter->next_move_name, NEXT_MOVE_REVERSE) != 0) &&
	    obs_source_enabled(move_filter->source)) {
		obs_source_set_enabled(move_filter->source, false);
	}

	if (move_filter->next_move_on == NEXT_MOVE_ON_END && move_filter->next_move_name && strlen(move_filter->next_move_name) &&
	    (!move_filter->filter_name || strcmp(move_filter->filter_name, move_filter->next_move_name) != 0)) {
		if (strcmp(move_filter->next_move_name, NEXT_MOVE_REVERSE) == 0) {
			move_filter->reverse = !move_filter->reverse;
			if (move_filter->reverse)
				move_filter_start(move_filter);
		} else {
			obs_source_t *parent = obs_filter_get_parent(move_filter->source);
			if (parent) {
				obs_source_t *filter = obs_source_get_filter_by_name(parent, move_filter->next_move_name);

				if (!filter && move_filter->get_alternative_filter) {
					filter = move_filter->get_alternative_filter(move_filter, move_filter->next_move_name);
				}

				if (filter) {
					if (!obs_source_removed(filter) && is_move_filter(obs_source_get_unversioned_id(filter))) {
						move_filter_start(obs_obj_get_data(filter));
					}

					obs_source_release(filter);
				}
			}
		}
	} else if (move_filter->next_move_on == NEXT_MOVE_ON_HOTKEY && move_filter->next_move_name &&
		   strcmp(move_filter->next_move_name, NEXT_MOVE_REVERSE) == 0) {
		move_filter->reverse = !move_filter->reverse;
	}
}

float get_eased(float f, long long easing, long long easing_function);

bool move_filter_tick(struct move_filter *move_filter, float seconds, float *tp)
{
	if (move_filter->filter_name &&
	    (move_filter->move_start_hotkey == OBS_INVALID_HOTKEY_ID || move_filter->move_hold_hotkey == OBS_INVALID_HOTKEY_ID)) {
		obs_source_t *parent = obs_filter_get_parent(move_filter->source);
		if (parent && move_filter->move_start_hotkey == OBS_INVALID_HOTKEY_ID) {
			move_filter->move_start_hotkey = obs_hotkey_register_source(
				parent, move_filter->filter_name, move_filter->filter_name, move_filter_start_hotkey, move_filter);
		}
		if (parent && move_filter->move_hold_hotkey == OBS_INVALID_HOTKEY_ID) {
			struct dstr hotkey_name = {0};
			dstr_init_copy(&hotkey_name, move_filter->filter_name);
			dstr_cat(&hotkey_name, " ");
			dstr_cat(&hotkey_name, obs_module_text("Hold"));
			move_filter->move_hold_hotkey = obs_hotkey_register_source(parent, hotkey_name.array, hotkey_name.array,
										   move_filter_hold_hotkey, move_filter);
			dstr_free(&hotkey_name);
		}
	}

	const bool enabled = obs_source_enabled(move_filter->source);
	if (move_filter->enabled != enabled) {
		if (enabled && (move_filter->start_trigger == START_TRIGGER_ENABLE ||
				(move_filter->enabled_match_moving && !move_filter->moving)))
			move_filter_start(move_filter);
		if (enabled && move_filter->stop_trigger == START_TRIGGER_ENABLE)
			move_filter_stop(move_filter);

		move_filter->enabled = enabled;
	}
	if (move_filter->enabled_match_moving && enabled != move_filter->moving) {
		if (enabled) {
			move_filter_start(move_filter);
		} else {
			move_filter_stop(move_filter);
		}
	}
	if (!move_filter->moving || !enabled)
		return false;

	move_filter->running_duration += seconds;
	if (move_filter->running_duration * 1000.0f < (move_filter->reverse ? move_filter->end_delay : move_filter->start_delay)) {
		return false;
	}
	if (move_filter->holding) {
		move_filter_start(move_filter);
		move_filter->running_duration = (float)(move_filter->start_delay + move_filter->duration) / 1000.0f;

		*tp = 1.0f;
		return true;
	}

	if (move_filter->running_duration * 1000.0f >=
	    (float)(move_filter->start_delay + move_filter->duration + move_filter->end_delay)) {
		move_filter->moving = false;
	}
	if (!move_filter->duration) {
		*tp = 1.0f;
		return true;
	}
	float t = (move_filter->running_duration * 1000.0f -
		   (float)(move_filter->reverse ? move_filter->end_delay : move_filter->start_delay)) /
		  (float)move_filter->duration;
	if (t >= 1.0f) {
		t = 1.0f;
	}
	if (move_filter->reverse) {
		t = 1.0f - t;
	}
	t = get_eased(t, move_filter->easing, move_filter->easing_function);
	*tp = t;
	return true;
}

bool move_filter_start_button(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct move_filter *move_filter = data;
	move_filter_start(move_filter);
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	return false;
}

void prop_list_add_move_filter(obs_source_t *parent, obs_source_t *child, void *data)
{
	UNUSED_PARAMETER(parent);
	if (!is_move_filter(obs_source_get_unversioned_id(child)))
		return;
	obs_property_t *p = (obs_property_t *)data;
	const char *name = obs_source_get_name(child);
	obs_property_list_add_string(p, name, name);
}

bool move_filter_start_trigger_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	obs_property_t *port = obs_properties_get(props, S_START_TRIGGER_UDP_PORT);
	obs_property_t *packet = obs_properties_get(props, S_START_TRIGGER_UDP_PACKET);
	bool udp = obs_data_get_int(settings, S_START_TRIGGER) == START_TRIGGER_UDP;
	if (obs_property_visible(port) == udp && obs_property_visible(packet) == udp)
		return false;
	obs_property_set_visible(port, udp);
	obs_property_set_visible(packet, udp);
	return true;
}

bool move_filter_stop_trigger_changed(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	obs_property_t *port = obs_properties_get(props, S_STOP_TRIGGER_UDP_PORT);
	obs_property_t *packet = obs_properties_get(props, S_STOP_TRIGGER_UDP_PACKET);
	bool udp = obs_data_get_int(settings, S_STOP_TRIGGER) == START_TRIGGER_UDP;
	if (obs_property_visible(port) == udp && obs_property_visible(packet) == udp)
		return false;
	obs_property_set_visible(port, udp);
	obs_property_set_visible(packet, udp);
	return true;
}

void move_filter_properties(struct move_filter *move_filter, obs_properties_t *ppts)
{
	obs_property_t *p = obs_properties_add_int(ppts, S_START_DELAY, obs_module_text("StartDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	obs_properties_t *duration = obs_properties_create();

	p = obs_properties_add_int(duration, S_DURATION, "", 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_group(ppts, S_CUSTOM_DURATION, obs_module_text("CustomDuration"), OBS_GROUP_CHECKABLE, duration);

	p = obs_properties_add_int(ppts, S_END_DELAY, obs_module_text("EndDelay"), 0, 10000000, 100);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_list(ppts, S_EASING_MATCH, obs_module_text("Easing"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	prop_list_add_easings(p);

	p = obs_properties_add_list(ppts, S_EASING_FUNCTION_MATCH, obs_module_text("EasingFunction"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	prop_list_add_easing_functions(p);

	p = obs_properties_add_bool(ppts, S_ENABLED_MATCH_MOVING, obs_module_text("EnabledMatchMoving"));

	p = obs_properties_add_list(ppts, S_START_TRIGGER, obs_module_text("StartTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StartTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Load"), START_TRIGGER_LOAD);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);

	obs_property_set_modified_callback2(p, move_filter_start_trigger_changed, move_filter);

	obs_properties_add_int(ppts, S_START_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_START_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	p = obs_properties_add_list(ppts, S_STOP_TRIGGER, obs_module_text("StopTrigger"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(p, obs_module_text("StopTrigger.None"), START_TRIGGER_NONE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Activate"), START_TRIGGER_ACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Deactivate"), START_TRIGGER_DEACTIVATE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Show"), START_TRIGGER_SHOW);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Hide"), START_TRIGGER_HIDE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Enable"), START_TRIGGER_ENABLE);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveMatch"), START_TRIGGER_MOVE_MATCH);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveIn"), START_TRIGGER_MOVE_IN);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.MoveOut"), START_TRIGGER_MOVE_OUT);
	obs_property_list_add_int(p, obs_module_text("StartTrigger.Udp"), START_TRIGGER_UDP);

	obs_property_set_modified_callback2(p, move_filter_stop_trigger_changed, move_filter);

	obs_properties_add_int(ppts, S_STOP_TRIGGER_UDP_PORT, obs_module_text("UdpPort"), 1, 65535, 1);
	obs_properties_add_text(ppts, S_STOP_TRIGGER_UDP_PACKET, obs_module_text("UdpPacket"), OBS_TEXT_DEFAULT);

	obs_source_t *parent = obs_filter_get_parent(move_filter->source);

	p = obs_properties_add_list(ppts, S_SIMULTANEOUS_MOVE, obs_module_text("SimultaneousMove"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("SimultaneousMove.None"), "");
	if (parent)
		obs_source_enum_filters(parent, prop_list_add_move_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE, obs_module_text("NextMove"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("NextMove.None"), "");
	obs_property_list_add_string(p, obs_module_text("NextMove.Reverse"), NEXT_MOVE_REVERSE);
	if (parent)
		obs_source_enum_filters(parent, prop_list_add_move_filter, p);

	p = obs_properties_add_list(ppts, S_NEXT_MOVE_ON, obs_module_text("NextMoveOn"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.End"), NEXT_MOVE_ON_END);
	obs_property_list_add_int(p, obs_module_text("NextMoveOn.Hotkey"), NEXT_MOVE_ON_HOTKEY);

	obs_properties_add_button(ppts, "move_filter_start", obs_module_text("Start"), move_filter_start_button);
	obs_properties_add_text(ppts, "plugin_info", PLUGIN_INFO, OBS_TEXT_INFO);
}

void move_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, S_ENABLED_MATCH_MOVING, true);
	obs_data_set_default_int(settings, S_DURATION, 300);
	obs_data_set_default_int(settings, S_START_TRIGGER_UDP_PORT, 3000);
	obs_data_set_default_int(settings, S_STOP_TRIGGER_UDP_PORT, 3000);
}

void move_filter_activate(void *data)
{
	struct move_filter *move_filter = data;
	if (move_filter->start_trigger == START_TRIGGER_ACTIVATE)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_ACTIVATE)
		move_filter_stop(move_filter);
}

void move_filter_deactivate(void *data)
{
	struct move_filter *move_filter = data;
	if (move_filter->start_trigger == START_TRIGGER_DEACTIVATE)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_DEACTIVATE)
		move_filter_stop(move_filter);
}

void move_filter_show(void *data)
{
	struct move_filter *move_filter = data;
	if (move_filter->start_trigger == START_TRIGGER_SHOW)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_SHOW)
		move_filter_stop(move_filter);
}

void move_filter_hide(void *data)
{
	struct move_filter *move_filter = data;
	if (move_filter->start_trigger == START_TRIGGER_HIDE)
		move_filter_start(move_filter);
	if (move_filter->stop_trigger == START_TRIGGER_HIDE)
		move_filter_stop(move_filter);
}
