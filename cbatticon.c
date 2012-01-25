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
#include "libubat.h"

/* todo: */
/* problem of current_rate: it can take a bit of time to have the refreshed */
/* info so the value is not always accurate when the notification is sent.  */

#define STR_LTH 256
#define UPDATE_INT 20
#define ERROR_TIME -1

struct BatteryInfo *info;
int old_status;
void update_tray_icon_state(GtkStatusIcon *tray_icon);
gchar* get_icon_name (gchar *time);
void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gchar *time);

/**
 * create_tray_icon()
 * Creates a new tray icon and initializes the timer for it.
 **/
GtkStatusIcon* create_tray_icon (void)
{
	GtkStatusIcon *tray_icon = gtk_status_icon_new ();

	gtk_status_icon_set_tooltip (tray_icon, "Battery Monitor");
	gtk_status_icon_set_visible (tray_icon, TRUE);

	update_tray_icon (tray_icon);
	g_timeout_add_seconds (UPDATE_INT, (GSourceFunc)update_tray_icon, (gpointer)tray_icon);

	return tray_icon;
}

/**
 * update_tray_icon(GtkStatusIcon)
 * @param tray_icon - The tray icon to be updated.
 **/
gboolean update_tray_icon (GtkStatusIcon *tray_icon)
{
	/* This piece of code ensures that multiple updates do not occur? */
	gboolean lock = FALSE;

	if (!lock) {
		lock = TRUE;
		update_battery(info);
		update_tray_icon_state (tray_icon);
		lock = FALSE;
	} else
		g_fprintf(stderr, "Tray update locked!?");

	return TRUE;
}

/**
 * update_tray_icon_state(GtkStatusIcon)
 * Update the state of the tray icon to match the information known about the battery.
 * @param tray_icon - The GtkStatusIcon to update.
 **/
void update_tray_icon_state (GtkStatusIcon *tray_icon) {
	if (info == NULL) {
		g_fprintf(stderr, "No battery present in system!");
		return;
	}
	if (old_status != info->status) {	
		old_status = info->status;
		notify_battery_information();	
		
		if (info->status == CHARGED)	
			set_tooltip_and_icon(tray_icon, "");
		
		if (info->status == CHARGING)	
			set_tooltip_and_icon(tray_icon, "");
		return;		
	}
	set_tooltip_and_icon(tray_icon, "");
	return;	
}

/**
 * Set the tooltip and the icon.
 **/
void set_tooltip_and_icon (GtkStatusIcon *tray_icon, gchar *time)
{
	gchar tooltip[STR_LTH], pct[STR_LTH], ti[STR_LTH];

	if (info->status == MISSING)
		g_stpcpy (tooltip, "No battery present!");
	else {
		g_stpcpy (tooltip, "Battery ");

		if (info->status == CHARGING)
			g_strlcat (tooltip, "charging ", STR_LTH);
		else if (info->status == CHARGED)
			g_strlcat (tooltip, "charged ", STR_LTH);

		g_sprintf (pct, "(%i\%)", info->percent);
		g_strlcat (tooltip, pct, STR_LTH);

		if (time[0] != '\0') {
			g_sprintf (ti, "\n%s", time);
			g_strlcat (tooltip, ti, STR_LTH);
		}
	}

	gtk_status_icon_set_tooltip (tray_icon, tooltip);
	gtk_status_icon_set_from_icon_name (tray_icon, get_icon_name (time));
}

/**
 * Notify the User of 
 **/
void notify_message (gchar *message)
{
	NotifyNotification *note = notify_notification_new ("Battery Monitor", message, NULL);

	notify_notification_set_timeout (note, 10000);
	notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
	notify_notification_show (note, NULL);
}

void notify_battery_information ()
{
	gchar message[STR_LTH], pct[STR_LTH], ti[STR_LTH];

	g_stpcpy (message, "Battery ");

	if (info->status == CHARGED)
		g_strlcat (message, "charged!", STR_LTH);
	else {
		if (info->status == CHARGING)
			g_strlcat (message, "charging!", STR_LTH);
		else {
			g_sprintf (pct, "has %i\% remaining", info->percent);
			g_strlcat (message, pct, STR_LTH);
		}

		//if (time[0] != '\0') {
		//	g_sprintf (ti, "\n%s", time);
		//	g_strlcat (message, ti, STR_LTH);
		//}
	}

	NotifyNotification *note = notify_notification_new ("Battery Monitor", message, (gchar *)get_icon_name (time));

	if (info->status == LOW_POWER) {
		notify_notification_set_timeout (note, NOTIFY_EXPIRES_NEVER);
		notify_notification_set_urgency (note, NOTIFY_URGENCY_CRITICAL);
	}
	else
		notify_notification_set_timeout (note, 10000);

	notify_notification_show (note, NULL);
}

gchar* get_time_string (gint minutes)
{
	gchar time[STR_LTH];
	gint hours;
	if(minutes<0){
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

gchar* get_icon_name (gchar *time)
{
	gchar icon_name[STR_LTH];

	g_stpcpy (icon_name, "battery");

	if (info->status == MISSING)
		g_strlcat (icon_name, "-missing", STR_LTH);
	else {
		if (info->percent < 20)
			g_strlcat (icon_name, "-caution", STR_LTH);
		else if (info->percent < 40)
			g_strlcat (icon_name, "-low", STR_LTH);
		else if (info->percent < 80)
			g_strlcat (icon_name, "-good", STR_LTH);
		else
			g_strlcat (icon_name, "-full", STR_LTH);

		if (info->status == CHARGING)
			g_strlcat (icon_name, "-charging", STR_LTH);
		else if (info->status == CHARGED)
			g_strlcat (icon_name, "-charged", STR_LTH);
	}

	return icon_name;
}

int main(int argc, char **argv)
{
	GtkStatusIcon *tray_icon;
	gchar *battery_path;
	old_status = UNKNOWN;
	gtk_init (&argc, &argv);
	notify_init ("Battery Monitor");
	info = init_battery("BAT0"); 
	if (info == NULL) return 1;
//	get_battery (argc > 1 ? argv[1] : NULL);
	tray_icon = create_tray_icon ();

	gtk_main();
	return 0;
}
