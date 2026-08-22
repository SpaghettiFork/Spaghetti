/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
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
#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <xf86.h>
#include <xf86Module.h>

static XF86ModuleVersionInfo VersRec = {
    .modname      = "soft2d",
    .vendor       = "Spaghetti Fork",
    ._modinfo1_   = MODINFOSTRING1,
    ._modinfo2_   = MODINFOSTRING2,
    .xf86version  = XORG_VERSION_CURRENT,
    .majorversion = 1,
    .minorversion = 0,
    .patchlevel   = 0,
    .abiclass     = ABI_CLASS_ANSIC,
    .abiversion   = ABI_ANSIC_VERSION,
    .moduleclass  = MOD_CLASS_NONE,
};

_X_EXPORT XF86ModuleData soft2dModuleData = {
    .vers = &VersRec,
};
