/*
 * Copyright (C) 2011-2013 Colin Jones
 *
 * Based on code by Matteo Marchesotti
 * Copyright (C) 2007 Matteo Marchesotti <matteo.marchesotti@fsfe.org>
 *
 * cbatticon: a lightweight and fast GTK+ battery icon.
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

#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>

#include <errno.h>
#include <math.h>
#include <syslog.h>

static gboolean get_options (int argc, char **argv);
static gboolean get_battery (gchar *battery_suffix, gboolean list_battery);

static gboolean get_sysattr_string (gchar *path, gchar *attribute, gchar **value);
static gboolean get_sysattr_double (gchar *path, gchar *attribute, gdouble *value);

static gboolean get_battery_present (gboolean *present);
static gboolean get_battery_status (gint *status);
static gboolean get_ac_online (gboolean *online);

static gboolean get_battery_full_capacity (gdouble *capacity);
static gboolean get_battery_remaining_capacity (gdouble *capacity);
static gboolean get_battery_current_rate (gdouble *rate);

static gboolean get_battery_charge_info (gint *percentage, gint *time);
static gboolean get_battery_remaining_charge_info (gint *percentage, gint *time);

static void get_battery_estimated_time (gdouble remaining_capacity, gdouble y, gint *time);
static void reset_estimated_vars (void);

static void create_tray_icon (void);
static gboolean update_tray_icon (GtkStatusIcon *tray_icon);
static void update_tray_icon_status (GtkStatusIcon *tray_icon);

static void notify_message (NotifyNotification **notification, gchar *summary, gchar *body, gint timeout, NotifyUrgency urgency);

static gchar* get_tooltip_string (gchar *battery, gchar *time);
static gchar* get_battery_string (gint state, gint percentage);
static gchar* get_time_string (gint minutes);
static gchar* get_icon_name (gint state, gint percentage);

#define SYSFS_PATH "/sys/class/power_supply"

#define DEFAULT_UPDATE_INTERVAL 5
#define DEFAULT_LOW_LEVEL       20
#define DEFAULT_CRITICAL_LEVEL  5

#define STR_LTH 256

#define HAS_STANDARD_ICON_TYPE     gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "battery-full")
#define HAS_NOTIFICATION_ICON_TYPE gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "notification-battery-100")
#define HAS_SYMBOLIC_ICON_TYPE     gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "battery-full-symbolic")

enum {
    UNKNOWN_ICON = 0,
    BATTERY_ICON,
    BATTERY_ICON_SYMBOLIC,
    BATTERY_ICON_NOTIFICATION
};

enum {
    MISSING = 0,
    UNKNOWN,
    CHARGED,
    CHARGING,
    DISCHARGING,
    NOT_CHARGING,
    LOW_LEVEL,
    CRITICAL_LEVEL
};

static gchar *battery_path = NULL;
static gchar *ac_path      = NULL;

static gint update_interval          = DEFAULT_UPDATE_INTERVAL;
static gint icon_type                = UNKNOWN_ICON;
static gchar *command_critical_level = NULL;
static gint low_level                = DEFAULT_LOW_LEVEL;
static gint critical_level           = DEFAULT_CRITICAL_LEVEL;

/*
 * workaround for limited/bugged batteries/drivers that don't provide current rate
 * the next 4 variables are used to calculate estimated time
 */

static gboolean is_current_rate_possible   = TRUE;
static gdouble previous_remaining_capacity = -1;
static gdouble seconds_time_change         = 0;
static gint previous_time                  = -1;

/*
 * command line options function
 */

static gboolean get_options (int argc, char **argv)
{
    GOptionContext *option_context;
    GError *error = NULL;

    gchar *icon_type_string = NULL;
    gboolean list_icon_type = FALSE;
    gboolean list_battery   = FALSE;
    GOptionEntry option_entries[] = {
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

    if (g_option_context_parse (option_context, &argc, &argv, &error) == FALSE) {
        g_printerr ("Cannot parse command line arguments: %s\n", error->message);
        g_error_free (error); error = NULL;

        return FALSE;
    }

    g_option_context_free (option_context);

    /* option : list available icon types */

    if (list_icon_type == TRUE) {
        g_print ("List of available icon types:\n");
        g_print ("standard\t%s\n"    , HAS_STANDARD_ICON_TYPE     == TRUE ? "available" : "unavailable");
        g_print ("notification\t%s\n", HAS_NOTIFICATION_ICON_TYPE == TRUE ? "available" : "unavailable");
        g_print ("symbolic\t%s\n"    , HAS_SYMBOLIC_ICON_TYPE     == TRUE ? "available" : "unavailable");

        return FALSE;
    }

    /* option : list available batteries */

    if (list_battery == TRUE) {
        g_print ("List of available batteries:\n");
        get_battery (NULL, TRUE);

        return FALSE;
    }

    /* option : set icon type */

    if (icon_type_string != NULL) {
        if (g_strcmp0 (icon_type_string, "standard") == 0 && HAS_STANDARD_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON;
        else if (g_strcmp0 (icon_type_string, "notification") == 0 && HAS_NOTIFICATION_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON_NOTIFICATION;
        else if (g_strcmp0 (icon_type_string, "symbolic") == 0 && HAS_SYMBOLIC_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON_SYMBOLIC;
        else g_printerr ("Unknown icon type: %s\n", icon_type_string);

        g_free (icon_type_string);
    }

    if (icon_type == UNKNOWN_ICON) {
        if (HAS_STANDARD_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON;
        else if (HAS_NOTIFICATION_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON_NOTIFICATION;
        else if (HAS_SYMBOLIC_ICON_TYPE == TRUE)
            icon_type = BATTERY_ICON_SYMBOLIC;
        else g_printerr ("No icon type found!\n");
    }

    /* option : update interval */

    if (update_interval <= 0) {
        update_interval = DEFAULT_UPDATE_INTERVAL;
        g_printerr ("Invalid update interval! It has been reset to default (%d seconds)\n", DEFAULT_UPDATE_INTERVAL);
    }

    /* option : low and critical levels */

    if (low_level < 0 || low_level > 100) {
        low_level = DEFAULT_LOW_LEVEL;
        g_printerr ("Invalid low level! It has been reset to default (%d percent)\n", DEFAULT_LOW_LEVEL);
    }

    if (critical_level < 0 || critical_level > 100) {
        critical_level = DEFAULT_CRITICAL_LEVEL;
        g_printerr ("Invalid critical level! It has been reset to default (%d percent)\n", DEFAULT_CRITICAL_LEVEL);
    }

    if (critical_level > low_level) {
        low_level = DEFAULT_LOW_LEVEL;
        critical_level = DEFAULT_CRITICAL_LEVEL;
        g_printerr ("Critical level is higher than low level! They have been reset to default\n");
    }

    return TRUE;
}

/*
 * sysfs functions
 */

static gboolean get_battery (gchar *battery_suffix, gboolean list_battery)
{
    GDir *directory;
    GError *error = NULL;
    const gchar *file; 
    gchar *path;
    gchar *sysattr_value;
    gboolean sysattr_status;

    directory = g_dir_open (SYSFS_PATH, 0, &error);
    if (directory != NULL) {
        file = g_dir_read_name (directory);
        while (file != NULL) {
            path = g_build_filename (SYSFS_PATH, file, NULL);

            sysattr_status = get_sysattr_string (path, "type", &sysattr_value);
            if (sysattr_status == TRUE) {
                /* process battery */
                if (g_str_has_prefix (sysattr_value, "Battery") == TRUE) {
                    if (list_battery == TRUE) {
                        gchar *battery_id = g_path_get_basename (path);
                        g_print ("id: %s\tpath: %s\n", battery_id, path);
                        g_free (battery_id);
                    }

                    if (battery_path == NULL) {
                        if (battery_suffix == NULL ||
                            g_str_has_suffix (path, battery_suffix) == TRUE) {
                            battery_path = g_strdup (path);

                            /* workaround for limited/bugged batteries/drivers */
                            /* that don't provide current rate                 */
                            gdouble dummy;
                            is_current_rate_possible = get_battery_current_rate (&dummy);
                        }
                    }
                }

                /* process main power supply (AC) */
                if (g_str_has_prefix (sysattr_value, "Mains") == TRUE) {
                    if (ac_path == NULL) {
                        ac_path = g_strdup (path);
                    }
                }

                g_free (sysattr_value);
            }

            g_free (path);
            file = g_dir_read_name (directory);
        }

        g_dir_close (directory);
    } else {
        g_printerr ("Cannot open sysfs directory: %s (%s)\n", SYSFS_PATH, error->message);
        g_error_free (error); error = NULL;
        return FALSE;
    }

    if (list_battery == FALSE && battery_path == NULL) {
        g_printerr ("No battery device found!\n");
        return FALSE;
    }

    return TRUE;
}

static gboolean get_sysattr_string (gchar *path, gchar *attribute, gchar **value)
{
    gchar *sysattr_filename;
    gboolean sysattr_status;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (attribute != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    sysattr_filename = g_build_filename (path, attribute, NULL);
    sysattr_status = g_file_get_contents (sysattr_filename, value, NULL, NULL);
    g_free (sysattr_filename);

    return sysattr_status;
}

static gboolean get_sysattr_double (gchar *path, gchar *attribute, gdouble *value)
{
    gchar *sysattr_filename, *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (attribute != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    sysattr_filename = g_build_filename (path, attribute, NULL);
    sysattr_status = g_file_get_contents (sysattr_filename, &sysattr_value, NULL, NULL);
    g_free (sysattr_filename);

    if (sysattr_status == TRUE) {
        *value = g_ascii_strtod (sysattr_value, NULL);
        if (errno != 0 || *value < 0.01) sysattr_status = FALSE;
        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_battery_present (gboolean *present)
{
    gchar *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (present != NULL, FALSE);

    sysattr_status = get_sysattr_string (battery_path, "present", &sysattr_value);
    if (sysattr_status == TRUE) {
        *present = g_str_has_prefix (sysattr_value, "1") ? TRUE : FALSE;
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
    if (sysattr_status == TRUE) {
        if (g_str_has_prefix (sysattr_value, "Charging") == TRUE)
            *status = CHARGING;
        else if (g_str_has_prefix (sysattr_value, "Discharging") == TRUE)
            *status = DISCHARGING;
        else if (g_str_has_prefix (sysattr_value, "Not charging") == TRUE)
            *status = NOT_CHARGING;
        else if (g_str_has_prefix (sysattr_value, "Full") == TRUE)
            *status = CHARGED;
        else
            *status = UNKNOWN;

        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_ac_online (gboolean *online)
{
    gchar *sysattr_value;
    gboolean sysattr_status;

    g_return_val_if_fail (online != NULL, FALSE);

    if (ac_path == NULL)
        return FALSE;

    sysattr_status = get_sysattr_string (ac_path, "online", &sysattr_value);
    if (sysattr_status == TRUE) {
        *online = g_str_has_prefix (sysattr_value, "1") ? TRUE : FALSE;
        g_free (sysattr_value);
    }

    return sysattr_status;
}

static gboolean get_battery_full_capacity (gdouble *capacity)
{
    gboolean sysattr_status;

    g_return_val_if_fail (capacity != NULL, FALSE);

    sysattr_status = get_sysattr_double (battery_path, "energy_full", capacity);
    if (sysattr_status == FALSE)
        sysattr_status = get_sysattr_double (battery_path, "charge_full", capacity);

    return sysattr_status;
}

static gboolean get_battery_remaining_capacity (gdouble *capacity)
{
    gboolean sysattr_status;

    g_return_val_if_fail (capacity != NULL, FALSE);

    sysattr_status = get_sysattr_double (battery_path, "energy_now", capacity);
    if (sysattr_status == FALSE)
        sysattr_status = get_sysattr_double (battery_path, "charge_now", capacity);

    return sysattr_status;
}

static gboolean get_battery_current_rate (gdouble *rate)
{
    gboolean sysattr_status;

    g_return_val_if_fail (rate != NULL, FALSE);

    sysattr_status = get_sysattr_double (battery_path, "power_now", rate);
    if (sysattr_status == FALSE)
        sysattr_status = get_sysattr_double (battery_path, "current_now", rate);

    return sysattr_status;
}

/*
 * computation functions
 */

static gboolean get_battery_charge_info (gint *percentage, gint *time)
{
    gdouble full_capacity, remaining_capacity, current_rate;

    g_return_val_if_fail (percentage != NULL, FALSE);
    g_return_val_if_fail (time != NULL, FALSE);

    if (get_battery_full_capacity (&full_capacity) == FALSE ||
        get_battery_remaining_capacity (&remaining_capacity) == FALSE)
        return FALSE;

    *percentage = (gint)fmin (floor (remaining_capacity / full_capacity * 100.0), 100.0);

    if (is_current_rate_possible == TRUE) {
        if (get_battery_current_rate (&current_rate) == FALSE)
            return FALSE;

        *time = (gint)((full_capacity - remaining_capacity) / current_rate * 60.0);
    } else {
        get_battery_estimated_time (remaining_capacity, full_capacity, time);
    }

    return TRUE;
}

static gboolean get_battery_remaining_charge_info (gint *percentage, gint *time)
{
    gdouble full_capacity, remaining_capacity, current_rate;

    g_return_val_if_fail (percentage != NULL, FALSE);
    g_return_val_if_fail (time != NULL, FALSE);

    if (get_battery_full_capacity (&full_capacity) == FALSE ||
        get_battery_remaining_capacity (&remaining_capacity) == FALSE)
        return FALSE;

    *percentage = (gint)fmin (floor (remaining_capacity / full_capacity * 100.0), 100.0);

    if (is_current_rate_possible == TRUE) {
        if (get_battery_current_rate (&current_rate) == FALSE)
            return FALSE;

        *time = (gint)(remaining_capacity / current_rate * 60.0);
    } else {
        get_battery_estimated_time (remaining_capacity, 0, time);
    }

    return TRUE;
}

/* y = 0 if discharging or battery_full_capacity */
static void get_battery_estimated_time (gdouble remaining_capacity, gdouble y, gint *time)
{
    if (previous_remaining_capacity == -1)
        previous_remaining_capacity = remaining_capacity;

    seconds_time_change += (gdouble)update_interval;

    *time = previous_time;

    /*
     * y = mx + b ... x = (y - b) / m solving for when y = full_charge or 0
     * y2 = remaining_capacity y1 = prev_remain run = seconds_time_change b = prev_remain
     */

    if (remaining_capacity != previous_remaining_capacity) {
        gdouble m = (remaining_capacity - previous_remaining_capacity) / seconds_time_change;
        gdouble calculated_seconds = ((y - previous_remaining_capacity) / m) - seconds_time_change;

        *time = (gint)(calculated_seconds / 60.0);

        previous_remaining_capacity = remaining_capacity;
        seconds_time_change         = 0;
        previous_time               = *time;
    }
}

static void reset_estimated_vars (void)
{
    previous_remaining_capacity = -1;
    seconds_time_change         = 0;
    previous_time               = -1;
}

/*
 * tray icon functions
 */

static void create_tray_icon (void)
{
    GtkStatusIcon *tray_icon = gtk_status_icon_new ();

    gtk_status_icon_set_tooltip_text (tray_icon, "cbatticon");
    gtk_status_icon_set_visible (tray_icon, TRUE);

    update_tray_icon (tray_icon);
    g_timeout_add_seconds (update_interval, (GSourceFunc)update_tray_icon, (gpointer)tray_icon);
}

static gboolean update_tray_icon (GtkStatusIcon *tray_icon)
{
    static gboolean lock = FALSE;

    g_return_val_if_fail (tray_icon != NULL, FALSE);

    if (lock == FALSE) {
        lock = TRUE;
        update_tray_icon_status (tray_icon);
        lock = FALSE;
    } else {
        g_printerr ("Tray icon update locked!?\n");
    }

    return TRUE;
}

static void update_tray_icon_status (GtkStatusIcon *tray_icon)
{
    gboolean battery_present = FALSE;
    gboolean battery_online  = FALSE;

    gint battery_status            = -1;
    static gint old_battery_status = -1;

    static gboolean battery_low            = FALSE;
    static gboolean battery_critical       = FALSE;
    static gboolean spawn_command_critical = FALSE;

    gint percentage, time;
    gchar *battery_string, *time_string;
    NotifyNotification *notification = NULL;
    GError *error = NULL;

    g_return_if_fail (tray_icon != NULL);
    g_return_if_fail (battery_path != NULL);

    if (get_battery_present (&battery_present) == FALSE)
        return;

    if (battery_present == FALSE) {
        battery_status = MISSING;
    } else {
        if (get_battery_status (&battery_status) == FALSE)
            return;

        /* workaround for limited/bugged batteries/drivers */
        /* that unduly return unknown status               */
        if (battery_status == UNKNOWN && get_ac_online (&battery_online) == TRUE) {
            if (battery_online == TRUE) {
                battery_status = CHARGING;

                if (get_battery_charge_info (&percentage, &time) == TRUE && percentage >= 99) {
                    battery_status = CHARGED;
                }
            } else {
                battery_status = DISCHARGING;
            }
        }
    }

    #define HANDLE_BATTERY_STATUS(PCT,TIM,EXP,URG)                                                          \
                                                                                                            \
            percentage = PCT;                                                                               \
                                                                                                            \
            battery_string = get_battery_string (battery_status, percentage);                               \
            time_string    = get_time_string (TIM);                                                         \
                                                                                                            \
            if (old_battery_status != battery_status) {                                                     \
                old_battery_status  = battery_status;                                                       \
                notify_message (&notification, battery_string, time_string, EXP, URG);                      \
            }                                                                                               \
                                                                                                            \
            gtk_status_icon_set_tooltip_text (tray_icon, get_tooltip_string (battery_string, time_string)); \
            gtk_status_icon_set_from_icon_name (tray_icon, get_icon_name (battery_status, percentage));

    switch (battery_status) {
        case MISSING:
            HANDLE_BATTERY_STATUS (0, -1, NOTIFY_EXPIRES_NEVER, NOTIFY_URGENCY_NORMAL)
            break;

        case UNKNOWN:
            HANDLE_BATTERY_STATUS (0, -1, NOTIFY_EXPIRES_DEFAULT, NOTIFY_URGENCY_NORMAL)
            break;

        case CHARGED:
            HANDLE_BATTERY_STATUS (100, -1, NOTIFY_EXPIRES_DEFAULT, NOTIFY_URGENCY_NORMAL)
            break;

        case CHARGING:
            if (is_current_rate_possible == FALSE && old_battery_status != CHARGING)
                reset_estimated_vars ();

            if (get_battery_charge_info (&percentage, &time) == FALSE)
                return;

            HANDLE_BATTERY_STATUS (percentage, time, NOTIFY_EXPIRES_DEFAULT, NOTIFY_URGENCY_NORMAL)
            break;

        case DISCHARGING:
        case NOT_CHARGING:
            if (is_current_rate_possible == FALSE && old_battery_status != DISCHARGING)
                reset_estimated_vars ();

            if (get_battery_remaining_charge_info (&percentage, &time) == FALSE)
                return;

            battery_string = get_battery_string (battery_status, percentage);
            time_string    = get_time_string (time);

            if (old_battery_status != DISCHARGING) {
                old_battery_status  = DISCHARGING;
                notify_message (&notification, battery_string, time_string, NOTIFY_EXPIRES_DEFAULT, NOTIFY_URGENCY_NORMAL);

                battery_low            = FALSE;
                battery_critical       = FALSE;
                spawn_command_critical = FALSE;
            }

            if (battery_low == FALSE && percentage <= low_level) {
                battery_low = TRUE;

                battery_string = get_battery_string (LOW_LEVEL, percentage);
                notify_message (&notification, battery_string, time_string, NOTIFY_EXPIRES_NEVER, NOTIFY_URGENCY_NORMAL);
            }

            if (battery_critical == FALSE && percentage <= critical_level) {
                battery_critical = TRUE;

                battery_string = get_battery_string (CRITICAL_LEVEL, percentage);
                notify_message (&notification, battery_string, time_string, NOTIFY_EXPIRES_NEVER, NOTIFY_URGENCY_CRITICAL);

                spawn_command_critical = TRUE;
            }

            gtk_status_icon_set_tooltip_text (tray_icon, get_tooltip_string (battery_string, time_string));
            gtk_status_icon_set_from_icon_name (tray_icon, get_icon_name (battery_status, percentage));

            if (spawn_command_critical == TRUE) {
                spawn_command_critical = FALSE;

                if (command_critical_level != NULL) {
                    syslog (LOG_CRIT, "Spawning critical battery level command in 30 seconds: %s", command_critical_level);
                    g_usleep (G_USEC_PER_SEC * 30);

                    if (g_spawn_command_line_async (command_critical_level, &error) == FALSE) {
                        syslog (LOG_CRIT, "Cannot spawn critical battery level command: %s", error->message);
                        g_error_free (error); error = NULL;

                        notify_message (&notification, "Cannot spawn critical battery level command!", command_critical_level, NOTIFY_EXPIRES_NEVER, NOTIFY_URGENCY_CRITICAL);
                    }
                }
            }
            break;
    }
}

static void notify_message (NotifyNotification **notification, gchar *summary, gchar *body, gint timeout, NotifyUrgency urgency)
{
    g_return_if_fail (notification != NULL);
    g_return_if_fail (summary != NULL);

    if (*notification == NULL) {
        *notification = notify_notification_new (summary, body, NULL);
    } else {
        notify_notification_update (*notification, summary, body, NULL);
    }

    notify_notification_set_timeout (*notification, timeout);
    notify_notification_set_urgency (*notification, urgency);

    notify_notification_show (*notification, NULL);
}

static gchar* get_tooltip_string (gchar *battery, gchar *time)
{
    static gchar tooltip_string[STR_LTH];

    tooltip_string[0] = '\0';

    g_return_val_if_fail (battery != NULL, tooltip_string);

    g_strlcpy (tooltip_string, battery, STR_LTH);

    if (time != NULL) {
        g_strlcat (tooltip_string, "\n", STR_LTH);
        g_strlcat (tooltip_string, time, STR_LTH);
    }

    return tooltip_string;
}

static gchar* get_battery_string (gint state, gint percentage)
{
    static gchar battery_string[STR_LTH];

    switch (state) {
        case MISSING:
            g_strlcpy (battery_string, "Battery is missing!", STR_LTH);
            break;

        case UNKNOWN:
            g_strlcpy (battery_string, "Battery status is unknown!", STR_LTH);
            break;

        case CHARGED:
            g_strlcpy (battery_string, "Battery is charged!", STR_LTH);
            break;

        case DISCHARGING:
            g_snprintf (battery_string, STR_LTH, "Battery is discharging (%i\% remaining)", percentage);
            break;

        case NOT_CHARGING:
            g_snprintf (battery_string, STR_LTH, "Battery is not charging (%i\% remaining)", percentage);
            break;

        case LOW_LEVEL:
            g_snprintf (battery_string, STR_LTH, "Battery level is low! (%i\% remaining)", percentage);
            break;

        case CRITICAL_LEVEL:
            g_snprintf (battery_string, STR_LTH, "Battery level is critical! (%i\% remaining)", percentage);
            break;

        case CHARGING:
            g_snprintf (battery_string, STR_LTH, "Battery is charging (%i\%)", percentage);
            break;

        default:
            battery_string[0] = '\0';
            break;
    }

    return battery_string;
}

static gchar* get_time_string (gint minutes)
{
    static gchar time_string[STR_LTH];
    gint hours;

    if (minutes < 0)
        return NULL;

    hours   = minutes / 60;
    minutes = minutes % 60;

    if (hours > 0) {
        g_sprintf (time_string, "%2d hours, %2d minutes remaining", hours, minutes);
    } else {
        g_sprintf (time_string, "%2d minutes remaining", minutes);
    }

    return time_string;
}

static gchar* get_icon_name (gint state, gint percentage)
{
    static gchar icon_name[STR_LTH];

    if (icon_type == BATTERY_ICON_NOTIFICATION) {
        g_strlcpy (icon_name, "notification-battery", STR_LTH);
    } else {
        g_strlcpy (icon_name, "battery", STR_LTH);
    }

    if (state == MISSING || state == UNKNOWN) {
        if (icon_type == BATTERY_ICON_NOTIFICATION) {
            g_strlcat (icon_name, "-empty", STR_LTH);
        } else {
            g_strlcat (icon_name, "-missing", STR_LTH);
        }
    } else {
        if (icon_type == BATTERY_ICON_NOTIFICATION) {
                 if (percentage <= 20)  g_strlcat (icon_name, "-020", STR_LTH);
            else if (percentage <= 40)  g_strlcat (icon_name, "-040", STR_LTH);
            else if (percentage <= 60)  g_strlcat (icon_name, "-060", STR_LTH);
            else if (percentage <= 80)  g_strlcat (icon_name, "-080", STR_LTH);
            else                        g_strlcat (icon_name, "-100", STR_LTH);

                 if (state == CHARGING) g_strlcat (icon_name, "-plugged", STR_LTH);
            else if (state == CHARGED)  g_strlcat (icon_name, "-plugged", STR_LTH);
        } else {
                 if (percentage <= 20)  g_strlcat (icon_name, "-caution", STR_LTH);
            else if (percentage <= 40)  g_strlcat (icon_name, "-low", STR_LTH);
            else if (percentage <= 80)  g_strlcat (icon_name, "-good", STR_LTH);
            else                        g_strlcat (icon_name, "-full", STR_LTH);

                 if (state == CHARGING) g_strlcat (icon_name, "-charging", STR_LTH);
            else if (state == CHARGED)  g_strlcat (icon_name, "-charged", STR_LTH);
        }
    }

    if (icon_type == BATTERY_ICON_SYMBOLIC) {
        g_strlcat (icon_name, "-symbolic", STR_LTH);
    }

    return icon_name;
}

int main (int argc, char **argv)
{
    if (get_options (argc, argv) == FALSE)
        return -1;

    if (notify_init ("cbatticon") == FALSE)
        return -1;

    if (get_battery (argc > 1 ? argv[1] : NULL, FALSE) == FALSE)
        return -1;

    create_tray_icon ();
    gtk_main();

    return 0;
}
