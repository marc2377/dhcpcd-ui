/*
 * dhcpcd-gtk
 * Copyright 2009 Roy Marples <roy@marples.name>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "dhcpcd-gtk.h"
#include "wpa.h"

static int
find_network(const char *ifname, const char *ssid)
{
	GType otype;
	GError *error;
	int id;
	size_t i;
	GPtrArray *array;
	GValueArray *varray;
	GValue *val;
	const char *str;
	char *nssid;

	otype = dbus_g_type_get_struct("GValueArray",
	    G_TYPE_INT,
	    G_TYPE_STRING,
	    G_TYPE_STRING,
	    G_TYPE_STRING,
	    G_TYPE_INVALID);
	otype = dbus_g_type_get_collection("GPtrArray", otype);
	error = NULL;
	if (!dbus_g_proxy_call(dbus, "ListNetworks", &error,
		G_TYPE_STRING, ifname, G_TYPE_INVALID,
		otype, &array, G_TYPE_INVALID))
	{
		g_warning("ListNetworks: %s", error->message);
		g_error_free(error);
		return -1;
	}

	for (i = 0; i < array->len; i++) {
		varray = g_ptr_array_index(array, i);
		val = g_value_array_get_nth(varray, 1);
		str = g_value_get_string(val);
		if (g_strcmp0(str, ssid) == 0) {
			val = g_value_array_get_nth(varray, 0);
			return g_value_get_int(val);
		}
	}

	if (!dbus_g_proxy_call(dbus, "AddNetwork", &error,
		G_TYPE_STRING, ifname, G_TYPE_INVALID,
		G_TYPE_INT, &id, G_TYPE_INVALID))
	{
		g_warning("AddNetwork: %s", error->message);
		g_error_free(error);
		return -1;
	}

	nssid = g_strconcat("\"", ssid, "\"", NULL);
	if (!dbus_g_proxy_call(dbus, "SetNetwork", &error,
		G_TYPE_STRING, ifname,
		G_TYPE_INT, id,
		G_TYPE_STRING, "ssid",
		G_TYPE_STRING, nssid,
		G_TYPE_INVALID,
		G_TYPE_INVALID))
	{
		g_warning("SetNetwork: %s", error->message);
		g_free(nssid);
		g_error_free(error);
		return -1;
	}
	g_free(nssid);

	return id;
}

static int
configure_network(const char *ifname, int id, const char *mgmt,
    const char *var, const char *val,
    bool quote)
{
	GError *error;
	char *str;
	static bool warned = false;
	GtkWidget *dialog;

	if (id == -1)
		return -1;

	dbus_g_proxy_call(dbus, "SetNetwork", &error,
	    G_TYPE_STRING, ifname,
	    G_TYPE_INT, id,
	    G_TYPE_STRING, "key_mgmt",
	    G_TYPE_STRING, mgmt,
	    G_TYPE_INVALID,
	    G_TYPE_INVALID);

	error = NULL;
	if (quote)
		str = g_strconcat("\"", val, "\"", NULL);
	else
		str = NULL;
	if (!dbus_g_proxy_call(dbus, "SetNetwork", &error,
		G_TYPE_STRING, ifname,
		G_TYPE_INT, id,
		G_TYPE_STRING, var,
		G_TYPE_STRING, quote ? str : val,
		G_TYPE_INVALID,
		G_TYPE_INVALID))
	{
		g_warning("SetNetwork: %s", error->message);
		g_free(str);
		g_error_free(error);
		dialog = gtk_message_dialog_new(NULL,
		    GTK_DIALOG_MODAL,
		    GTK_MESSAGE_ERROR,
		    GTK_BUTTONS_OK,
		    _("Failed to set password, probably too short."));
		gtk_window_set_title(GTK_WINDOW(dialog),
		    _("Error setting password"));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		return -1;
	}
	g_free(str);

	if (!dbus_g_proxy_call(dbus, "EnableNetwork", &error,
		G_TYPE_STRING, ifname,
		G_TYPE_INT, id,
		G_TYPE_INVALID,
		G_TYPE_INVALID))
	{
		g_warning("EnableNetwork: %s", error->message);
		g_error_free(error);
		return -1;
	}

	if (!dbus_g_proxy_call(dbus, "SaveConfig", &error,
		G_TYPE_STRING, ifname,
		G_TYPE_INVALID,
		G_TYPE_INVALID))
	{
		g_warning("SaveConfig: %s", error->message);
		if (!warned) {
			warned = true;
			dialog = gtk_message_dialog_new(NULL,
			    GTK_DIALOG_MODAL,
			    GTK_MESSAGE_ERROR,
			    GTK_BUTTONS_OK,
			    _("Failed to save wpa_supplicant configuration.\n\nYou should add update_config=1 to /etc/wpa_supplicant.conf.\nThis warning will not appear again until program restarted."));
			gtk_window_set_title(GTK_WINDOW(dialog),
			    _("Error saving configuration"));
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
		}
		g_error_free(error);
	}
/*
  if (!dbus_g_proxy_call(dbus, "Disconnect", &error,
  G_TYPE_STRING, ifname,
  G_TYPE_INVALID,
  G_TYPE_INVALID))
  {
  g_warning("Disconnect: %s", error->message);
  g_error_free(error);
  }
*/
	if (!dbus_g_proxy_call(dbus, "Reassociate", &error,
		G_TYPE_STRING, ifname,
		G_TYPE_INVALID,
		G_TYPE_INVALID))
	{
		g_warning("Reassociate: %s", error->message);
		g_error_free(error);
	}
	
	return 0;
}

static void
onEnter(_unused GtkWidget *widget, gpointer *data)
{
	gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT);
}

bool
wpa_configure(const struct if_ap *ifa)
{
	GtkWidget *dialog, *label, *psk, *vbox, *hbox;
	const char *var, *mgt;
	int result, id, retval;

	dialog = gtk_dialog_new_with_buttons(ifa->ssid,
	    NULL,
	    GTK_DIALOG_MODAL,
	    GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
	    GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
	    NULL);
	gtk_window_set_resizable(GTK_WINDOW(dialog), false);
	gtk_window_set_icon_name(GTK_WINDOW(dialog), "network-wireless-encrypted");
	gtk_dialog_set_default_response(GTK_DIALOG(dialog),
	    GTK_RESPONSE_ACCEPT);
	vbox = GTK_DIALOG(dialog)->vbox;

	hbox = gtk_hbox_new(false, 2);
	label = gtk_label_new(_("Pre Shared Key:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, false, false, 0);
	psk = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(psk), 130);
	g_signal_connect(G_OBJECT(psk), "activate", G_CALLBACK(onEnter), dialog);
	gtk_box_pack_start(GTK_BOX(hbox), psk, true, true, 0);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);

	gtk_widget_show_all(dialog);
	result = gtk_dialog_run(GTK_DIALOG(dialog));
	
	id = -1;
	retval = -1;
	if (result == GTK_RESPONSE_ACCEPT) {
		id = find_network(ifa->ifname, ifa->ssid);
		if (g_strcmp0(ifa->flags, "[WEP]") == 0) {
			mgt = "NONE";
			var = "wep_key0";
		} else {
			mgt = "WPA-PSK";
			var = "psk";
		}
		if (id != -1) {
			retval = configure_network(ifa->ifname, id, mgt, var,
			    gtk_entry_get_text(GTK_ENTRY(psk)), true);
		}
	}
	gtk_widget_destroy(dialog);
	return retval == -1 ? false : true;
}