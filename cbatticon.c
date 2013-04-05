/*
 * Copyright (C) 2011-2013 Colin Jones
 *
 * Based on code by Matteo Marchesotti
 * Copyright (C) 2007 Matteo Marchesotti <matteo.marchesotti@fsfe.org>
 *
 * cbatticon: a GTK+ battery icon which uses libudev to be lightweight and fast.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glib/gprintf.h>
#include <libudev.h>
#include <libnotify/notify.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

static void get_options (int argc, char **argv);
static gboolean get_battery (gchar *udev_device_suffix, gboolean list_battery);

static gboolean get_sysattr_string (gchar *path, gchar *attribute, gchar **value);
static gboolean get_battery_present (gint *present);
static gboolean get_battery_status (gint *status);
static gboolean get_ac_online (gint *online);

static gboolean get_sysattr_double (gchar *attribute, gdouble *value);
static gboolean get_battery_remaining_capacity (gdouble *capacity);
static gboolean get_battery_full_capacity (gdouble *capacity);
static gboolean get_battery_current_rate (gdouble *rate);

static gboolean get_battery_charge_info (gint *percentage, gint *time);
static gboolean get_battery_remaining_charge_info (gint *percentage, gint *time);
static gboolean get_battery_estimated_time (gdouble remaining_capacity, gdouble y, gint *time);
static void reset_estimated_vars (void);

static void create_tray_icon (void);
static gboolean update_tray_icon (GtkStatusIcon *tray_icon);
static void update_tray_icon_state (GtkStatusIcon *tray_icon);
static void notify_message (gchar *message, gint timeout);
static void notify_battery_information (gint state, gint percentage, gchar *time);
static void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gint state, gint percentage, gchar *time);
static gchar* get_time_string (gint minutes);
static gchar* get_icon_name (gint state, gint percentage);

#define STR_LTH 256
#define ERROR_TIME -1

#define HAS_STANDARD_ICON_TYPE     gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "battery-full")
#define HAS_NOTIFICATION_ICON_TYPE gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "notification-battery-100")
#define HAS_SYMBOLIC_ICON_TYPE     gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "battery-full-symbolic")

enum {
    MISSING = 0,
    CHARGING,
    DISCHARGING,
    NOT_CHARGING,
    CHARGED,
    UNKNOWN,
    LOW_POWER
};

static gchar *battery_path = NULL;
static gchar *ac_path = NULL;

enum {
    UNKNOWN_ICON = 0,
    BATTERY_ICON,
    BATTERY_ICON_SYMBOLIC,
    BATTERY_ICON_NOTIFICATION
};

#define DEFAULT_UPDATE_INTERVAL 5
#define DEFAULT_LOW_LEVEL       20
#define DEFAULT_CRITICAL_LEVEL  5

static gint update_interval          = DEFAULT_UPDATE_INTERVAL;
static gint icon_type                = UNKNOWN_ICON;
static gchar *command_critical_level = NULL;
static gint low_level                = DEFAULT_LOW_LEVEL;
static gint critical_level           = DEFAULT_CRITICAL_LEVEL;

/*
 * not all hardware supports get_current_rate so the next 4 variables
 * are used to calculate estimated time
 */

gboolean is_rate_possible       = TRUE;
gdouble prev_remaining_capacity = -1;
gint prev_time                  = ERROR_TIME;
glong secs_last_time_change     = 0;

/*
 * command line options function
 */

static void get_options (int argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *option_context;

    static gchar *icon_type_string = NULL;
    static gboolean list_icon_type = FALSE;
    static gboolean list_battery = FALSE;
    static GOptionEntry option_entries[] = {
        { "update-interval"       , 'u', 0, G_OPTION_ARG_INT   , &update_interval       , "Set update interval (in seconds)"                         , NULL },
        { "icon-type"             , 'i', 0, G_OPTION_ARG_STRING, &icon_type_string      , "Set icon type ('standard', 'notification' or 'symbolic')" , NULL },
        { "command-critical-level", 'c', 0, G_OPTION_ARG_STRING, &command_critical_level, "Command to execute when critical battery level is reached", NULL },
        { "low-level"             , 'l', 0, G_OPTION_ARG_INT   , &low_level             , "Set low battery level (in percent)"                       , NULL },
        { "critical-level"        , 'r', 0, G_OPTION_ARG_INT   , &critical_level        , "Set critical battery level (in percent)"                  , NULL },
        { "list-icon-types"       , 't', 0, G_OPTION_ARG_NONE  , &list_icon_type        , "List available icon types"                                , NULL },
        { "list-batteries"        , 'b', 0, G_OPTION_ARG_NONE  , &list_battery          , "List available batteries"                                 , NULL },
        { NULL }
    };

    option_context = g_option_context_new ("[BATTERY ID]");
    g_option_context_add_main_entries (option_context, option_entries, NULL);
    g_option_context_add_group (option_context, gtk_get_option_group (TRUE));

    if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
        g_printerr ("cbatticon: %s\n", error->message);
        g_error_free (error); error = NULL;
        exit (1);
    }

    g_option_context_free (option_context);

    /* option : list available batteries */

    if (list_battery) {
        g_print ("List of available batteries:\n");
        get_battery (NULL, TRUE);
        exit (0);
    }

    /* option : list available icon types */

    if (list_icon_type) {
        g_print ("List of available icon types:\n");
        g_print ("standard\t%s\n", HAS_STANDARD_ICON_TYPE     ? "available" : "unavailable");
        g_print ("notification\t%s\n", HAS_NOTIFICATION_ICON_TYPE ? "available" : "unavailable");
        g_print ("symbolic\t%s\n", HAS_SYMBOLIC_ICON_TYPE     ? "available" : "unavailable");
        exit (0);
    }

    /* option : set icon type */

    if (icon_type_string) {
        if (!g_strcmp0 (icon_type_string, "standard") && (HAS_STANDARD_ICON_TYPE))
            icon_type = BATTERY_ICON;
        else if (!g_strcmp0 (icon_type_string, "notification") && (HAS_NOTIFICATION_ICON_TYPE))
            icon_type = BATTERY_ICON_NOTIFICATION;
        else if (!g_strcmp0 (icon_type_string, "symbolic") && (HAS_SYMBOLIC_ICON_TYPE))
            icon_type = BATTERY_ICON_SYMBOLIC;
        else g_printerr ("Unknown icon type: %s\n", icon_type_string);

        g_free (icon_type_string);
    }

    if (icon_type == UNKNOWN_ICON) {
        if (HAS_STANDARD_ICON_TYPE)
            icon_type = BATTERY_ICON;
        else if (HAS_NOTIFICATION_ICON_TYPE)
            icon_type = BATTERY_ICON_NOTIFICATION;
        else if (HAS_SYMBOLIC_ICON_TYPE)
            icon_type = BATTERY_ICON_SYMBOLIC;
        else g_printerr ("No icon type found!\n");
    }

    /* option : update interval */

    if (update_interval <= 0) {
        update_interval = DEFAULT_UPDATE_INTERVAL;
        g_printerr ("Invalid update interval (in seconds), it has been reset to default\n");
    }

    /* option : low and critical levels */

    if (low_level <= 0 || low_level >= 100) {
        low_level = DEFAULT_LOW_LEVEL;
        g_printerr ("Invalid low level (in percent), it has been reset to default\n");
    }

    if (critical_level <= 0 || critical_level >= 100) {
        critical_level = DEFAULT_CRITICAL_LEVEL;
        g_printerr ("Invalid critical level (in percent), it has been reset to default\n");
    }

    if (critical_level > low_level) {
        low_level = DEFAULT_LOW_LEVEL;
        critical_level = DEFAULT_CRITICAL_LEVEL;
        g_printerr ("Critical level higher than low level, reset to default\n");
    }
}

/*
 * udev function
 */

static gboolean get_battery (gchar *udev_device_suffix, gboolean list_battery)
{
    struct udev *udev_context;
    struct udev_enumerate *udev_enumerate_context;
    struct udev_list_entry *udev_devices_list, *udev_device_list_entry;
    struct udev_device *udev_device;
    gchar *udev_device_path;

    udev_context = udev_new ();
    if (!udev_context) {
        g_printerr ("Can't create udev context\n");
        return FALSE;
    }

    udev_enumerate_context = udev_enumerate_new (udev_context);
    if (!udev_enumerate_context) {
        g_printerr ("Can't create udev enumeration context\n");
        return FALSE;
    }

    if (udev_enumerate_add_match_subsystem (udev_enumerate_context, "power_supply")) {
        g_printerr ("Can't add udev matching subsystem: power_supply\n");
        return FALSE;
    }

    if (udev_enumerate_scan_devices (udev_enumerate_context)) {
        g_printerr ("Can't scan udev devices\n");
        return FALSE;
    }

    udev_devices_list = udev_enumerate_get_list_entry (udev_enumerate_context);
    udev_list_entry_foreach (udev_device_list_entry, udev_devices_list) {

        udev_device_path = g_strdup (udev_list_entry_get_name (udev_device_list_entry));
        udev_device = udev_device_new_from_syspath (udev_context, udev_device_path);

        if (udev_device) {
            /* process battery */
            if (!g_strcmp0 (udev_device_get_sysattr_value (udev_device, "type"), "Battery")) {
                if (list_battery) {
                    gchar *battery_id = g_path_get_basename (udev_device_path);
                    g_print ("id: %s\tpath: %s\n", battery_id, udev_device_path);
                    g_free (battery_id);
                }

                if (battery_path == NULL) {
                    if (udev_device_suffix == NULL ||
                        g_str_has_suffix (udev_device_path, udev_device_suffix)) {
                        battery_path = g_strdup (udev_device_path);
                    }
                }
            }

            /* process main supply (AC) */
            if (!g_strcmp0 (udev_device_get_sysattr_value (udev_device, "type"), "Mains")) {
                if (ac_path == NULL) {
                    ac_path = g_strdup (udev_device_path);
                }
            }
        }

        udev_device_unref (udev_device);
        g_free (udev_device_path);
    }

    udev_enumerate_unref (udev_enumerate_context);
    udev_unref (udev_context);

    if (!list_battery && !battery_path) {
        g_printerr ("No battery device found!\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * sysfs functions
 */

static gboolean get_sysattr_string (gchar *path, gchar *attribute, gchar **value)
{
    gchar *sysattr_filename;
    gboolean sysattr_status;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (attribute != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    sysattr_filename = g_build_filename (path, attribute, NULL);
    sysattr_status   = g_file_get_contents (sysattr_filename, value, NULL, NULL);
    g_free (sysattr_filename);

    return sysattr_status;
}

static gboolean get_battery_present (gint *present)
{
    gchar *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (present != NULL, FALSE);

    sysattr_status = get_sysattr_string (battery_path, "present", &sysattr_value);
    if (sysattr_status) {
        *present = (g_str_has_prefix (sysattr_value, "1") ? 1 : 0);
        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_battery_status (gint *status)
{
    gchar *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (status != NULL, FALSE);

    sysattr_status = get_sysattr_string (battery_path, "status", &sysattr_value);
    if (sysattr_status) {
        if (g_str_has_prefix (sysattr_value, "Charging"))
            *status = CHARGING;
        else if (g_str_has_prefix (sysattr_value, "Discharging"))
            *status = DISCHARGING;
        else if (g_str_has_prefix (sysattr_value, "Not charging"))
            *status = NOT_CHARGING;
        else if (g_str_has_prefix (sysattr_value, "Full"))
            *status = CHARGED;
        else
            *status = UNKNOWN;

        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_ac_online (gint *online)
{
    gchar *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (online != NULL, FALSE);

    if (!ac_path)
        return FALSE;

    sysattr_status = get_sysattr_string (ac_path, "online", &sysattr_value);
    if (sysattr_status) {
        *online = (g_str_has_prefix (sysattr_value, "1") ? 1 : 0);
        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_sysattr_double (gchar *attribute, gdouble *value)
{
    gchar *sysattr_filename, *sysattr_content;
    gboolean sysattr_status;

    g_return_val_if_fail (attribute != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    sysattr_filename = g_build_filename (battery_path, attribute, NULL);
    sysattr_status   = g_file_get_contents (sysattr_filename, &sysattr_content, NULL, NULL);
    g_free (sysattr_filename);

    if (sysattr_status) {
        *value = g_ascii_strtod (sysattr_content, NULL);
        if (errno || *value < 0.01) sysattr_status = FALSE;
        g_free (sysattr_content);
    }

    return sysattr_status;
}

static gboolean get_battery_remaining_capacity (gdouble *capacity)
{
    gboolean status;

    g_return_val_if_fail (capacity != NULL, FALSE);

    status = get_sysattr_double ("energy_now", capacity);
    if (!status) status = get_sysattr_double ("charge_now", capacity);

    return status;
}

static gboolean get_battery_full_capacity (gdouble *capacity)
{
    static gboolean status = FALSE;
    static gdouble fixed_capacity = 0.0;

    g_return_val_if_fail (capacity != NULL, FALSE);

    if (!status) {
        status = get_sysattr_double ("energy_full", &fixed_capacity);
        if (!status) status = get_sysattr_double ("charge_full", &fixed_capacity);
    }

    *capacity = fixed_capacity;

    return status;
}

static gboolean get_battery_current_rate (gdouble *rate)
{
    gboolean status;

    g_return_val_if_fail (rate != NULL, FALSE);

    status = get_sysattr_double ("power_now", rate);
    if (!status) status = get_sysattr_double ("current_now", rate);

    is_rate_possible = status;

    return status;
}

/*
 * computation functions
 */

static gboolean get_battery_charge_info (gint *percentage, gint *time)
{
    gdouble full_capacity, remaining_capacity, current_rate;

    if (!get_battery_full_capacity (&full_capacity) ||
        !get_battery_remaining_capacity (&remaining_capacity))
        return FALSE;

    *percentage = (gint)fmin (floor (remaining_capacity / full_capacity * 100.0), 100.0);

    if (!is_rate_possible) {
        if (!get_battery_estimated_time (remaining_capacity, full_capacity, time))
            *time = ERROR_TIME;

        return TRUE;
    }

    if (!get_battery_current_rate (&current_rate))
        return FALSE;

    *time = (gint)(((full_capacity - remaining_capacity) / current_rate) * 60.0);

    return TRUE;
}

static gboolean get_battery_remaining_charge_info (gint *percentage, gint *time)
{
    gdouble full_capacity, remaining_capacity, current_rate;

    if (!get_battery_full_capacity (&full_capacity) ||
        !get_battery_remaining_capacity (&remaining_capacity))
        return FALSE;

    *percentage = (gint)fmin (floor (remaining_capacity / full_capacity * 100.0), 100.0);

    if (!is_rate_possible) {
        if (!get_battery_estimated_time (remaining_capacity, 0, time))
            *time = ERROR_TIME;

        return TRUE;
    }

    if (!get_battery_current_rate (&current_rate))
        return FALSE;

    *time = (gint)((remaining_capacity / current_rate) * 60.0);

    return TRUE;
}

/* y = 0 if discharging or battery_full_capacity */
static gboolean get_battery_estimated_time (gdouble remaining_capacity, gdouble y, gint *time)
{
    gdouble m = 0;
    gdouble calc_sec = 0;

    if (prev_remaining_capacity == -1)
        prev_remaining_capacity = remaining_capacity;

    *time = prev_time;
    secs_last_time_change += update_interval;

    /*
     * y=mx+b...x=(y-b)/m solving for when y = full_charge or 0
     * y2=remaining_capacity y1=prev_remain run=secs_last_time_change b=prev_remain
     */

    if (prev_remaining_capacity != remaining_capacity) {
        m = (remaining_capacity - prev_remaining_capacity) / (gdouble)(secs_last_time_change);
        calc_sec = ((y - prev_remaining_capacity) / m) - (secs_last_time_change);
        *time = (gint)((calc_sec) / (gdouble)60);

        prev_remaining_capacity = remaining_capacity;
        prev_time = *time;
        secs_last_time_change = 0;
    }

    return TRUE;
}

static void reset_estimated_vars (void)
{
    prev_remaining_capacity = -1;
    prev_time = ERROR_TIME;
    secs_last_time_change = 0;
}

/*
 * tray icon functions
 */

static void create_tray_icon (void)
{
    GtkStatusIcon *tray_icon = gtk_status_icon_new ();

    gtk_status_icon_set_tooltip_text (tray_icon, "Battery Monitor");
    gtk_status_icon_set_visible (tray_icon, TRUE);

    update_tray_icon (tray_icon);
    g_timeout_add_seconds (update_interval, (GSourceFunc)update_tray_icon, (gpointer)tray_icon);
}

static gboolean update_tray_icon (GtkStatusIcon *tray_icon)
{
    static gboolean lock = FALSE;

    if (!lock) {
        lock = TRUE;
        update_tray_icon_state (tray_icon);
        lock = FALSE;
    } else
        g_printerr ("Tray update locked!?\n");

    return TRUE;
}

static void update_tray_icon_state (GtkStatusIcon *tray_icon)
{
    gint battery_present, battery_status, battery_online;
    gint percentage, time;
    gchar *time_string;
    GError *error = NULL;
    static gint battery_state        = -1;
    static gint battery_low          = -1;
    static gint battery_critical     = -1;
    static gint battery_not_charging = -1;

    if (!battery_path)
        return;

    /* first check if battery is present... */
    if (!get_battery_present (&battery_present))
        return;

    if (!battery_present) {
        if (battery_state != MISSING) {
            battery_state = MISSING;
            notify_message ("No battery present!", NOTIFY_EXPIRES_NEVER);
        }

        set_tooltip_and_icon (tray_icon, battery_state, 0, "");
        return;
    }

    /* ...and then check its status */
    if (!get_battery_status (&battery_status))
        return;

    /* ...if status is unknown, try guessing its status */
    if (battery_status == UNKNOWN && get_ac_online (&battery_online)) {
        if (battery_online) {
            battery_status = CHARGING;

            if (get_battery_charge_info (&percentage, &time) && percentage >= 99)
                battery_status = CHARGED;
        } else {
            battery_status = DISCHARGING;
        }
    }

    switch (battery_status) {
        case CHARGING:
            if (!get_battery_charge_info (&percentage, &time))
                return;

            time_string = get_time_string (time);

            if (battery_state != CHARGING) {
                reset_estimated_vars ();

                battery_state = CHARGING;
                notify_battery_information (battery_state, percentage, time_string);
            }

            set_tooltip_and_icon (tray_icon, battery_state, percentage, time_string);
            break;

        case NOT_CHARGING:
        case DISCHARGING:
            if (!get_battery_remaining_charge_info (&percentage, &time))
                return;

            time_string = get_time_string (time);

            if (battery_state != DISCHARGING) {
                reset_estimated_vars();

                battery_low          = 0;
                battery_critical     = 0;
                battery_not_charging = 0;

                battery_state = DISCHARGING;
                notify_battery_information (battery_state, percentage, time_string);
            }

            if (!battery_low && percentage < low_level) {
                battery_low = 1;
                notify_battery_information (LOW_POWER, percentage, time_string);
            }

            if (!battery_critical && percentage <= critical_level) {
                battery_critical = 1;
                notify_message ("Critical battery level reached!", NOTIFY_EXPIRES_NEVER);

                g_usleep (G_USEC_PER_SEC * 10);
                if (command_critical_level &&
                    g_spawn_command_line_async (command_critical_level, &error) == FALSE) {
                    notify_message ("Error: cannot spawn critical battery level command!", NOTIFY_EXPIRES_NEVER);
                    g_printerr ("Cannot spawn critical battery level command: %s\n%s\n", command_critical_level, error->message);
                    g_error_free (error); error = NULL;
                }
            }

            if (!battery_not_charging && battery_status == NOT_CHARGING) {
                battery_not_charging = 1;
                notify_message ("Battery isn't charging!", NOTIFY_EXPIRES_NEVER);
            }

            set_tooltip_and_icon (tray_icon, battery_state, percentage, time_string);
            break;

        case CHARGED:
            percentage = 100;

            if (battery_state != CHARGED) {
                battery_state = CHARGED;
                notify_battery_information (battery_state, percentage, "");
            }

            set_tooltip_and_icon (tray_icon, battery_state, percentage, "");
            break;

        case UNKNOWN:
            if (battery_state != UNKNOWN) {
                battery_state = UNKNOWN;
                /* notify_message ("Battery status is unknown!", 10000); */
            }

            set_tooltip_and_icon (tray_icon, battery_state, 0, "");
            break;
    }
}

static void notify_message (gchar *message, gint timeout)
{
    NotifyNotification *note = notify_notification_new ("Battery Monitor", message, NULL);

    notify_notification_set_timeout (note, timeout);
    notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
    notify_notification_show (note, NULL);
}

static void notify_battery_information (gint state, gint percentage, gchar *time_string)
{
    gchar message[STR_LTH], pct[STR_LTH], ti[STR_LTH];

    g_strlcpy (message, "Battery ", STR_LTH);

    if (state == CHARGED)
        g_strlcat (message, "charged!", STR_LTH);
    else {
        if (state == CHARGING)
            g_strlcat (message, "charging!", STR_LTH);
        else {
            g_sprintf (pct, "has %i\% remaining", percentage);
            g_strlcat (message, pct, STR_LTH);
        }

        if (time_string[0] != '\0') {
            g_sprintf (ti, "\n%s", time_string);
            g_strlcat (message, ti, STR_LTH);
        }
    }

    NotifyNotification *note = notify_notification_new ("Battery Monitor", message, get_icon_name (state, percentage));

    if (state == LOW_POWER) {
        notify_notification_set_timeout (note, NOTIFY_EXPIRES_NEVER);
        notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
    } else
        notify_notification_set_timeout (note, 10000);

    notify_notification_show (note, NULL);
}

static void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gint state, gint percentage, gchar *time_string)
{
    gchar tooltip[STR_LTH], pct[STR_LTH], ti[STR_LTH];

    switch (state) {
        case MISSING:
            g_strlcpy (tooltip, "No battery present!", STR_LTH);
            break;

        case UNKNOWN:
            g_strlcpy (tooltip, "Battery status is unknown!", STR_LTH);
            break;

        default:
            g_strlcpy (tooltip, "Battery ", STR_LTH);

            if (state == CHARGING)
                g_strlcat (tooltip, "charging ", STR_LTH);
            else if (state == CHARGED)
                g_strlcat (tooltip, "charged ", STR_LTH);

            g_sprintf (pct, "(%i\%)", percentage);
            g_strlcat (tooltip, pct, STR_LTH);

            if (time_string[0] != '\0') {
                g_sprintf (ti, "\n%s", time_string);
                g_strlcat (tooltip, ti, STR_LTH);
            }
            break;
    }

    gtk_status_icon_set_tooltip_text (tray_icon, tooltip);
    gtk_status_icon_set_from_icon_name (tray_icon, get_icon_name (state, percentage));
}

static gchar* get_time_string (gint minutes)
{
    static gchar time_string[STR_LTH];
    gint hours;

    if (minutes < 0) {
        time_string[0]='\0';
        return time_string;
    }

    hours   = minutes / 60;
    minutes = minutes - (60 * hours);

    if (hours > 0)
        g_sprintf(time_string, "%2d hours, %2d minutes remaining", hours, minutes);
    else
        g_sprintf(time_string, "%2d minutes remaining", minutes);

    return time_string;
}

static gchar* get_icon_name (gint state, gint percentage)
{
    static gchar icon_name[STR_LTH];

    if (icon_type == BATTERY_ICON_NOTIFICATION)
        g_strlcpy (icon_name, "notification-battery", STR_LTH);
    else
        g_strlcpy (icon_name, "battery", STR_LTH);

    if (state == MISSING || state == UNKNOWN) {
        if (icon_type == BATTERY_ICON_NOTIFICATION)
            g_strlcat (icon_name, "-empty", STR_LTH);
        else
            g_strlcat (icon_name, "-missing", STR_LTH);
    } else {
        if (icon_type == BATTERY_ICON_NOTIFICATION) {
                 if (percentage < 20)   g_strlcat (icon_name, "-020", STR_LTH);
            else if (percentage < 40)   g_strlcat (icon_name, "-040", STR_LTH);
            else if (percentage < 60)   g_strlcat (icon_name, "-060", STR_LTH);
            else if (percentage < 80)   g_strlcat (icon_name, "-080", STR_LTH);
            else                        g_strlcat (icon_name, "-100", STR_LTH);

                 if (state == CHARGING) g_strlcat (icon_name, "-plugged", STR_LTH);
            else if (state == CHARGED)  g_strlcat (icon_name, "-plugged", STR_LTH);
        } else {
                 if (percentage < 20)   g_strlcat (icon_name, "-caution", STR_LTH);
            else if (percentage < 40)   g_strlcat (icon_name, "-low", STR_LTH);
            else if (percentage < 80)   g_strlcat (icon_name, "-good", STR_LTH);
            else                        g_strlcat (icon_name, "-full", STR_LTH);

                 if (state == CHARGING) g_strlcat (icon_name, "-charging", STR_LTH);
            else if (state == CHARGED)  g_strlcat (icon_name, "-charged", STR_LTH);
        }
    }

    if (icon_type == BATTERY_ICON_SYMBOLIC)
        g_strlcat (icon_name, "-symbolic", STR_LTH);

    return icon_name;
}

int main (int argc, char **argv)
{
    get_options (argc, argv);
    notify_init ("Battery Monitor");

    if (!get_battery (argc > 1 ? argv[1] : NULL, FALSE))
        return -1;

    create_tray_icon ();
    gtk_main();

    return 0;
}
