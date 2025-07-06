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
#ifndef _MEATBALL_H
#define _MEATBALL_H

#include <pthread.h>

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

typedef int _MEATBALL_BOOL;

enum MEATBALL_BOOL
{
	MEATBALL_FALSE = 0,
	MEATBALL_TRUE  = 1
};

enum meatball_flags
{
	MB_NONE									= 0,
	MB_FORCE_SOFTWARE_RENDERING				= 0x2,
	MB_PICTURE_EXPOSE_ALL_SHM_FORMATS		= 0x4,
};

struct meatball_config
{
	/**
	 * Render device to use.
	 *
	 * Accepted values:
	 * - DRM path (e.g. /dev/dri/renderD128)
	 * - Screen name (e.g. "G0")
	 */
	const char*					renderer_string;
	/**
	 * Meatball flags to modify behaviour.
	 *
	 * See `meatball_flags` for more accepted values.
	 */
	unsigned long				meatball_flags;
};

extern _X_EXPORT _MEATBALL_BOOL meatball_initialize(struct meatball_config* config);

extern _X_EXPORT void meatball_shutdown(void);

#endif /* _MEATBALL_H */
