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
#include "xf86.h"
#include "xf86Module.h"

#include <meatball/meatball.h>

static MODULESETUPPROTO(sauce_setup);
static MODULETEARDOWNPROTO(sauce_shutdown);

static XF86ModuleVersionInfo VersRec =
{
	.modname      = "sauce",
	.vendor       = "Spaghetti Fork",
	._modinfo1_   = MODINFOSTRING1,
	._modinfo2_   = MODINFOSTRING2,
	.xf86version  = XORG_VERSION_CURRENT,
	.majorversion = 1,
	.minorversion = 0,
	.patchlevel   = 0,
	.abiclass     = ABI_CLASS_EXTENSION,
	.abiversion   = ABI_EXTENSION_VERSION,
};

_X_EXPORT XF86ModuleData sauceModuleData =
{
	.vers = &VersRec,
	.setup = sauce_setup,
	.teardown = sauce_shutdown
};

static ScrnInfoPtr
GetPrimaryScreen(void)
{
	for (int i = 0; i < screenInfo.numScreens; i++) {
		const ScreenPtr screen = screenInfo.screens[i];

		if (!screen)
		{
			continue;
		}

		/**
		 * Skip any PRIME or offload screens since
		 * they won't be of any use to us.
		 */
		if (screen->is_offload_secondary || screen->is_output_secondary)
		{
			continue;
		}

		return xf86ScreenToScrn(screen);
	}

	return NULL;
}

static void *
sauce_setup(void *module, void *opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (setupDone) {
		if (errmaj)
		{
			*errmaj = LDR_ONCEONLY;
		}
		return NULL;
	}

	setupDone = TRUE;

	if (meatball_initialize(NULL))
	{
		return module;
	}
	else
	{
		return NULL;
	}
}

static void
sauce_shutdown(void *teardownData)
{
	meatball_shutdown();
}