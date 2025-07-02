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

/* Logging. */
#ifndef LOG_H
#define LOG_H

#include <X11/Xdefs.h>
#include <X11/Xfuncproto.h>

enum ExitCode {
    EXIT_NO_ERROR = 0,
    EXIT_ERR_ABORT = 1,
    EXIT_ERR_CONFIGURE = 2,
    EXIT_ERR_DRIVERS = 3,
};

typedef enum _LogParameter {
    XLOG_FLUSH,
    XLOG_SYNC,
    XLOG_VERBOSITY,
    XLOG_FILE_VERBOSITY
} LogParameter;

/* Flags for log messages. */
typedef enum {
    X_PROBED,                   /* Value was probed */
    X_CONFIG,                   /* Value was given in the config file */
    X_DEFAULT,                  /* Value is a default */
    X_CMDLINE,                  /* Value was given on the command line */
    X_NOTICE,                   /* Notice */
    X_ERROR,                    /* Error message */
    X_WARNING,                  /* Warning message */
    X_INFO,                     /* Informational message */
    X_NONE,                     /* No prefix */
    X_NOT_IMPLEMENTED,          /* Not implemented */
    X_DEBUG,                    /* Debug message */
    X_UNKNOWN = -1              /* unknown -- this must always be last */
} MessageType;

extern _X_EXPORT const char *
LogInit(const char *fname, const char *backup);
extern void
LogSetDisplay(void);
extern _X_EXPORT void
LogClose(enum ExitCode error);
extern _X_EXPORT Bool
LogSetParameter(LogParameter param, int value);
extern _X_EXPORT void
LogVMessageVerb(MessageType type, int verb, const char *format, va_list args)
_X_ATTRIBUTE_PRINTF(3, 0);
extern _X_EXPORT void
LogMessageVerb(MessageType type, int verb, const char *format, ...)
_X_ATTRIBUTE_PRINTF(3, 4);
extern _X_EXPORT void
LogMessage(MessageType type, const char *format, ...)
_X_ATTRIBUTE_PRINTF(2, 3);
extern _X_EXPORT void
LogMessageVerbSigSafe(MessageType type, int verb, const char *format, ...)
_X_ATTRIBUTE_PRINTF(3, 4);
extern _X_EXPORT void
LogVMessageVerbSigSafe(MessageType type, int verb, const char *format, va_list args)
_X_ATTRIBUTE_PRINTF(3, 0);

extern _X_EXPORT void
LogVHdrMessageVerb(MessageType type, int verb,
                   const char *msg_format, va_list msg_args,
                   const char *hdr_format, va_list hdr_args)
_X_ATTRIBUTE_PRINTF(3, 0)
_X_ATTRIBUTE_PRINTF(5, 0);
extern _X_EXPORT void
LogHdrMessageVerb(MessageType type, int verb,
                  const char *msg_format, va_list msg_args,
                  const char *hdr_format, ...)
_X_ATTRIBUTE_PRINTF(3, 0)
_X_ATTRIBUTE_PRINTF(5, 6);

extern _X_EXPORT void
FatalError(const char *f, ...)
_X_ATTRIBUTE_PRINTF(1, 2)
    _X_NORETURN;

#ifdef DEBUG
#define DebugF ErrorF
#else
#define DebugF(...)             /* */
#endif

extern _X_EXPORT void
VErrorF(const char *f, va_list args)
_X_ATTRIBUTE_PRINTF(1, 0);
extern _X_EXPORT void
ErrorF(const char *f, ...)
_X_ATTRIBUTE_PRINTF(1, 2);
extern _X_EXPORT void
VErrorFSigSafe(const char *f, va_list args)
_X_ATTRIBUTE_PRINTF(1, 0);
extern _X_EXPORT void
ErrorFSigSafe(const char *f, ...)
_X_ATTRIBUTE_PRINTF(1, 2);
extern _X_EXPORT void
LogPrintMarkers(void);

extern _X_EXPORT void
xorg_backtrace(void);

#endif // LOG_H
