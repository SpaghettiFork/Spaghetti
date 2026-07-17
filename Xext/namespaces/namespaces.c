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
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/Xfuncproto.h>

#include "os/osdep.h"

#include "scrnintstr.h"
#include "inputstr.h"
#include "windowstr.h"
#include "propertyst.h"
#include "colormapst.h"
#include "privates.h"
#include "registry.h"
#include "xacestr.h"
#include "xace.h"
#include "extinit.h"
#include "extnsionst.h"
#include "dixstruct.h"
#include "opaque.h"
#include "selection.h"

#include "namespacesproto.h"
#include "namespacesstr.h"

#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h>
#include <sys/user.h>
#elif defined(__OpenBSD__)
#include <kvm.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#elif defined(__NetBSD__)
#include <sys/sysctl.h>
#include <sys/user.h>
#elif defined(__APPLE__)
#include <libproc.h>
#endif

static DevPrivateKeyRec xNSClientKeyRec;
static DevPrivateKeyRec xNSWindowKeyRec;
static int xNSErrorBase;
static int xnsEventBase;

#define xNSClientKey (&xNSClientKeyRec)
#define xNSWindowKey (&xNSWindowKeyRec)
#define XNS_ERROR(err) (xNSErrorBase + (err))

static xNSNamespaceRec *xNSNamespaces[XNS_MAX_NAMESPACES] = { NULL };

static xNSNamespaceRec *
xns_find_namespace(uint32_t id)
{
    if (id >= XNS_MAX_NAMESPACES)
        return NULL;
    return xNSNamespaces[id];
}

static int
xns_alloc_namespace_id(void)
{
    for (int i = 1; i < XNS_MAX_NAMESPACES; i++) {
        if (!xNSNamespaces[i])
            return i;
    }
    return -1;
}

static inline Bool
xns_ns_add_client(xNSNamespaceRec *ns, ClientPtr client)
{
    ClientPtr *tmp = realloc(ns->clients,
        (ns->num_clients + 1) * sizeof(ClientPtr));
    if (!tmp)
        return FALSE;
    ns->clients = tmp;
    ns->clients[ns->num_clients++] = client;
    return TRUE;
}

static inline void
xns_ns_remove_client(xNSNamespaceRec *ns, ClientPtr client)
{
    for (int i = 0; i < ns->num_clients; i++) {
        if (ns->clients[i] == client) {
            if (i < ns->num_clients - 1)
                ns->clients[i] = ns->clients[ns->num_clients - 1];
            ns->num_clients--;
            return;
        }
    }
}

static inline Bool
xns_ns_not_root(uint32_t target_ns, uint32_t client_ns)
{
    return target_ns != XNS_ROOT_NAMESPACE &&
           target_ns != client_ns &&
           client_ns != XNS_ROOT_NAMESPACE;
}

uint32_t
xns_client_namespace(ClientPtr client)
{
    xNSClientRec *rec;

    if (!client)
        return XNS_ROOT_NAMESPACE;

    rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!rec)
        return XNS_ROOT_NAMESPACE;

    return rec->namespace_id;
}

uint32_t
xns_window_namespace(WindowPtr pWin)
{
    xNSWindowRec *rec;

    if (!pWin)
        return XNS_ROOT_NAMESPACE;

    rec = dixLookupPrivate(&pWin->devPrivates, xNSWindowKey);
    if (!rec)
        return XNS_ROOT_NAMESPACE;

    return rec->namespace_id;
}

int
xns_create_namespace(ClientPtr client)
{
    xNSNamespaceRec *ns;
    xNSClientRec *client_rec;
    int id;

    id = xns_alloc_namespace_id();
    if (id < 0)
        return BadAlloc;

    ns = calloc(1, sizeof(xNSNamespaceRec));
    if (!ns)
        return BadAlloc;

    ns->id = id;
    ns->parent_id = XNS_ROOT_NAMESPACE;

    ns->clients = malloc(sizeof(ClientPtr));
    if (!ns->clients) {
        free(ns);
        return BadAlloc;
    }
    ns->clients[0] = client;
    ns->num_clients = 1;

    xNSNamespaces[id] = ns;

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (client_rec)
        client_rec->namespace_id = id;

#ifdef DEBUG
    LogMessage(X_DEBUG, "xNS: Created new namespace %d\n", id);
#endif
    return Success;
}

void
xns_destroy_namespace(uint32_t id)
{
    xNSNamespaceRec *ns;

    if (id == XNS_ROOT_NAMESPACE)
        return;

    ns = xns_find_namespace(id);
    if (!ns)
        return;

    for (int i = 0; i < ns->num_clients; i++) {
        xNSClientRec *rec = dixLookupPrivate(&ns->clients[i]->devPrivates,
                                              xNSClientKey);
        if (rec)
            rec->namespace_id = XNS_ROOT_NAMESPACE;
    }

    free(ns->clients);
    free(ns);
    xNSNamespaces[id] = NULL;

    xns_broadcast_event(xNSSubtypeNamespaceDestroyed, id, 0);
}

/* Remove client from its namespace */
void
xns_remove_client(ClientPtr client)
{
    xNSClientRec *client_rec;
    xNSNamespaceRec *ns;

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!client_rec)
        return;

    ns = xns_find_namespace(client_rec->namespace_id);
    if (!ns)
        return;

    /* Remove client from namespace's client list */
    xns_ns_remove_client(ns, client);

    /* Destroy namespace if empty (but never destroy root) */
    if (ns->num_clients == 0 && ns->id != XNS_ROOT_NAMESPACE) {
        xns_destroy_namespace(ns->id);
    }
}

/* Move client to a namespace.
 * target_ns: 0=root, XNS_NEW_NAMESPACE=NEW */
void
xns_move_to_namespace(ClientPtr client, uint32_t target_ns, Bool promote_siblings)
{
    xNSClientRec *client_rec, *sibling_rec;
    xNSNamespaceRec *ns, *dest_ns;
    uint32_t old_ns;

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!client_rec)
        return;

    /* Don't move the client's NS to the same NS */
    if (client_rec->namespace_id == target_ns)
        return;

    old_ns = client_rec->namespace_id;

    /* Remove from current namespace */
    ns = xns_find_namespace(old_ns);
    if (ns)
        xns_ns_remove_client(ns, client);

    /* Determine destination */
    if (target_ns == XNS_NEW_NAMESPACE) {
        /* Create a new namespace for this client */
        if (xns_create_namespace(client) != Success) {
            /* re-add to old namespace if possible on failure */
            if (ns)
                xns_ns_add_client(ns, client);
            return;
        }
        /* xns_create_namespace already set namespace_id and added to ns */
        xns_broadcast_event(xNSSubtypeNamespaceCreated, client_rec->namespace_id, 0);
        goto cleanup;
    }

    /* Move to destination namespace */
    dest_ns = xns_find_namespace(target_ns);
    if (!dest_ns) {
        /* re-add to old namespace as the target doesn't exist */
        if (ns)
            xns_ns_add_client(ns, client);
        return;
    }

    if (xns_ns_add_client(dest_ns, client))
        client_rec->namespace_id = target_ns;

    /* Promote all other clients in the same namespace to root (only when moving TO root) */
    if (target_ns == XNS_ROOT_NAMESPACE && promote_siblings) {
        for (int i = 1; i < MAXCLIENTS; i++) {
            ClientPtr c = clients[i];
            if (c && c->clientState == ClientStateRunning && c != client) {
                sibling_rec = dixLookupPrivate(&c->devPrivates, xNSClientKey);
                if (sibling_rec && sibling_rec->namespace_id == old_ns) {
                    if (ns)
                        xns_ns_remove_client(ns, c);

                    if (xns_ns_add_client(dest_ns, c))
                        sibling_rec->namespace_id = XNS_ROOT_NAMESPACE;
#ifdef DEBUG
                    LogMessage(X_DEBUG, "xNS: Promoted sibling client %p to root namespace\n", c);
#endif
                }
            }
        }
    }

cleanup:
    /* Destroy old namespace if empty */
    if (ns && ns->num_clients == 0)
        xns_destroy_namespace(ns->id);

    xns_broadcast_event(xNSSubtypeClientMoved, client_rec->namespace_id, client->index);
}

void
xns_add_window(WindowPtr pWin)
{
    xNSWindowRec *rec;
    ClientPtr client;

    if (!pWin)
        return;

    rec = dixLookupPrivate(&pWin->devPrivates, xNSWindowKey);
    if (!rec)
        return;

    /* Get the creating client */
    client = wClient(pWin);
    if (client) {
        rec->namespace_id = xns_client_namespace(client);
    } else {
        /* Root window or no client */
        rec->namespace_id = XNS_ROOT_NAMESPACE;
    }
}

void
xns_remove_window(WindowPtr pWin)
{
    /* No-op for now; windows are automatically cleaned up */
}

static inline ClientPtr
xns_find_client(int client_id)
{
    if (client_id < 0 || client_id >= MAXCLIENTS)
        return NULL;
    return clients[client_id];
}

/*
 * Event delivery
 */
static Bool
xns_event_work_proc(ClientPtr pClient, void *closure)
{
    xNSWorkItem *item = closure;
    xNSClientRec *rec;
    xNSEvent ev;

    rec = dixLookupPrivate(&pClient->devPrivates, xNSClientKey);
    if (rec && (rec->eventMask & (1 << item->subtype))) {
        ev = (xNSEvent) {
            .type = xnsEventBase + item->subtype,
            .subtype = item->subtype,
            .sequenceNumber = pClient->sequence,
            .timestamp = currentTime.milliseconds,
            .namespaceId = item->namespaceId,
            .clientId = item->clientId,
        };
        WriteEventsToClient(pClient, 1, (xEvent *) &ev);
    }

    free(item);
    return TRUE;
}

static void
xns_broadcast_event(int subtype, uint32_t namespaceId, uint32_t clientId)
{
    xNSNamespaceRec *root = xNSNamespaces[XNS_ROOT_NAMESPACE];
    xNSWorkItem *item;

    if (!root)
        return;

    for (int i = 0; i < root->num_clients; i++) {
        ClientPtr c = root->clients[i];
        if (c && c->clientState == ClientStateRunning) {
            item = malloc(sizeof(xNSWorkItem));
            if (_X_UNLIKELY(!item))
                continue;
            item->subtype     = subtype;
            item->namespaceId = namespaceId;
            item->clientId    = clientId;
            QueueWorkProc(xns_event_work_proc, c, item);
        }
    }
}

/*
 * XACE Callbacks
 */

static void
xNSResource(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceResourceAccessRec *rec = calldata;
    xNSWindowRec *win_rec;
    xNSClientRec *client_rec;
    uint32_t win_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* Only filter window resources */
    if (rec->rtype != RT_WINDOW)
        return;

    /* Server client can access everything */
    if (rec->client == serverClient)
        return;

    /* Detect WM: client managing root window = SubstructureRedirect = WM */
    if (rec->access_mode & DixManageAccess) {
        WindowPtr pWin = rec->res;
        if (pWin == pWin->drawable.pScreen->root) {
            client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);
            if (client_rec && client_rec->namespace_id != XNS_ROOT_NAMESPACE) {
                xns_move_to_namespace(rec->client, XNS_ROOT_NAMESPACE, TRUE);
#ifdef DEBUG
                LogMessage(X_DEBUG,
                    "xNS: Detected WM (SubstructureRedirect on root), promoted to root\n");
#endif
                return;
            }
        }
    }

    win_rec = dixLookupPrivate(&((WindowPtr) rec->res)->devPrivates, xNSWindowKey);
    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);

    if (!win_rec || !client_rec)
        return;

    win_ns = win_rec->namespace_id;
    client_ns = client_rec->namespace_id;

    /* Root namespace visible to all */
    if (win_ns == XNS_ROOT_NAMESPACE)
        return;

    /* Same namespace */
    if (win_ns == client_ns)
        return;

    /* Root client can see everything */
    if (client_ns == XNS_ROOT_NAMESPACE)
        return;

    /* Cross-namespace access denied */
#ifdef DEBUG
    LogMessage(X_DEBUG,
               "xNS: xNSResource DENIED client ns=%d accessing window ns=%d rtype=%d\n",
               client_ns, win_ns, rec->rtype);
#endif
    rec->status = BadAccess;
}

static void
xNSSend(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceSendAccessRec *rec = calldata;
    xNSWindowRec *win_rec;
    xNSClientRec *client_rec;
    uint32_t win_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* Generic send (no window) is allowed */
    if (rec->pWin == NULL)
        return;

    /* No client or server client can send anywhere */
    if (!rec->client || rec->client == serverClient)
        return;

    win_rec = dixLookupPrivate(&rec->pWin->devPrivates, xNSWindowKey);
    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);

    if (!win_rec || !client_rec)
        return;

    win_ns = win_rec->namespace_id;
    client_ns = client_rec->namespace_id;

    if (xns_ns_not_root(win_ns, client_ns)) {
#ifdef DEBUG
        LogMessage(X_DEBUG,
                   "xNS: xNSSend DENIED client ns=%d sending to window ns=%d\n",
                   client_ns, win_ns);
#endif
        rec->status = BadAccess;
    }
}

static void
xNSReceive(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceReceiveAccessRec *rec = calldata;
    xNSWindowRec *win_rec;
    xNSClientRec *client_rec;
    uint32_t win_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* No client or server client can receive anything */
    if (!rec->client || rec->client == serverClient)
        return;

    win_rec = dixLookupPrivate(&rec->pWin->devPrivates, xNSWindowKey);
    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);

    if (!win_rec || !client_rec)
        return;

    win_ns = win_rec->namespace_id;
    client_ns = client_rec->namespace_id;

    if (xns_ns_not_root(win_ns, client_ns)) {
#ifdef DEBUG
        LogMessage(X_DEBUG,
                   "xNS: xNSReceive DENIED client ns=%d receiving from window ns=%d\n",
                   client_ns, win_ns);
#endif
        rec->status = BadAccess;
    }
}

static void
xNSProperty(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XacePropertyAccessRec *rec = calldata;
    xNSWindowRec *win_rec;
    xNSClientRec *client_rec;
    uint32_t win_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* Server client can access any property */
    if (rec->client == serverClient)
        return;

    win_rec = dixLookupPrivate(&rec->pWin->devPrivates, xNSWindowKey);
    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);

    if (!win_rec || !client_rec)
        return;

    win_ns = win_rec->namespace_id;
    client_ns = client_rec->namespace_id;

    if (xns_ns_not_root(win_ns, client_ns)) {
        rec->status = BadAccess;
    }
}

static void
xNSSelection(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceSelectionAccessRec *rec = calldata;
    Selection *pSel;
    xNSWindowRec *win_rec;
    xNSClientRec *client_rec;
    uint32_t sel_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* Server client can access any selection */
    if (rec->client == serverClient)
        return;

    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);
    if (!client_rec)
        return;

    pSel = *rec->ppSel;

    /* Clipboard isolation check */
    if (!IsolateClipboard)
        return;

    client_ns = client_rec->namespace_id;

    if (pSel && pSel->pWin) {
        win_rec = dixLookupPrivate(&pSel->pWin->devPrivates, xNSWindowKey);
        if (win_rec) {
            sel_ns = win_rec->namespace_id;
            if (sel_ns != XNS_ROOT_NAMESPACE && sel_ns != client_ns) {
                rec->status = BadAccess;
            }
        }
    }
}

static void
xNSSelectionOwner(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    SelectionInfoRec *info = calldata;
    xNSClientRec *client_rec;
    const char *name;

    if (!UseNamespaces)
        return;

    if (info->kind != SelectionSetOwner)
        return;

    if (!info->client || info->client == serverClient)
        return;

    client_rec = dixLookupPrivate(&info->client->devPrivates, xNSClientKey);
    if (!client_rec)
        return;

    if (client_rec->namespace_id == XNS_ROOT_NAMESPACE)
        return;

    name = NameForAtom(info->selection->selection);
    if (!name)
        return;

    if (strncmp(name, "WM_S", 4) == 0 ||
        strcmp(name, "_NET_WM_CM_S0") == 0 ||
        strcmp(name, "_NET_SYSTEM_TRAY_S0") == 0) {
        
        xns_move_to_namespace(info->client, XNS_ROOT_NAMESPACE, TRUE);
#ifdef DEBUG
        LogMessage(X_DEBUG, "xNS: Detected %s (selection %s), promoted to root\n",
                   strncmp(name, "WM_S", 4) == 0 ? "WM" :
                   strcmp(name, "_NET_WM_CM_S0") == 0 ? "compositor" : "system tray",
                   name);
#endif
    }
}

static void
xNSClient(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceClientAccessRec *rec = calldata;
    xNSClientRec *target_rec, *client_rec;
    uint32_t target_ns, client_ns;

    if (!UseNamespaces)
        return;

    /* Server client can access any client */
    if (rec->client == serverClient)
        return;

    target_rec = dixLookupPrivate(&rec->target->devPrivates, xNSClientKey);
    client_rec = dixLookupPrivate(&rec->client->devPrivates, xNSClientKey);

    if (!target_rec || !client_rec)
        return;

    target_ns = target_rec->namespace_id;
    client_ns = client_rec->namespace_id;

    if (xns_ns_not_root(target_ns, client_ns)) {
        rec->status = BadAccess;
    }
}

static void
xNSClientState(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    NewClientInfoRec *pci = calldata;
    xNSClientRec *state;

    state = dixLookupPrivate(&pci->client->devPrivates, xNSClientKey);

    switch (pci->client->clientState) {
    case ClientStateInitial:
        state->namespace_id = XNS_ROOT_NAMESPACE;
        break;
    case ClientStateRunning:
        if (UseNamespaces) {
            xns_assign_client(pci->client);
            state = dixLookupPrivate(&pci->client->devPrivates, xNSClientKey);
#ifdef DEBUG
            LogMessage(X_DEBUG,
                       "xNS: Client %p assigned to namespace %d\n",
                       pci->client, state ? state->namespace_id : -1);
#endif
        }
        break;
    case ClientStateGone:
    case ClientStateRetained:
        xns_remove_client(pci->client);
        break;
    }
}

static inline pid_t
xns_get_ppid(pid_t pid)
{
#if defined(__linux__)
    char path[64];
    FILE *f;
    pid_t ppid = 0;
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, "PPid:\t%d", &ppid) == 1) break;
        fclose(f);
    }
    return ppid;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    if (sysctl(mib, 4, &kp, &len, NULL, 0) == 0 && len > 0)
        return kp.ki_ppid;
    return 0;
#elif defined(__OpenBSD__)
    int cnt;
    struct kinfo_proc *kp = kvm_getprocs(NULL, KERN_PROC_PID, pid, &cnt);
    if (kp && cnt > 0)
        return kp->p_ppid;
    return 0;
#elif defined(__NetBSD__)
    struct kinfo_proc2 kp;
    size_t len = sizeof(kp);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    if (sysctl(mib, 4, &kp, &len, NULL, 0) == 0 && len > 0)
        return kp.p_ppid;
    return 0;
#elif defined(__APPLE__)
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info))
        return info.pbi_ppid;
    return 0;
#else
#error "SPAGHETTI-NS requires Parent PID lookup, which is missing on this OS." 
#endif
}

int
xns_assign_client(ClientPtr client)
{
    static Bool xNSHadFirstClient = FALSE;
    xNSClientRec *client_rec;
    LocalClientCredRec *lcc = NULL;

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!client_rec)
        return BadAlloc;

    if (!xNSHadFirstClient) {
        xNSHadFirstClient = TRUE;
        xNSNamespaceRec *root = xNSNamespaces[XNS_ROOT_NAMESPACE];
        if (root)
            xns_ns_add_client(root, client);
        xns_broadcast_event(xNSSubtypeClientJoined, XNS_ROOT_NAMESPACE, client->index);
        return Success;
    }

    /* Walk PPID chain (up to 3 levels) to find ancestor X client */
    if (GetLocalClientCreds(client, &lcc) != -1) {
        if (lcc->fieldsSet & LCC_PID_SET) {
            pid_t check_pid = lcc->pid;

            for (int depth = 0; depth < 3; depth++) {
                pid_t ppid = xns_get_ppid(check_pid);

                if (ppid <= 0)
                    break;

                /* Check if this PPID is a connected X client */
                for (int i = 1; i < MAXCLIENTS; i++) {
                    ClientPtr parent = clients[i];
                    if (parent && parent->clientState == ClientStateRunning) {
                        LocalClientCredRec *parent_lcc = NULL;
                        if (GetLocalClientCreds(parent, &parent_lcc) != -1) {
                            if ((parent_lcc->fieldsSet & LCC_PID_SET) &&
                                parent_lcc->pid == ppid) {
                                uint32_t parent_ns = xns_client_namespace(parent);

                                /* Only inherit non-root namespace */
                                if (parent_ns != XNS_ROOT_NAMESPACE) {
                                    xNSNamespaceRec *ns = xns_find_namespace(parent_ns);
                                    if (ns) {
                                        if (xns_ns_add_client(ns, client))
                                            client_rec->namespace_id = parent_ns;
                                    }

                                    FreeLocalClientCreds(parent_lcc);
                                    FreeLocalClientCreds(lcc);
                                    xns_broadcast_event(xNSSubtypeClientJoined, parent_ns, client->index);
                                    return Success;
                                }
                                FreeLocalClientCreds(parent_lcc);
                            } else {
                                FreeLocalClientCreds(parent_lcc);
                            }
                        }
                    }
                }

                check_pid = ppid;
            }
        }
        FreeLocalClientCreds(lcc);
    }

    /* No connected ancestor found - create new namespace */
    int rc = xns_create_namespace(client);
    if (rc == Success) {
        uint32_t ns = xns_client_namespace(client);
        xns_broadcast_event(xNSSubtypeNamespaceCreated, ns, 0);
        xns_broadcast_event(xNSSubtypeClientJoined, ns, client->index);
    }
    return rc;
}

static int
ProcNSQueryVersion(ClientPtr client)
{
    xNSQueryVersionReply rep;

    REQUEST_SIZE_MATCH(xNSQueryVersionReq);

    rep = (xNSQueryVersionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = SPAGHETTI_NS_MAJOR_VERSION,
        .minorVersion = SPAGHETTI_NS_MINOR_VERSION,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swaps(&rep.majorVersion);
        swaps(&rep.minorVersion);
    }

    WriteReplyToClient(client, sizeof(xNSQueryVersionReply), &rep);
    return Success;
}

static int
ProcNSListNamespaces(ClientPtr client)
{
    xNSListNamespacesReply rep;
    int num_visible = 0;
    xNSNamespaceInfo *info;

    REQUEST_SIZE_MATCH(xNSListNamespacesReq);

    uint32_t caller_ns = xns_client_namespace(client);

    /* Count visible namespaces */
    for (int i = 0; i < XNS_MAX_NAMESPACES; i++) {
        if (!xNSNamespaces[i])
            continue;
        if (caller_ns == XNS_ROOT_NAMESPACE ||
            xNSNamespaces[i]->id == XNS_ROOT_NAMESPACE ||
            xNSNamespaces[i]->id == caller_ns) {
            num_visible++;
        }
    }

    info = calloc(num_visible, sizeof(xNSNamespaceInfo));
    if (!info)
        return BadAlloc;

    int idx = 0;
    for (int i = 0; i < XNS_MAX_NAMESPACES; i++) {
        if (!xNSNamespaces[i])
            continue;
        if (caller_ns == XNS_ROOT_NAMESPACE ||
            xNSNamespaces[i]->id == XNS_ROOT_NAMESPACE ||
            xNSNamespaces[i]->id == caller_ns) {
            info[idx].namespaceId = xNSNamespaces[i]->id;
            info[idx].numClients = xNSNamespaces[i]->num_clients;
            info[idx].numWindows = xNSNamespaces[i]->num_windows;
            info[idx].parentId = xNSNamespaces[i]->parent_id;

            if (client->swapped) {
                swapl(&info[idx].namespaceId);
                swapl(&info[idx].numClients);
                swapl(&info[idx].numWindows);
                swapl(&info[idx].parentId);
            }
            idx++;
        }
    }

    rep = (xNSListNamespacesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = (num_visible * sizeof(xNSNamespaceInfo)) / 4,
        .numNamespaces = num_visible,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swapl(&rep.numNamespaces);
    }

    WriteReplyToClient(client, sizeof(xNSListNamespacesReply), &rep);
    WriteToClient(client, num_visible * sizeof(xNSNamespaceInfo), info);
    free(info);

    return Success;
}

static int
ProcNSGetClientNamespace(ClientPtr client)
{
    xNSGetClientNamespaceReply rep;
    ClientPtr target;
    xNSClientRec *target_rec;

    REQUEST(xNSGetClientNamespaceReq);
    REQUEST_SIZE_MATCH(xNSGetClientNamespaceReq);

    target = xns_find_client(stuff->clientId);
    if (!target) {
        client->errorValue = stuff->clientId;
        return XNS_ERROR(xNSBadClient);
    }

    target_rec = dixLookupPrivate(&target->devPrivates, xNSClientKey);

    rep = (xNSGetClientNamespaceReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .namespaceId = target_rec ? target_rec->namespace_id : XNS_ROOT_NAMESPACE,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swapl(&rep.namespaceId);
    }

    WriteReplyToClient(client, sizeof(xNSGetClientNamespaceReply), &rep);
    return Success;
}

static int
ProcNSGetWindowNamespace(ClientPtr client)
{
    xNSGetWindowNamespaceReply rep;
    WindowPtr pWin;
    xNSWindowRec *win_rec;
    int rc;

    REQUEST(xNSGetWindowNamespaceReq);
    REQUEST_SIZE_MATCH(xNSGetWindowNamespaceReq);

    rc = dixLookupWindow(&pWin, stuff->windowId, client, DixReadAccess);
    if (rc != Success)
        return rc;

    win_rec = dixLookupPrivate(&pWin->devPrivates, xNSWindowKey);

    rep = (xNSGetWindowNamespaceReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .namespaceId = win_rec ? win_rec->namespace_id : XNS_ROOT_NAMESPACE,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swapl(&rep.namespaceId);
    }

    WriteReplyToClient(client, sizeof(xNSGetWindowNamespaceReply), &rep);
    return Success;
}

static int
ProcNSMoveClient(ClientPtr client)
{
    xNSMoveClientReply rep;
    xNSClientRec *client_rec;
    ClientPtr target;

    REQUEST(xNSMoveClientReq);
    REQUEST_SIZE_MATCH(xNSMoveClientReq);

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!client_rec)
        return XNS_ERROR(xNSBadClient);

    /* Only root-namespace clients can move others */
    if (client_rec->namespace_id != XNS_ROOT_NAMESPACE) {
#ifdef DEBUG
        LogMessage(X_DEBUG, "xNS: xNSMoveClient DENIED non-root client\n");
#endif
        return XNS_ERROR(xNSBadAccess);
    }

    target = xns_find_client(stuff->clientId);
    if (!target) {
        client->errorValue = stuff->clientId;
        return XNS_ERROR(xNSBadClient);
    }

    xns_move_to_namespace(target, stuff->targetNamespace, TRUE);

    rep = (xNSMoveClientReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = Success,
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.status);
    }

    WriteReplyToClient(client, sizeof(xNSMoveClientReply), &rep);
    return Success;
}

static int
ProcNSPromoteSelf(ClientPtr client)
{
    xNSPromoteSelfReply rep;

    REQUEST(xNSPromoteSelfReq);
    REQUEST_SIZE_MATCH(xNSPromoteSelfReq);

    /* Check password if PromotePassword is configured */
    if (PromotePassword && PromotePassword[0] != '\0') {
        volatile int diff = 0;
        /* Do a fixed time password comparison so that no 
         * smartass tries to thrash the server with requests */
        for (int i = 0; i < XNS_MAX_PASSWORD_LENGTH; i++)
            diff |= (unsigned char)stuff->password[i] ^
                    (unsigned char)PromotePassword[i];
        if (diff != 0) {
#ifdef DEBUG
            LogMessage(X_DEBUG, "xNS: xNSPromoteSelf DENIED wrong password\n");
#endif
            return XNS_ERROR(xNSBadAccess);
        }
    }

    xns_move_to_namespace(client, XNS_ROOT_NAMESPACE, stuff->promoteChildren != 0);

    rep = (xNSPromoteSelfReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .status = Success,
    };

    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.status);
    }

    WriteReplyToClient(client, sizeof(xNSPromoteSelfReply), &rep);
    return Success;
}

static int
ProcNSGetClientPID(ClientPtr client)
{
    xNSGetClientPIDReply rep;
    ClientPtr target;
    LocalClientCredRec *lcc = NULL;

    REQUEST(xNSGetClientPIDReq);
    REQUEST_SIZE_MATCH(xNSGetClientPIDReq);

    /* Only root-namespace clients can query PIDs */
    if (xns_client_namespace(client) != XNS_ROOT_NAMESPACE)
        return XNS_ERROR(xNSBadAccess);

    target = xns_find_client(stuff->clientId);
    if (!target) {
        client->errorValue = stuff->clientId;
        return XNS_ERROR(xNSBadClient);
    }

    if (GetLocalClientCreds(target, &lcc) == -1 ||
        !(lcc->fieldsSet & LCC_PID_SET)) {
        if (lcc)
            FreeLocalClientCreds(lcc);
        return XNS_ERROR(xNSClientIsExternal);
    }

    rep = (xNSGetClientPIDReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .pid = lcc->pid,
    };

    FreeLocalClientCreds(lcc);

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swapl(&rep.pid);
    }

    WriteReplyToClient(client, sizeof(xNSGetClientPIDReply), &rep);
    return Success;
}

static int
ProcNSListClients(ClientPtr client)
{
    xNSListClientsReply rep;
    xNSNamespaceRec *ns;
    uint32_t caller_ns;
    CARD32 *ids;

    REQUEST(xNSListClientsReq);
    REQUEST_SIZE_MATCH(xNSListClientsReq);

    caller_ns = xns_client_namespace(client);

    if (caller_ns != XNS_ROOT_NAMESPACE &&
        stuff->namespaceId != XNS_ROOT_NAMESPACE &&
        stuff->namespaceId != caller_ns) {
#ifdef DEBUG
        LogMessage(X_DEBUG, "xNS: ProcNSListClients DENIED caller_ns=%u ns=%u\n",
                            caller_ns, stuff->namespaceId);
#endif
        return XNS_ERROR(xNSBadAccess);
    }

    ns = xns_find_namespace(stuff->namespaceId);
    if (!ns) {
#ifdef DEBUG
        LogMessage(X_DEBUG,
                   "xNS: ProcNSListClients namespace %u not found\n",
                   stuff->namespaceId);
#endif
        client->errorValue = stuff->namespaceId;
        return XNS_ERROR(xNSBadNamespace);
    }

    ids = calloc(ns->num_clients, sizeof(CARD32));
    if (!ids)
        return BadAlloc;

    for (int i = 0; i < ns->num_clients; i++)
        ids[i] = ns->clients[i]->index;

    if (client->swapped) {
        for (int i = 0; i < ns->num_clients; i++)
            swapl(&ids[i]);
    }

    rep = (xNSListClientsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = ns->num_clients,
        .numClients = ns->num_clients,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
        swapl(&rep.numClients);
    }

    WriteReplyToClient(client, sizeof(xNSListClientsReply), &rep);
    WriteToClient(client, ns->num_clients * sizeof(CARD32), ids);
    free(ids);

    return Success;
}

static int
ProcNSSelectEvents(ClientPtr client)
{
    xNSSelectEventsReply rep;
    xNSClientRec *client_rec;

    REQUEST(xNSSelectEventsReq);
    REQUEST_SIZE_MATCH(xNSSelectEventsReq);

    client_rec = dixLookupPrivate(&client->devPrivates, xNSClientKey);
    if (!client_rec)
        return XNS_ERROR(xNSBadClient);

    if (xns_client_namespace(client) != XNS_ROOT_NAMESPACE)
        return XNS_ERROR(xNSBadAccess);

    client_rec->eventMask = stuff->eventMask;

    rep = (xNSSelectEventsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
    };

    if (client->swapped) {
        swapl(&rep.length);
        swaps(&rep.sequenceNumber);
    }

    WriteReplyToClient(client, sizeof(xNSSelectEventsReply), &rep);
    return Success;
}

/*
 * Dispatch
 */
static int
ProcNSDispatch(ClientPtr client)
{
    REQUEST(xNSQueryVersionReq);

    switch (stuff->nsReqType) {
    case X_NSQueryVersion:
        return ProcNSQueryVersion(client);
    case X_NSListNamespaces:
        return ProcNSListNamespaces(client);
    case X_NSGetClientNamespace:
        return ProcNSGetClientNamespace(client);
    case X_NSGetWindowNamespace:
        return ProcNSGetWindowNamespace(client);
    case X_NSMoveClient:
        return ProcNSMoveClient(client);
    case X_NSListClients:
        return ProcNSListClients(client);
    case X_NSPromoteSelf:
        return ProcNSPromoteSelf(client);
    case X_NSGetClientPID:
        return ProcNSGetClientPID(client);
    case X_NSSelectEvents:
        return ProcNSSelectEvents(client);
    default:
        return BadRequest;
    }
}

static int
SProcNSDispatch(ClientPtr client)
{
    REQUEST(xNSQueryVersionReq);

    switch (stuff->nsReqType) {
    case X_NSQueryVersion:
    case X_NSListNamespaces:
    case X_NSGetClientNamespace:
    case X_NSGetWindowNamespace:
    case X_NSMoveClient:
    case X_NSListClients:
    case X_NSPromoteSelf:
    case X_NSGetClientPID:
    case X_NSSelectEvents:
        return ProcNSDispatch(client);
    default:
        return BadRequest;
    }
}

static void
SProcNSEvent(xNSEvent *from, xNSEvent *to)
{
    to->type = from->type;
    to->subtype = from->subtype;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->namespaceId, to->namespaceId);
    cpswapl(from->clientId, to->clientId);
}

static void
NSResetProc(ExtensionEntry *extEntry)
{
    for (int i = 0; i < XNS_MAX_NAMESPACES; i++) {
        if (xNSNamespaces[i]) {
            free(xNSNamespaces[i]->clients);
            free(xNSNamespaces[i]);
            xNSNamespaces[i] = NULL;
        }
    }
}

/*
 * Extension initialization
 */
void
xNSExtensionInit(void)
{
    int ret = TRUE;

    if (!UseNamespaces)
        return;

    /* Register private keys */
    if (!dixRegisterPrivateKey(xNSClientKey,
                               PRIVATE_CLIENT, sizeof(xNSClientRec)))
        FatalError("Can't allocate client private.\n");

    if (!dixRegisterPrivateKey(xNSWindowKey,
                               PRIVATE_WINDOW, sizeof(xNSWindowRec)))
        FatalError("Can't allocate window private.\n");

    /* Register XACE callbacks */
    ret &= XaceRegisterCallback(XACE_RESOURCE_ACCESS, xNSResource, NULL);
    ret &= XaceRegisterCallback(XACE_SEND_ACCESS, xNSSend, NULL);
    ret &= XaceRegisterCallback(XACE_RECEIVE_ACCESS, xNSReceive, NULL);
    ret &= XaceRegisterCallback(XACE_PROPERTY_ACCESS, xNSProperty, NULL);
    ret &= XaceRegisterCallback(XACE_SELECTION_ACCESS, xNSSelection, NULL);
    ret &= XaceRegisterCallback(XACE_CLIENT_ACCESS, xNSClient, NULL);

    /* Register client state callback */
    ret &= AddCallback(&ClientStateCallback, xNSClientState, NULL);

    /* Register selection owner callback for WM/compositor/tray detection */
    ret &= AddCallback(&SelectionCallback, xNSSelectionOwner, NULL);

    if (!ret)
        FatalError("Failed to register callbacks\n");

    /* Add extension to server */
    ExtensionEntry *extEntry = AddExtension(SPAGHETTI_NS_EXTENSION_NAME,
                 xNSNumEvents, xNSNumErrors,
                 ProcNSDispatch, SProcNSDispatch,
                 NSResetProc, StandardMinorOpcode);
    xNSErrorBase = extEntry->errorBase;
    xnsEventBase = extEntry->eventBase;

    /* Register event byte-swap functions */
    EventSwapVector[xnsEventBase + xNSSubtypeClientJoined] =
        (EventSwapPtr) SProcNSEvent;
    EventSwapVector[xnsEventBase + xNSSubtypeClientMoved] =
        (EventSwapPtr) SProcNSEvent;
    EventSwapVector[xnsEventBase + xNSSubtypeNamespaceCreated] =
        (EventSwapPtr) SProcNSEvent;
    EventSwapVector[xnsEventBase + xNSSubtypeNamespaceDestroyed] =
        (EventSwapPtr) SProcNSEvent;

    /* Initialize root namespace */
    xNSNamespaces[XNS_ROOT_NAMESPACE] = calloc(1, sizeof(xNSNamespaceRec));
    if (xNSNamespaces[XNS_ROOT_NAMESPACE]) {
        xNSNamespaces[XNS_ROOT_NAMESPACE]->id = XNS_ROOT_NAMESPACE;
        xNSNamespaces[XNS_ROOT_NAMESPACE]->parent_id = XNS_ROOT_NAMESPACE;
        xNSNamespaces[XNS_ROOT_NAMESPACE]->clients = NULL;
        xNSNamespaces[XNS_ROOT_NAMESPACE]->num_clients = 0;
        xNSNamespaces[XNS_ROOT_NAMESPACE]->num_windows = 0;
    }
}
