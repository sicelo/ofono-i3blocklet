/*
 * Embedded Linux library
 * Copyright (C) 2011-2016  Intel Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>

#include <ell/ell.h>

struct ofono_modem {
	char *path;

	bool online;
	bool powered;

	bool has_connman;
	bool has_netreg;
	bool has_simmgr;

	bool pin_locked;
	bool gprs_active;
	char *tech;
	char *reg_status;
	uint8_t strength;
};

static void signal_handler(uint32_t signo, void *user_data)
{
	switch (signo) {
	case SIGINT:
	case SIGTERM:
		l_info("Terminate");
		l_main_quit();
		break;
	}
}

static void ready_callback(void *user_data)
{
	l_info("ready");
}

static void disconnect_callback(void *user_data)
{
	l_main_quit();
}

static void update_i3(struct ofono_modem *modem) {
    const char *block = NULL;
    char concat[24] = "";

	if (modem->path == NULL)
		concat[0] = '\0';
	else if (!modem->powered)
		snprintf(concat, sizeof(concat), "%s", "󰥍");
	else if (modem->pin_locked)
		snprintf(concat, sizeof(concat), "%s", "󰒨");
	else if (modem->online && modem->tech && modem->tech[0] != '\0') {
		if (strcmp(modem->tech, "gsm") ==  0) {
			if (modem->gprs_active)
				block = l_strdup("󱘖 2G");
			else
				block = l_strdup("2G");
		} else if (strcmp(modem->tech, "edge") ==  0) {
			if (modem->gprs_active)
				block = l_strdup("󱘖 2G");
			else
				block = l_strdup("2.5");
		} else if (strcmp(modem->tech, "umts") ==  0) {
			if (modem->gprs_active)
				block = l_strdup("󱘖 3G");
			else
				block = l_strdup("3G");
		} else if (strcmp(modem->tech, "hspa") ==  0) {
			if (modem->gprs_active)
				block = l_strdup("󱘖 3.5");
			else
				block = l_strdup("3.5");
		}
		if (modem->strength > 80)
			snprintf(concat, sizeof(concat), "%s 󰣺", block); 
		else if (modem->strength > 60)
			snprintf(concat, sizeof(concat), "%s 󰣸", block); 
		else if (modem->strength > 40)
			snprintf(concat, sizeof(concat), "%s 󰣶", block); 
		else if (modem->strength > 20)
			snprintf(concat, sizeof(concat), "%s 󰣴", block); 
		else if (modem->strength > 0)
			snprintf(concat, sizeof(concat), "%s 󰣾", block); 
		else
			snprintf(concat, sizeof(concat), "%s 󰣾", block);
	} else if (!modem->online)
		snprintf(concat, sizeof(concat), "%s", "󰣼");
	printf("{\"full_text\":\"%s \", \"short_text\":\"%s \"}\n", concat, concat);
	/* i3blocks requires flushing of stdout */
	fflush(stdout);
}

static void parse_modem(struct l_dbus_message_iter *message, struct ofono_modem *modem)
{
	struct l_dbus_message_iter iter, iter2;
	const char *str, *str2;
	bool val;

	while (l_dbus_message_iter_next_entry(message, &str, &iter)){
		if (strcmp(str, "Powered") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &val);
			modem->powered = val;
		} else if (strcmp(str, "Online") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &val);
			modem->online = val;
		} else if (strcmp(str, "Interfaces") == 0) {
			/* Reset list of interfaces */
			modem->has_connman = false;
			modem->has_netreg = false;
			modem->has_simmgr = false;
			l_dbus_message_iter_get_variant(&iter, "as", &iter2);
			while (l_dbus_message_iter_next_entry(&iter2, &str2)){
				if (strcmp(str2, "org.ofono.SimManager") == 0) {
					modem->has_simmgr = true;
				} else if (strcmp(str2, "org.ofono.ConnectionManager") == 0) {
					modem->has_connman = true;
				} else if (strcmp(str2, "org.ofono.NetworkRegistration") == 0) {
					modem->has_netreg = true;
				}
			}
		}
	}
}

static void get_simprops_cb(struct l_dbus_message *message, void *user_data)
{
	const char *signature, *key, *str;
	struct l_dbus_message_iter dict, iter;
	struct ofono_modem *modem = user_data;
	bool present;

	signature = l_dbus_message_get_signature(message);
	if (strcmp(signature, "a{sv}") != 0)
		return;
	l_dbus_message_get_arguments(message, "a{sv}", &dict);

	/* We only care about PinRequired */
	while (l_dbus_message_iter_next_entry(&dict, &key, &iter)){
		if (strcmp(key, "PinRequired") == 0) {
			l_dbus_message_iter_get_variant(&iter, "s", &str);
			if (strcmp(str, "none") != 0)
				modem->pin_locked = true;
			else
				modem->pin_locked = false;
		}
	}
	update_i3(modem);
}

static void get_netreg_cb(struct l_dbus_message *message, void *user_data)
{
	const char *signature, *key, *str;
	struct l_dbus_message_iter dict, iter;
	struct ofono_modem *modem = user_data;
	uint8_t intval;
	bool present;

	signature = l_dbus_message_get_signature(message);
	if (strcmp(signature, "a{sv}") != 0)
		return;
	l_dbus_message_get_arguments(message, "a{sv}", &dict);

	while (l_dbus_message_iter_next_entry(&dict, &key, &iter)){
		if (strcmp(key, "Strength") == 0) {
			l_dbus_message_iter_get_variant(&iter, "y", &intval);
			modem->strength = intval;
		} else if (strcmp(key, "Technology") == 0) {
			l_dbus_message_iter_get_variant(&iter, "s", &str);
			modem->tech = l_strdup(str);
		} else if (strcmp(key, "Status") == 0) {
			l_dbus_message_iter_get_variant(&iter, "s", &str);
			modem->reg_status = l_strdup(str);
		}
	}
	update_i3(modem);
}

static void get_context_cb(struct l_dbus_message *message, void *user_data)
{
	const char *signature, *key;
	struct l_dbus_message_iter dict, iter;
	struct ofono_modem *modem = user_data;
	bool active;

	signature = l_dbus_message_get_signature(message);
	if (strcmp(signature, "a{sv}") != 0)
		return;
	l_dbus_message_get_arguments(message, "a{sv}", &dict);

	/* We only care about Active */
	while (l_dbus_message_iter_next_entry(&dict, &key, &iter)){
		if (strcmp(key, "Active") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &active);
			modem->gprs_active = active;
		}
	}
	update_i3(modem);
}

static void signal_cb(struct l_dbus_message *message, void *user_data)
{
	const char *path, *interface, *member, *destination, *sender, *signature;
	struct l_dbus_message_iter iter;
	struct ofono_modem *modem = user_data;
	const char *key;
	const char *str, *obj;
	uint8_t intval;
	bool boolval, update;

	update = false;

	interface = l_dbus_message_get_interface(message);
	member = l_dbus_message_get_member(message);

	if (strcmp(interface, "org.ofono.NetworkRegistration") == 0 || 
	    strcmp(interface, "org.ofono.SimManager") == 0 ||
	    strcmp(interface, "org.ofono.Modem") == 0 ||
	    strcmp(interface, "org.ofono.ConnectionContext") == 0) {

		l_dbus_message_get_arguments(message, "sv", &key, &iter);

		if (strcmp(key, "Technology") == 0) {
			l_dbus_message_iter_get_variant(&iter, "s", &str);
			modem->tech = l_strdup(str);
			update = true;
		} else if (strcmp(key, "Strength") == 0) {
			l_dbus_message_iter_get_variant(&iter, "y", &intval);
			modem->strength = intval;
			update = true;
		} else if (strcmp(key, "Active") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &boolval);
			modem->gprs_active = boolval;
			update = true;
		} else if (strcmp(key, "PinRequired") == 0) {
			l_dbus_message_iter_get_variant(&iter, "s", &str);
			if (strcmp(str, "none") != 0)
				modem->pin_locked = true;
			else
				modem->pin_locked = false;
			update = true;
		} else if (strcmp(key, "Powered") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &boolval);
			modem->powered = boolval;
			update = true;
		} else if (strcmp(key, "Online") == 0) {
			l_dbus_message_iter_get_variant(&iter, "b", &boolval);
			modem->online = boolval;
			update = true;
		}
	} else if (strcmp(interface, "org.ofono.Manager") == 0) {
		if (strcmp(member, "ModemAdded") == 0) {
			l_dbus_message_get_arguments(message, "oa{sv}", &obj, &iter);
			modem->path = l_strdup(obj);
			parse_modem(&iter, modem);
			update = true;
		} else if (strcmp(member, "ModemRemoved") == 0) {
			modem->path = NULL;
			update = true;
		}
	}

	/* print updated output */
	if (update)
		update_i3(modem);
}

static void get_modems_cb(struct l_dbus_message *message, void *user_data)
{
	struct l_dbus *dbus;
	const char *signature, *path;
	struct l_dbus_message_iter array, dict;
	struct ofono_modem *modem = user_data;
	struct l_dbus_message *sim_msg, *nr_msg, *ctx_msg;

	dbus = l_dbus_new_default(L_DBUS_SYSTEM_BUS);

	signature = l_dbus_message_get_signature(message);

	if (strcmp(signature, "a(oa{sv})") != 0)
		return;
	l_dbus_message_get_arguments(message, "a(oa{sv})", &array);
	l_dbus_message_iter_next_entry(&array, &path, &dict);
	modem->path = l_strdup(path);
	parse_modem(&dict, modem);
	if (modem->has_simmgr) {
		sim_msg = l_dbus_message_new_method_call(dbus, "org.ofono", modem->path,
				"org.ofono.SimManager", "GetProperties");
		l_dbus_message_set_arguments(sim_msg, "");
		l_dbus_send_with_reply(dbus, sim_msg, get_simprops_cb, modem, NULL);
	}
	if (modem->has_netreg) {
		nr_msg = l_dbus_message_new_method_call(dbus, "org.ofono", modem->path,
				"org.ofono.NetworkRegistration", "GetProperties");
		l_dbus_message_set_arguments(nr_msg, "");
		l_dbus_send_with_reply(dbus, nr_msg, get_netreg_cb, modem, NULL);
	}
	if (modem->has_connman) {
		char ctx_obj[128];
		snprintf(ctx_obj, 128, "%s%s", modem->path, "/context1");
		ctx_msg = l_dbus_message_new_method_call(dbus, "org.ofono", ctx_obj,
				"org.ofono.ConnectionContext", "GetProperties");
		l_dbus_message_set_arguments(ctx_msg, "");
		l_dbus_send_with_reply(dbus, ctx_msg, get_context_cb, modem, NULL);
		/* l_free(ctx_obj); */
	}
	update_i3(modem);
	
	l_dbus_add_signal_watch(dbus, "org.ofono", NULL, NULL, NULL,
					L_DBUS_MATCH_NONE, signal_cb, modem);
}

static void service_appeared(struct l_dbus *dbus, void *user_data)
{
	struct ofono_modem *modem;
	struct l_dbus_message *msg;

	l_info("ofono appeared");
	modem = l_new(struct ofono_modem, 1);
	msg = l_dbus_message_new_method_call(dbus, "org.ofono", "/",
						"org.ofono.Manager", "GetModems");
	l_dbus_message_set_arguments(msg, "");
	l_dbus_send_with_reply(dbus, msg, get_modems_cb, modem, NULL);
}

static void service_disappeared(struct l_dbus *dbus, void *user_data)
{
	l_info("Service disappeared");
}

int main(int argc, char *argv[])
{
	struct l_dbus *dbus;
	uint32_t service_watch_id;
	unsigned int signal_watch_id;
	struct ofono_modem *modem;

	if (!l_main_init())
		return -1;

	dbus = l_dbus_new_default(L_DBUS_SYSTEM_BUS);
	l_dbus_set_ready_handler(dbus, ready_callback, dbus, NULL);
	l_dbus_set_disconnect_handler(dbus, disconnect_callback, NULL, NULL);

	service_watch_id = l_dbus_add_service_watch(dbus, "org.ofono", service_appeared,
						service_disappeared, NULL, NULL);

	l_main_run_with_signal(signal_handler, NULL);

	l_dbus_destroy(dbus);

	l_main_exit();

	return 0;
}
