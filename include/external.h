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
#ifndef EXTERNAL_H
#define EXTERNAL_H

#include "os.h"

/* Callbacks run on the server thread. Copy output into a queue and defer
 * protocol dispatch and client destruction until the current server operation
 * returns. Buffers and ClientPtr are borrowed for the duration of each call.
 * write returns count on success, or a negative value on allocation failure.
 * send_fd takes ownership of fd on success (return 0); failure leaves ownership
 * with the caller. closed runs once, after client resources have been freed.
 */
typedef struct {
    int (*write)   (ClientPtr client, int count, const void *data, void *closure);
    int (*send_fd) (ClientPtr client, int fd, void *closure);
    void (*closed) (ClientPtr client, void *closure);
} ExternalClientCallbacks;

/* Creates a local client without an X transport. Callbacks and credentials
 * are copied. credentials must include uid, gid and pid; supplementary groups
 * are optional. Destroy through CloseDownClient, and use FakeClientID(index)
 * for resources. NULL indicates invalid arguments or allocation/slot failure.
 * Output failure marks noClientException; the owner must arrange deferred
 * CloseDownClient processing for exceptional clients.
 * ClientRec retains its existing layout.
 */
extern MEATBALL_EXPORT ClientPtr
dixCreateExternalClient(const ExternalClientCallbacks *callbacks,
                        const LocalClientCredRec *credentials, void *closure);

#endif
