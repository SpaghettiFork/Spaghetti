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
#ifndef NAMESPACES_PROTO_H
#define NAMESPACES_PROTO_H

#include <X11/Xmd.h>

#define SPAGHETTI_NS_EXTENSION_NAME "SPAGHETTI-NS"
#define SPAGHETTI_NS_MAJOR_VERSION  1
#define SPAGHETTI_NS_MINOR_VERSION  0

/* Opcodes */
#define X_NSQueryVersion            0
#define X_NSListNamespaces          1
#define X_NSGetClientNamespace      2
#define X_NSGetWindowNamespace      3
#define X_NSMoveClient              4
#define X_NSListClients             5
#define X_NSPromoteSelf             6
#define X_NSGetClientPID            7
#define X_NSSelectEvents            8
#define X_NSNumRequests             9

/* Errors */
#define xNSBadNamespace             0
#define xNSBadAccess                1
#define xNSBadClient                2
#define xNSClientIsExternal         3
#define xNSNumErrors                4

/* Events */
#define xNSSubtypeClientJoined       0
#define xNSSubtypeClientMoved        1
#define xNSSubtypeNamespaceCreated   2
#define xNSSubtypeNamespaceDestroyed 3
#define xNSNumEvents                 4

/* PromoteSelf password length */
#define XNS_MAX_PASSWORD_LENGTH     32
/* MoveClient targetNamespace special values */
#define XNS_NEW_NAMESPACE           0xFFFFFFFF

/*
 * All requests share a common 8-byte header:
 *   CARD8  reqType        (extension opcode)
 *   CARD8  nsReqType      (request opcode)
 *   CARD16 sequenceNumber
 *   CARD32 length          (in 4-byte units, including header)
 */

/*
 * Query version
 */
typedef struct _xNSQueryVersionReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD16 majorVersion;
    CARD16 minorVersion;
} xNSQueryVersionReq;

typedef struct _xNSQueryVersionReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD16 majorVersion;
    CARD16 minorVersion;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSQueryVersionReply;

/*
 * List namespaces
 */
typedef struct _xNSListNamespacesReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
} xNSListNamespacesReq;

typedef struct _xNSNamespaceInfo {
    CARD32 namespaceId;
    CARD32 numClients;
    CARD32 numWindows;
    CARD32 parentId;
} xNSNamespaceInfo;

typedef struct _xNSListNamespacesReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 numNamespaces;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSListNamespacesReply;

/*
 * Get client namespace
 */
typedef struct _xNSGetClientNamespaceReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 clientId;
} xNSGetClientNamespaceReq;

typedef struct _xNSGetClientNamespaceReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 namespaceId;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSGetClientNamespaceReply;

/*
 * Get window namespace
 */
typedef struct _xNSGetWindowNamespaceReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 windowId;
} xNSGetWindowNamespaceReq;

typedef struct _xNSGetWindowNamespaceReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 namespaceId;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSGetWindowNamespaceReply;

/*
 * Move client to a namespace (root-namespace clients only)
 * targetNamespace: 0 = root, XNS_NEW_NAMESPACE = create new, other = move to that ns
 */
typedef struct _xNSMoveClientReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 clientId;
    CARD32 targetNamespace;
} xNSMoveClientReq;

typedef struct _xNSMoveClientReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 status;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSMoveClientReply;

/*
 * List clients in a namespace
 */
typedef struct _xNSListClientsReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 namespaceId;
    CARD32 pad[2];
} xNSListClientsReq;

typedef struct _xNSListClientsReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 numClients;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSListClientsReply;

/*
 * Promote self (and optionally children) to root namespace
 * password: null-padded, compared if PromotePassword is configured
 */
typedef struct _xNSPromoteSelfReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 promoteChildren;
    CARD8 password[32];
} xNSPromoteSelfReq;

typedef struct _xNSPromoteSelfReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 status;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSPromoteSelfReply;

/*
 * Get client process ID
 */
typedef struct _xNSGetClientPIDReq {
    CARD8 reqType;
    CARD8 nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 clientId;
} xNSGetClientPIDReq;

typedef struct _xNSGetClientPIDReply {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 pid;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xNSGetClientPIDReply;

/*
 * Extension event
 * Both namespaceId and clientId always populated.
 * For NamespaceCreated/Destroyed, clientId is 0.
 */
typedef struct _xNSEvent {
    CARD8  type;           /* eventBase + subtype */
    CARD8  subtype;        /* xNSSubtype* */
    CARD16 sequenceNumber;
    CARD32 timestamp;      /* currentTime.milliseconds */
    CARD32 namespaceId;
    CARD32 clientId;
    CARD32 pad[4];
} xNSEvent;

/*
 * Select events
 */
typedef struct _xNSSelectEventsReq {
    CARD8  reqType;
    CARD8  nsReqType;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 eventMask;      /* bitmask: bit N = subtype N */
} xNSSelectEventsReq;

typedef struct _xNSSelectEventsReply {
    CARD8  type;
    CARD8  pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 pad[5];
} xNSSelectEventsReply;

#endif /* NAMESPACES_PROTO_H */
