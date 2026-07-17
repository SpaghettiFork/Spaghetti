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
#ifndef NAMESPACES_STR_H
#define NAMESPACES_STR_H

#include "dixstruct.h"
#include "windowstr.h"

/* Root namespace ID */
#define XNS_ROOT_NAMESPACE      0
#define XNS_MAX_NAMESPACES      256

/* Per-client namespace state */
typedef struct _xNSClientRec {
    uint32_t namespace_id;
    uint32_t eventMask;
} xNSClientRec;

/* Per-window namespace tag */
typedef struct _xNSWindowRec {
    uint32_t namespace_id;
} xNSWindowRec;

/* Namespace record */
typedef struct _xNSNamespace {
    uint32_t id;
    uint32_t parent_id;
    ClientPtr *clients;
    int num_clients;
    int num_windows;
} xNSNamespaceRec;

typedef struct _xNSWorkItem {
    int subtype;
    uint32_t namespaceId;
    uint32_t clientId;
} xNSWorkItem;

uint32_t xns_client_namespace(ClientPtr client);
uint32_t xns_window_namespace(WindowPtr pWin);

int xns_create_namespace(ClientPtr client);
void xns_destroy_namespace(uint32_t id);

int xns_assign_client(ClientPtr client);
void xns_remove_client(ClientPtr client);

void xns_move_to_namespace(ClientPtr client,
                           uint32_t target_ns, Bool promote_siblings);

void xns_add_window(WindowPtr pWin);
void xns_remove_window(WindowPtr pWin);

#endif /* NAMESPACES_STR_H */
