/*
 * Copyright (c) 2026 SpaghettiFork
 * SPDX-License-Identifier: MIT
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
    int (*write)(ClientPtr client, int count, const void *data, void *closure);
    int (*send_fd)(ClientPtr client, int fd, void *closure);
    void (*closed)(ClientPtr client, void *closure);
} ExternalClientCallbacks;

/* Creates a local client without an X transport. Callbacks and credentials
 * are copied. credentials must include uid, gid and pid; supplementary groups
 * are optional. Destroy through CloseDownClient, and use FakeClientID(index)
 * for resources. NULL indicates invalid arguments or allocation/slot failure.
 * Output failure marks noClientException; the owner must arrange deferred
 * CloseDownClient processing for exceptional clients.
 * ClientRec retains its existing layout.
 */
extern _X_EXPORT ClientPtr
dixCreateExternalClient(const ExternalClientCallbacks *callbacks,
                        const LocalClientCredRec *credentials, void *closure);

#endif
