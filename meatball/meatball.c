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
#include <locale.h>

#include "misc.h"
#include <X11/Xlib.h>
#include <X11/Xfuncproto.h>

#include "meatball.h"
#include "compositor.h"

/* GLOBAL */ pthread_t meatball_thread;
/* GLOBAL */ Compositor compositor;

static void
DetermineServerTime(void) {
	struct timespec clock_spec;
	uint64_t clock_ms, truncated;

	/* Try to determine if the X server time is the same as the
	   monotonic time.  If it is not, certain features such as "active"
	   frame synchronization will not be available.  */

	clock_gettime(CLOCK_MONOTONIC, &clock_spec);
	const Time server_time = XLGetServerTimeRoundtrip();

	/* Convert the monotonic time to milliseconds.  */

	if (IntMultiplyWrapv(clock_spec.tv_sec, 1000, &clock_ms))
		goto overflow;

	if (IntAddWrapv(clock_ms, clock_spec.tv_nsec / 1000000,
	                &clock_ms))
		goto overflow;

	/* Truncate the time to 32 bits.  */
	truncated = clock_ms;

	/* Compare the clock time with the server time.  */
	if (llabs((long long) server_time - (long long) truncated) <= 5) {
		/* Since the difference between the server time and the monotonic
		   time is less than 5 ms, the server time is most likely the
		   monotonic time.  */
		compositor.server_time_monotonic = True;
	} else {
	overflow:
		compositor.server_time_monotonic = False;
		LogMessage(X_WARNING, "meatball: the X server time does not seem to"
		           " be synchronized with the monotonic time.  Multiple"
		           " subsurfaces may be displayed at a reduced maximum"
		           " frame rate.\n");
	}
}

static void
meatball_run(const void* __renderer_ptr__)
{
	/* Set the locale.  */
	setlocale(LC_ALL, "");

	/* Initialize Xlib threads.  */
	XInitThreads();

	Display *dpy = XOpenDisplay(NULL);
	struct wl_display *wl_display = wl_display_create();

	if (!dpy || !wl_display) {
		LogMessage(X_ERROR, "meatball: Display initialization failed\n");
		exit(1);
	}

	const char *socket = wl_display_add_socket_auto(wl_display);

	if (!socket) {
		LogMessage(X_ERROR, "meatball: Unable to add socket to Wayland display\n");
		exit(1);
	}

	/* Call XGetDefault with some dummy values to have the resource
	   database set up.  */
	XrmInitialize();
	XGetDefault(dpy, "dummmy", "value");

	/* Setup the internal compositor data. */
	compositor.app_name = "Meatball";
	compositor.resource_name = "12to11";
	compositor.display = dpy;
	compositor.conn = XGetXCBConnection(dpy);
	compositor.wl_display = wl_display;
	compositor.wl_socket = socket;
	compositor.wl_event_loop
			= wl_display_get_event_loop(wl_display);

	/* Initialize server time tracking very early.  */
	InitTime();

	InitXErrors();
	SubcompositorInit();
	InitSelections();

	XLInitTimers();
	XLInitAtoms();

	/**
	 * Initialize renderers immediately after timers and
	 * atoms are setup.
	 */
	InitRenderers();
	SetRenderer(__renderer_ptr__);

	XLInitRROutputs();
	XLInitCompositor();
	XLInitSurfaces();
	XLInitShm();
	XLInitXdgWM();
	XLInitXdgSurfaces();
	XLInitXdgToplevels();
	XLInitFrameClock();
	XLInitSubsurfaces();
	XLInitSeats();
	XLInitDataDevice();
	XLInitPopups();
	XLInitDmabuf();
	XLInitXData();
	XLInitXSettings();
	XLInitIconSurfaces();
	XLInitPrimarySelection();
	XLInitExplicitSynchronization();
	XLInitWpViewporter();
	XLInitDecoration();
	XLInitTextInput();
	XLInitSinglePixelBuffer();
	XLInitDrmLease();
	XLInitPointerConstraints();
	XLInitRelativePointer();
	XLInitKeyboardShortcutsInhibit();
	XLInitIdleInhibit();
	XLInitPointerGestures();
	XLInitXdgActivation();
	XLInitTearingControl();
	XLInitTest();

	/* This has to come after the rest of the initialization.  */
	DetermineServerTime();
	XLRunCompositor();

	/* Exit the thread if we manage to exit the loop. */
	pthread_exit(NULL);
}

static MODULESETUPPROTO(meatballSetup);
static MODULETEARDOWNPROTO(meatballShutdown);

static XF86ModuleVersionInfo VersRec =
{
	.modname      = "meatball",
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

_X_EXPORT XF86ModuleData meatballModuleData =
{
	.vers = &VersRec,
	.setup = meatballSetup,
	.teardown = meatballShutdown
};

#if 0
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
#endif

static void *
meatballSetup(void *module, void *opts, int *errmaj, int *errmin)
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
	LogMessage(X_INFO, "meatball: Initializing.");

#if 0
	const ScrnInfoPtr ptr = GetPrimaryScreen();
	if (ptr)
	{
		LogMessage(X_INFO, "meatball: Using screen %d as backing.", ptr->scrnIndex);
	}
	else
	{
		LogMessage(X_ERROR, "meatball: Failed to get the primary screen! Aborting.");
		return NULL;
	}

	pthread_create(&meatball_thread, NULL, meatball_run, NULL);
#endif
	return module;
}

static void
meatballShutdown(void *teardownData)
{
#if 0
	if (meatball_thread)
	{
		pthread_kill(meatball_thread, SIGSTOP);
	}
#endif
}
