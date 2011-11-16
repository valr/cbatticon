/*
 * Copyright (c) 2011 Colin Jones
 * Based on code by Matteo Marchesotti
 * Copyright (c) 2007 Matteo Marchesotti <matteo.marchesotti@fsfe.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gtk/gtk.h>
#include <libudev.h>
#include <libnotify/notify.h>
#include <math.h>
#include <errno.h>

/* todo: */
/* problem of current_rate: it can take a bit of time to have the refreshed */
/* info so the value is not always accurate when the notification is sent.  */

#define STR_LTH 256

static void get_battery (gchar *udev_device_suffix);
static gboolean get_battery_present (struct udev_device* battery, gint *present);
static gboolean get_battery_status (struct udev_device* battery, gint *status);
static gboolean get_battery_remaining_capacity (struct udev_device* battery, gdouble *capacity);
static gboolean get_battery_full_capacity (struct udev_device* battery, gdouble *capacity);
static gboolean get_battery_current_rate (struct udev_device* battery, gdouble *rate);
static gboolean get_battery_charge_percentage (struct udev_device* battery, gint *percentage);
static gboolean get_battery_charge_time (struct udev_device* battery, gint *time);
static gboolean get_battery_remaining_time (struct udev_device* battery, gint *time);

static GtkStatusIcon* create_tray_icon (void);
static gboolean update_tray_icon (GtkStatusIcon *tray_icon);
static void update_tray_icon_state (GtkStatusIcon *tray_icon);
static void notify_message (gchar *message);
static void notify_battery_information (gint state, gint percentage, gchar *time);
static void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gint state, gint percentage, gchar *time);
static gchar* get_time_string (gint minutes);
static gchar* get_icon_name (gint state, gint percentage, gchar *time);

/*
 * udev functions
 */

enum {
	MISSING = 0,
	CHARGING,
	DISCHARGING,
	NOT_CHARGING,
	CHARGED,
	UNKNOWN,
	LOW_POWER
};

struct udev *battery_context = NULL;
static gchar *battery_path = NULL;

int is_rate_possible=TRUE;//not all hardware supports get_current_rate

static void get_battery (gchar *udev_device_suffix)
{
	struct udev *udev_context;
	struct udev_enumerate *udev_enumerate_context;
	struct udev_list_entry *udev_devices_list, *udev_device_list_entry;
	struct udev_device *udev_device;
	gchar *udev_device_path;

	udev_context = udev_new ();
	if (!udev_context) {
		g_fprintf(stderr, "Can't create udev context");
		return;
	}

	udev_enumerate_context = udev_enumerate_new (udev_context);
	if (!udev_enumerate_context) {
		g_fprintf(stderr, "Can't create udev enumeration context");
		return;
	}

	if (udev_enumerate_add_match_subsystem (udev_enumerate_context, "power_supply")) {
		g_fprintf(stderr, "Can't add udev matching subsystem: power_supply");
		return;
	}

	if (udev_enumerate_scan_devices (udev_enumerate_context)) {
		g_fprintf(stderr, "Can't scan udev devices");
		return;
	}

	udev_devices_list = udev_enumerate_get_list_entry (udev_enumerate_context);
	udev_list_entry_foreach (udev_device_list_entry, udev_devices_list) {

		udev_device_path = g_strdup (udev_list_entry_get_name (udev_device_list_entry));
		udev_device = udev_device_new_from_syspath (udev_context, udev_device_path);

		if (udev_device &&
		    !g_strcmp0 (udev_device_get_sysattr_value (udev_device, "type"), "Battery") &&
			(udev_device_suffix == NULL || g_str_has_suffix (udev_device_path, udev_device_suffix))) {

			udev_device_unref (udev_device);
			udev_enumerate_unref (udev_enumerate_context);

			battery_context = udev_context;
			battery_path = udev_device_path;
			return;
		}

		udev_device_unref (udev_device);
		g_free (udev_device_path);
	}

	udev_enumerate_unref (udev_enumerate_context);
	udev_unref (udev_context);

	g_fprintf(stderr, "No battery device found!");
}

static gboolean get_battery_present (struct udev_device* battery, gint *present)
{
	const gchar *value = udev_device_get_sysattr_value (battery, "present");
	if (!value)
		return FALSE;

	*present = (!g_strcmp0 (value, "1") ? 1 : 0);
	return TRUE;
}

static gboolean get_battery_status (struct udev_device* battery, gint *status)
{
	const gchar *value = udev_device_get_sysattr_value (battery, "status");
	if (!value)
		return FALSE;

	if (!g_strcmp0 (value, "Charging"))
		*status = CHARGING;
	else if (!g_strcmp0 (value, "Discharging"))
		*status = DISCHARGING;
	else if (!g_strcmp0 (value, "Not charging"))
		*status = NOT_CHARGING;
	else if (!g_strcmp0 (value, "Full"))
		*status = CHARGED;
	else
		*status = UNKNOWN;

	return TRUE;
}

static gboolean get_battery_remaining_capacity (struct udev_device* battery, gdouble *capacity)
{
	const gchar *value = udev_device_get_sysattr_value (battery, "energy_now");
	if (!value) value = udev_device_get_sysattr_value (battery, "charge_now");
	if (!value)
		return FALSE;

	*capacity = g_ascii_strtod (value, NULL);
	if (errno)
		return FALSE;

	return TRUE;
}

static gboolean get_battery_full_capacity (struct udev_device* battery, gdouble *capacity)
{
	const gchar *value = udev_device_get_sysattr_value (battery, "energy_full");
	if (!value) value = udev_device_get_sysattr_value (battery, "charge_full");
	if (!value)
		return FALSE;

	*capacity = g_ascii_strtod (value, NULL);
	if (errno)
		return FALSE;

	return TRUE;
}

static gboolean get_battery_current_rate (struct udev_device* battery, gdouble *rate)
{
	if(!is_rate_possible){
		return FALSE;
	}
	const gchar *value = udev_device_get_sysattr_value (battery, "power_now");
	if (!value) value = udev_device_get_sysattr_value (battery, "current_now");
	if (!value){
		is_rate_possible=FALSE;
		return FALSE;
	}
	*rate = g_ascii_strtod (value, NULL);
	if (errno)
		return FALSE;

	return TRUE;
}

/*
 * computation functions
 */

static gboolean get_battery_charge_percentage (struct udev_device* battery, gint *percentage)
{
	gdouble remaining_capacity, full_capacity;

	if (!get_battery_remaining_capacity (battery, &remaining_capacity) ||
		!get_battery_full_capacity (battery, &full_capacity))
		return FALSE;

	if (full_capacity == 0.0)
		return FALSE;

	*percentage = (gint)fmin(floor(remaining_capacity / full_capacity * 100.0), 100.0);
	return TRUE;
}

static gboolean get_battery_charge_time (struct udev_device* battery, gint *time)
{
	if(!is_rate_possible){
		*time=-1;
		return TRUE;
	}
	gdouble full_capacity, remaining_capacity, current_rate;

	if (!get_battery_full_capacity (battery, &full_capacity) ||
		!get_battery_remaining_capacity (battery, &remaining_capacity) ||
		!get_battery_current_rate (battery, &current_rate))
		return FALSE;

	if (full_capacity == 0.0 || current_rate == 0.0)
		return FALSE;

	*time = (gint)(((full_capacity - remaining_capacity) / current_rate) * 60.0);
	return TRUE;
}

static gboolean get_battery_remaining_time (struct udev_device* battery, gint *time)
{
	if(!is_rate_possible){
		*time=-1;
		return TRUE;
	}
	gdouble remaining_capacity, current_rate;

	if (!get_battery_remaining_capacity (battery, &remaining_capacity) ||
		!get_battery_current_rate (battery, &current_rate))
		return FALSE;

	if (current_rate == 0.0)
		return FALSE;

	*time = (gint)((remaining_capacity / current_rate) * 60.0);
	return TRUE;
}

/*
 * tray icon functions
 */

static GtkStatusIcon* create_tray_icon (void)
{
	GtkStatusIcon *tray_icon = gtk_status_icon_new ();

	gtk_status_icon_set_tooltip (tray_icon, "Battery Monitor");
	gtk_status_icon_set_visible (tray_icon, TRUE);

	update_tray_icon (tray_icon);
	g_timeout_add_seconds (2, (GSourceFunc)update_tray_icon, (gpointer)tray_icon);

	return tray_icon;
}

static gboolean update_tray_icon (GtkStatusIcon *tray_icon)
{
	static gboolean lock = FALSE;

	if (!lock) {
		lock = TRUE;
		update_tray_icon_state (tray_icon);
		lock = FALSE;
	} else
		g_fprintf(stderr, "Tray update locked!?");

	return TRUE;
}

static void update_tray_icon_state (GtkStatusIcon *tray_icon)
{
	static gint battery_state = -1;
	static gint battery_low   = -1;
	struct udev_device *battery_device;
	gint battery_present, battery_status;
	gint charge_percentage, charge_time, remaining_time;
	gchar *time;

	if (!battery_context || !battery_path)
		return;

	battery_device = udev_device_new_from_syspath (battery_context, battery_path);
	if (!battery_device)
		return;

	/* first check if battery is present... */
	if (!get_battery_present (battery_device, &battery_present))
		goto out;

	if (!battery_present) {
		if (battery_state != MISSING) {
			battery_state  = MISSING;
			notify_message ("No battery present!");
		}

		set_tooltip_and_icon (tray_icon, MISSING, 0, "");
		goto out;
	}

	/* ...and then check its status */
	if (!get_battery_status (battery_device, &battery_status))
		goto out;

	switch (battery_status) {
		case CHARGING:
			if (!get_battery_charge_percentage (battery_device, &charge_percentage) ||
				!get_battery_charge_time (battery_device, &charge_time))
				goto out;

			time = get_time_string (charge_time);

			if (battery_state != CHARGING) {
				battery_state  = CHARGING;
				notify_battery_information (CHARGING, charge_percentage, time);
			}

			set_tooltip_and_icon (tray_icon, CHARGING, charge_percentage, time);
			break;

		case DISCHARGING:
			if (!get_battery_charge_percentage (battery_device, &charge_percentage) ||
				!get_battery_remaining_time (battery_device, &remaining_time))
				goto out;

			time = get_time_string (remaining_time);

			if (battery_state != DISCHARGING) {
				battery_state  = DISCHARGING;
				battery_low    = 0;
				notify_battery_information (DISCHARGING, charge_percentage, time);
			}

			if (!battery_low && charge_percentage < 20) {
				battery_state = DISCHARGING;
				battery_low   = 1;
				notify_battery_information (LOW_POWER, charge_percentage, time);
			}

			set_tooltip_and_icon(tray_icon, DISCHARGING, charge_percentage, time);
			break;

		case CHARGED:
			charge_percentage = 100;

			if (battery_state != CHARGED) {
				battery_state  = CHARGED;
				notify_battery_information (CHARGED, charge_percentage, "");
			}

			set_tooltip_and_icon (tray_icon, CHARGED, charge_percentage, "");
			break;

		case NOT_CHARGING:
			if (battery_state != NOT_CHARGING) {
				battery_state  = NOT_CHARGING;
				notify_message ("Battery isn't charging!");
			}
			break;
	}

out:
	udev_device_unref (battery_device);
}

static void notify_message (gchar *message)
{
	NotifyNotification *note = notify_notification_new ("Battery Monitor", message, NULL);

	notify_notification_set_timeout (note, 10000);
	notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
	notify_notification_show (note, NULL);
}

static void notify_battery_information (gint state, gint percentage, gchar *time)
{
	gchar message[STR_LTH], pct[STR_LTH], ti[STR_LTH];

	g_stpcpy (message, "Battery ");

	if (state == CHARGED)
		g_strlcat (message, "charged!", STR_LTH);
	else {
		if (state == CHARGING)
			g_strlcat (message, "charging!", STR_LTH);
		else {
			g_sprintf (pct, "has %i\% remaining", percentage);
			g_strlcat (message, pct, STR_LTH);
		}

		if (time[0] != '\0') {
			g_sprintf (ti, "\n%s", time);
			g_strlcat (message, ti, STR_LTH);
		}
	}

	NotifyNotification *note = notify_notification_new ("Battery Monitor", message, get_icon_name (state, percentage, time));

	if (state == LOW_POWER) {
		notify_notification_set_timeout (note, NOTIFY_EXPIRES_NEVER);
		notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
	}
	else
		notify_notification_set_timeout (note, 10000);

	notify_notification_show (note, NULL);
}

static void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gint state, gint percentage, gchar *time)
{
	gchar tooltip[STR_LTH], pct[STR_LTH], ti[STR_LTH];

	if (state == MISSING)
		g_stpcpy (tooltip, "No battery present!");
	else {
		g_stpcpy (tooltip, "Battery ");

		if (state == CHARGING)
			g_strlcat (tooltip, "charging ", STR_LTH);
		else if (state == CHARGED)
			g_strlcat (tooltip, "charged ", STR_LTH);

		g_sprintf (pct, "(%i\%)", percentage);
		g_strlcat (tooltip, pct, STR_LTH);

		if (time[0] != '\0') {
			g_sprintf (ti, "\n%s", time);
			g_strlcat (tooltip, ti, STR_LTH);
		}
	}

	gtk_status_icon_set_tooltip (tray_icon, tooltip);
	gtk_status_icon_set_from_icon_name (tray_icon, get_icon_name (state, percentage, time));
}

static gchar* get_time_string (gint minutes)
{
	static gchar time[STR_LTH];
	gint hours;

	if(minutes==-1){
		time[0]='\0';
		return time;
	}
	hours   = minutes / 60;
	minutes = minutes - (60 * hours);

	if (hours > 0)
		g_sprintf(time, "%2d hours, %2d minutes remaining", hours, minutes);
	else
		g_sprintf(time, "%2d minutes remaining", minutes);

	return time;
}

static gchar* get_icon_name (gint state, gint percentage, gchar *time)
{
	static gchar icon_name[STR_LTH];

	g_stpcpy (icon_name, "battery");

	if (state == MISSING)
		g_strlcat (icon_name, "-missing", STR_LTH);
	else {
		if (percentage < 20)
			g_strlcat (icon_name, "-caution", STR_LTH);
		else if (percentage < 40)
			g_strlcat (icon_name, "-low", STR_LTH);
		else if (percentage < 80)
			g_strlcat (icon_name, "-good", STR_LTH);
		else
			g_strlcat (icon_name, "-full", STR_LTH);

		if (state == CHARGING)
			g_strlcat (icon_name, "-charging", STR_LTH);
		else if (state == CHARGED)
			g_strlcat (icon_name, "-charged", STR_LTH);
	}

	return icon_name;
}

int main(int argc, char **argv)
{
	GtkStatusIcon *tray_icon;
	gchar *battery_path;

	gtk_init (&argc, &argv);
	notify_init ("Battery Monitor");

	get_battery (argc > 1 ? argv[1] : NULL);
	tray_icon = create_tray_icon ();

	gtk_main();
	return 0;
}
