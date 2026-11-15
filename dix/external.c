/*
 * Copyright (c) 2026 SpaghettiFork
 * SPDX-License-Identifier: MIT
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include "dixstruct.h"
#include "external_priv.h"

DevPrivateKeyRec externalClientPrivateKeyRec;

LocalClientCredRec *
dixCopyClientCreds(const LocalClientCredRec *credentials)
{
    LocalClientCredRec *copy;

    if (!credentials || credentials->nSuppGids < 0 ||
        (credentials->nSuppGids && !credentials->pSuppGids))
        return NULL;
    copy = malloc(sizeof(*copy));
    if (!copy)
        return NULL;
    *copy = *credentials;
    copy->pSuppGids = NULL;
    if (copy->nSuppGids) {
        copy->pSuppGids = xallocarray(copy->nSuppGids, sizeof(*copy->pSuppGids));
        if (!copy->pSuppGids) {
            free(copy);
            return NULL;
        }
        memcpy(copy->pSuppGids, credentials->pSuppGids,
               copy->nSuppGids * sizeof(*copy->pSuppGids));
    }
    return copy;
}

int
dixWriteExternalClient(ClientPtr client, int count, const void *data)
{
    ExternalClientRec *external = dixLookupPrivate(&client->devPrivates,
                                                   &externalClientPrivateKeyRec);
    int written = external->callbacks.write ?
        external->callbacks.write(client, count, data, external->closure) : -1;

    if (written != count) {
        MarkClientException(client);
        return -1;
    }
    return written;
}

int
dixSendExternalClientFD(ClientPtr client, int fd, Bool do_close)
{
    ExternalClientRec *external = dixLookupPrivate(&client->devPrivates,
                                                   &externalClientPrivateKeyRec);
    int sent_fd;

    if (client->clientGone || !external->callbacks.send_fd || fd < 0)
        return -1;
    sent_fd = do_close ? fd : dup(fd);
    if (sent_fd < 0)
        return -1;
    if (!do_close && fcntl(sent_fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(sent_fd);
        return -1;
    }
    if (external->callbacks.send_fd(client, sent_fd, external->closure) != 0) {
        if (!do_close)
            close(sent_fd);
        return -1;
    }
    return 0;
}

void
dixCloseExternalClient(ClientPtr client)
{
    ExternalClientRec *external = dixLookupPrivate(&client->devPrivates,
                                                   &externalClientPrivateKeyRec);

    if (external->callbacks.closed)
        external->callbacks.closed(client, external->closure);
    FreeLocalClientCreds(external->credentials);
    memset(external, 0, sizeof(*external));
}
