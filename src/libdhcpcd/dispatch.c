/*
 * libdhcpcd
 * Copyright 2009-2014 Roy Marples <roy@marples.name>
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

#include <stdlib.h>
#include <string.h>

#define IN_LIBDHCPCD
#include "libdhcpcd.h"

static const char * const dhcpcd_types[] =
    { "link", "ipv4", "ra", "dhcp6", NULL };

static const char *
dhcpcd_message_get_string(DHCPCD_MESSAGE *msg)
{
	DBusMessageIter args;
	char *str;

	if (dbus_message_iter_init(msg, &args) &&
	    dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
	{
		dbus_message_iter_get_basic(&args, &str);
		return str;
	}
	return NULL;
}

static void
dhcpcd_handle_event(DHCPCD_CONNECTION *con, DHCPCD_MESSAGE *msg)
{
	DBusMessageIter args;
	DHCPCD_IF *i, *e, *l, *n, *nl;
	char *order, *o, *p;
	int ti;

	if (!dbus_message_iter_init(msg, &args))
		return;
	order = NULL;
	i = dhcpcd_if_new(con, &args, &order);
	if (i == NULL)
		return;
	p = order;
	n = nl = NULL;

	/* Remove all instances on carrier drop */
	if (strcmp(i->reason, "NOCARRIER") == 0 ||
	    strcmp(i->reason, "DEPARTED") == 0 ||
	    strcmp(i->reason, "STOPPED") == 0)
	{
		l = NULL;
		for (e = con->interfaces; e; e = n) {
			n = e->next;
			if (strcmp(e->ifname, i->ifname) == 0) {
				if (strcmp(e->type, i->type) == 0)
					l = nl = e;
				else {
					if (l)
						l->next = e->next;
					else
						con->interfaces = e->next;
					free(e);
				}
			} else
				l = e;
		}
	}

	/* Find our pointer */
	if (nl == NULL) {
		l = NULL;
		for (e = con->interfaces; e; e = e->next) {
			if (strcmp(e->ifname, i->ifname) == 0 &&
			    strcmp(e->type, i->type) == 0)
			{
				nl = e;
				break;
			}
			l = e;
		}
	}
	if (nl) {
		/* Preserve the pointer for wireless history */
		n = nl->next;
		memcpy(nl, i, sizeof(*i));
		nl->next = n;
		free(i);
		i = nl;
	} else {
		/* Append it then */
		if (l)
			l->next = i;
		else
			con->interfaces = i;
		i->next = NULL;
	}

	/* Sort! */
	n = nl = NULL;
	while ((o = strsep(&p, " ")) != NULL) {
		for (ti = 0; dhcpcd_types[ti]; ti++) {
			l = NULL;
			for (e = con->interfaces; e; e = e->next) {
				if (strcmp(e->ifname, o) == 0 &&
				    strcmp(e->type, dhcpcd_types[ti]) == 0)
					break;
				l = e;
			}
			if (e == NULL)
				continue;
			if (l)
				l->next = e->next;
			else
				con->interfaces = e->next;
			e->next = NULL;
			if (nl == NULL)
				n = nl = e;
			else {
				nl->next = e;
				nl = e;
			}
		}
	}
	/* Free any stragglers */
	while (con->interfaces) {
		e = con->interfaces->next;
		free(con->interfaces);
		con->interfaces = e;
	}
	con->interfaces = n;

	if (con->event)
		con->event(con, i, con->signal_data);
}

bool
dhcpcd_dispatch_message(DHCPCD_CONNECTION *con, DHCPCD_MESSAGE *msg)
{
	bool handled;
	const char *str;
	DHCPCD_IF *ifp;

	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
		return false;

	handled = true;
	dbus_connection_ref(con->bus);
	dbus_message_ref(msg);
	if (dbus_message_is_signal(msg, DHCPCD_SERVICE, "StatusChanged")) {
		con->status = strdup(dhcpcd_message_get_string(msg));
		if (strcmp(con->status, "down") == 0) {
			dhcpcd_if_free(con->interfaces);
			con->interfaces = NULL;
		}
		if (con->status_changed)
			con->status_changed(con, con->status,
			    con->signal_data);
	}
	else if (dbus_message_is_signal(msg, DHCPCD_SERVICE, "ScanResults"))
	{
		if (con->wi_scanresults) {
			str = dhcpcd_message_get_string(msg);
			ifp = dhcpcd_if_find(con, str, "link");
			if (ifp)
				con->wi_scanresults(con, ifp, con->signal_data);
		}
	} else if (dbus_message_is_signal(msg, DHCPCD_SERVICE, "Event"))
		dhcpcd_handle_event(con, msg);
	else
		handled = false;
	dbus_message_unref(msg);
	dbus_connection_unref(con->bus);
	return handled;
}
