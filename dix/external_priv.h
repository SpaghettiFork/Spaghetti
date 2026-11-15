/*
 * Copyright (c) 2026 SpaghettiFork
 * SPDX-License-Identifier: MIT
 */
#ifndef EXTERNAL_PRIV_H
#define EXTERNAL_PRIV_H

#include "external.h"
#include "privates.h"

typedef struct {
    ExternalClientCallbacks callbacks;
    LocalClientCredRec *credentials;
    void *closure;
} ExternalClientRec;

extern DevPrivateKeyRec externalClientPrivateKeyRec;

LocalClientCredRec *dixCopyClientCreds(const LocalClientCredRec *credentials);
int dixWriteExternalClient(ClientPtr client, int count, const void *data);
int dixSendExternalClientFD(ClientPtr client, int fd, Bool do_close);
void dixCloseExternalClient(ClientPtr client);

#endif
