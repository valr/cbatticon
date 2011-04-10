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
//#include <dbus/dbus.h>
#include <libacpi.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <libnotify/notify.h>

gboolean lock = FALSE;
gint battery_num = 0;
gint battery_state = 1; 
// -1 - Battery missing
// 0 - Critical, notified
// 1 - normal
// 2 - charging
// 3 - charged

void set_icon(GtkStatusIcon *icon, gint state, gint percent, gchar *time);

void notify_user(gint state, gint percent, gchar *time);

void notify_message(gchar *message);

gchar * get_icon_name(gint state, gint percent, gchar *time);

battery_t* check_battery()
{
	battery_t *binfo;
	global_t *global = malloc(sizeof(global_t));
	
	int batterystate;
	
	batterystate = init_acpi_batt(global);
	
	if (batterystate == SUCCESS)
	{
		if (global->batt_count < battery_num)
		{
			fprintf(stderr, "No batteries in system!\n");
			exit(-1);
		}
		
		binfo = &batteries[battery_num];
		
		read_acpi_batt(battery_num);
		
	}
	else
	{
		fprintf(stderr, "ACPI not supported!\n");
		exit(-1);
	}
	free(global);
	
	return binfo;
}

gchar *get_time(gint mins)
{
	GString *time;
	gint hours, minutes;
	
	hours = mins / 60;
	minutes = mins - (60 * hours);
	
	time = g_string_new("");
	if (hours > 0)
		g_string_sprintf(time, "%2d hours, %2d minutes remaining", hours, minutes);
	else
		g_string_sprintf(time, "%2d minutes remaining", minutes);
	return time->str;
	
}


void update_state(GtkStatusIcon *tray_icon, battery_t *binfo)
{
	if (!binfo->present)
	{
		set_icon(tray_icon, 3, -1, "");
		gtk_status_icon_set_tooltip(tray_icon, "No battery present!");
		return;
	}
	
	//g_print("Battery State: %i\%\n", binfo->percentage);
	if (binfo->charge_state == C_CHARGED)
	{
		if (battery_state != 3)
		{
			battery_state = 3;
			notify_user(3, binfo->percentage, "");
		}
		set_icon(tray_icon, 3, 100, "");
		return;
	}
	if (binfo->charge_state == C_CHARGE)
	{
		
		if (binfo->present_rate == 0)
		{
			notify_message("Battery isn't charging!");
		}
		else if (battery_state != 2)
		{
			battery_state = 2;
			notify_user(2, binfo->percentage, get_time(binfo->charge_time));
		}
		set_icon(tray_icon, 2, binfo->percentage, get_time(binfo->charge_time));
		return;
	}
	if ((binfo->percentage < 20) && (battery_state != 0))
	{
		battery_state = 0;
		notify_user(0, binfo->percentage, get_time(binfo->remaining_time));
	}
	set_icon(tray_icon, 1, binfo->percentage,  get_time(binfo->remaining_time));
	
}


static gboolean update_tray(GtkStatusIcon *widget)
{
	if (check_acpi_support() < 0)
	{
		g_print("No ACPI support!\n");
		return FALSE;
	}
	battery_t *binfo = NULL;
	
	if (!lock)
	{
		lock = TRUE;
		//g_print("Checking battery!\n");
		binfo = check_battery();
		lock = FALSE;
	}
	else
	{
		g_print("LOCKED!?\n");
		return FALSE;
	}
	
	update_state(widget, binfo);
	
	
	return TRUE; 
}


void tray_icon_on_click(GtkStatusIcon *status_icon, gpointer user_data)
{
	printf("Clicked on tray icon!\n");
}

void tray_icon_on_menu(GtkStatusIcon *status_icon, guint button, guint activate_time, gpointer user_data)
{
	g_print("MENU! %i\n", button);
}

void notify_user(gint state, gint percent, gchar *time)
{
	GString *message = g_string_new("Battery ");
	
	if (state == 3)
	{
		g_string_append(message, "Charged!");
	}
	else
	{
		 if (state == 2)
		{
			g_string_append(message, "Charging!");
		}
		else
		{
			g_string_sprintfa(message, "has %i\% remaining", percent);
		}
			
		if (time != "")
		{
			g_string_sprintfa(message, "\n%s", time);
		}
	}
	
	
	
	NotifyNotification *note;
	note = notify_notification_new("Battery Monitor", message->str, get_icon_name(state, percent, time));
	
	if (state == 0)
	{
		notify_notification_set_timeout(note, NOTIFY_EXPIRES_NEVER);
		notify_notification_set_urgency(note, NOTIFY_URGENCY_CRITICAL);
	}
	else
	{
		notify_notification_set_timeout(note, 10000);
	}
	GError *error = NULL;
	notify_notification_show(note, &error);
	
	
	
}

void notify_message(gchar *message)
{
	NotifyNotification *note;
	note = notify_notification_new("Battery Monitor", message, NULL);
	notify_notification_set_timeout(note, 10000);
	notify_notification_set_urgency(note, NOTIFY_URGENCY_CRITICAL);
	GError *error = NULL;
	notify_notification_show(note, &error);
}

gchar * get_icon_name(gint state, gint percent, gchar *time)
{
	GString *filename;
	filename = g_string_new("gpm-battery-");
	
	
	if (percent < 20)
		g_string_append(filename, "000");
	else if (percent < 40)
		g_string_append(filename, "020");
	else if (percent < 60)
		g_string_append(filename, "040");
	else if (percent < 80)
		g_string_append(filename, "060");
	else if (percent < 100)
		g_string_append(filename, "080");
		
	
	if (state == 2)
	{
		g_string_append(filename, "-charging");
	}
	else if (state == 3)
	{
		g_string_append(filename, "charged");
	}
	
	return filename->str;
}


void set_icon(GtkStatusIcon *tray_icon, gint state, gint percent, gchar *time)
{
	
	GString *tooltip;
	
	
	tooltip = g_string_new("Battery ");
	
	
	if (state == 2)
	{
		g_string_append(tooltip, " charging");
	}
	else if (state == 3)
	{
		g_string_append(tooltip, " charged");
	}
	
	//g_string_append(tooltip, percent);
	g_string_sprintfa(tooltip, " (%i\%)", percent);
	
	if (time != "")
	{
		g_string_sprintfa(tooltip, "\n%s", time);
	}
	
	gtk_status_icon_set_tooltip(tray_icon, tooltip->str);
	
	//g_print("File: %s\n", filename->str);
	
	//gtk_status_icon_set_from_file(icon, filename->str);
	gtk_status_icon_set_from_icon_name(tray_icon, get_icon_name(state, percent, time));
}



static GtkStatusIcon *create_tray_icon() 
{
	GtkStatusIcon *tray_icon;

	tray_icon = gtk_status_icon_new();
	
	g_signal_connect(G_OBJECT(tray_icon), "activate", G_CALLBACK(tray_icon_on_click), NULL);
	g_signal_connect(G_OBJECT(tray_icon), "popup-menu", G_CALLBACK(tray_icon_on_menu), NULL);

	gtk_status_icon_set_tooltip(tray_icon, "Tray Icon");

	gtk_status_icon_set_visible(tray_icon, TRUE);

	update_tray(tray_icon);
	
	g_timeout_add(5000, (GSourceFunc) update_tray, (gpointer) tray_icon);
	
	//notify_message("Battery Monitor!");
	return tray_icon;
}







int main(int argc, char **argv)
{
	GtkStatusIcon *tray_icon;
	gtk_init(&argc, &argv);
	notify_init("Battery Monitor");
	tray_icon = create_tray_icon();
	gtk_main();
	
	return 0;
}
