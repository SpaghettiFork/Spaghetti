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
