/**
 * Spaghetti Display Server
 * Copyright (C) 2025  SpaghettiFork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <string.h>

#include <stdlib.h>
#include <stdio.h>

#include "compositor.h"

#include "xdg-system-bell-v1.h"

/* The xdg_system_bell_v1_interface global.  */
static struct wl_global *xdg_system_bell_global;

static void
Ring(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface)
{
	XBell(compositor.display, 100);
}

static void
Destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct xdg_system_bell_v1_interface xdg_system_bell_impl =
{
	.destroy = Destroy,
	.ring = Ring
};

static void
HandleBind(struct wl_client *client, void *data, uint32_t version,
		   uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client,
								  &xdg_system_bell_v1_interface,
								  version, id);

	if (!resource)
	{
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &xdg_system_bell_impl, NULL, NULL);
}

void XLInitSystemBell(void)
{
	xdg_system_bell_global = wl_global_create(compositor.wl_display,
												  &xdg_system_bell_v1_interface,
												  1, NULL, HandleBind);
}