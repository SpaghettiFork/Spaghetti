/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "dixstruct.h"
#include "dix/dispatch.h"
#include "external.h"
#include "inputstr.h"
#include "opaque.h"
#include "xfixesint.h"
#include "tests-common.h"

enum { EXTERNAL_TEST_PAYLOAD_WORDS = 8, EXTERNAL_TEST_BUFFER_SIZE = 256 };

struct external_fixture {
    unsigned char output[EXTERNAL_TEST_BUFFER_SIZE];
    int length;
    int fd;
    unsigned states[ClientStateGone + 1];
    unsigned closed;
    unsigned freed;
    unsigned events;
    Bool fail_write;
    Bool fail_fd;
};

static int
write_output(ClientPtr client, int count, const void *data, void *closure)
{
    struct external_fixture *fixture = closure;

    if (fixture->fail_write)
        return -1;
    assert(count <= sizeof(fixture->output));
    fixture->length = count;
    memcpy(fixture->output, data, count);
    return count;
}

static int
send_fd(ClientPtr client, int fd, void *closure)
{
    struct external_fixture *fixture = closure;

    if (fixture->fail_fd)
        return -1;
    assert(fixture->fd == -1);
    fixture->fd = fd;
    return 0;
}

static void
client_closed(ClientPtr client, void *closure)
{
    struct external_fixture *fixture = closure;

    assert(client->clientGone);
    assert(client->clientState == ClientStateGone);
    assert(fixture->freed == 1);
    fixture->closed++;
}

static int
free_resource(void *value, XID id)
{
    struct external_fixture *fixture = value;

    fixture->freed++;
    return Success;
}

static void
client_state(CallbackListPtr *list, void *closure, void *data)
{
    struct external_fixture *fixture = closure;
    ClientPtr client = ((NewClientInfoRec *) data)->client;
    LocalClientCredRec *credentials;

    assert(client->external && client->local);
    assert(!client->osPrivate);
    assert(GetLocalClientCreds(client, &credentials) == 0);
    assert(credentials->pid == getpid());
    FreeLocalClientCreds(credentials);
    fixture->states[client->clientState]++;
}

static void
client_event(CallbackListPtr *list, void *closure, void *data)
{
    struct external_fixture *fixture = closure;
    EventInfoRec *info = data;

    assert(info->client->external);
    fixture->events += info->count;
}

static void
external_clients(void)
{
    ClientRec server = { .index = 0 };
    DeviceIntRec keyboard = { .type = MASTER_KEYBOARD };
    struct external_fixture fixture = { .fd = -1 };
    ExternalClientCallbacks callbacks = {
        .write = write_output,
        .send_fd = send_fd,
        .closed = client_closed
    };
    int group = getgid();
    LocalClientCredRec credentials = {
        .fieldsSet = LCC_UID_SET | LCC_GID_SET | LCC_PID_SET,
        .euid = getuid(), .egid = getgid(), .pid = getpid(),
        .nSuppGids = 1, .pSuppGids = &group
    };
    LocalClientCredRec *copy;
    ClientPtr client, exhausted;
    RESTYPE type;
    int saved_limit = LimitClients;
    XID id;
    xEvent event = { 0 };
    struct {
        xGenericEvent header;
        CARD32 payload[EXTERNAL_TEST_PAYLOAD_WORDS];
    } generic = { .header = { .type = GenericEvent,
                              .length = EXTERNAL_TEST_PAYLOAD_WORDS } };

    serverClient = clients[0] = &server;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&server.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&server));
    assert(XFixesClientDisconnectInit());
    type = CreateNewResourceType(free_resource, "external-test");
    assert(type);
    assert(AddCallback(&ClientStateCallback, client_state, &fixture));
    assert(AddCallback(&EventCallback, client_event, &fixture));
    LimitClients = 2;
    assert(!dixCreateExternalClient(NULL, &credentials, &fixture));
    client = dixCreateExternalClient(&callbacks, &credentials, &fixture);
    assert(client);
    assert(client->index == 1 && currentMaxClients == 2);
    exhausted = dixCreateExternalClient(&callbacks, &credentials, &fixture);
    assert(!exhausted);
    assert(fixture.states[ClientStateInitial] == 1);
    assert(fixture.states[ClientStateRunning] == 1);
    assert(ClientIsLocal(client));
    assert(GetClientFd(client) == -1);
    assert(GetLocalClientCreds(client, &copy) == 0);
    assert(copy->pSuppGids != credentials.pSuppGids);
    assert(copy->pSuppGids[0] == group);
    copy->pSuppGids[0]++;
    FreeLocalClientCreds(copy);
    assert(GetLocalClientCreds(client, &copy) == 0);
    assert(copy->pSuppGids[0] == group);
    FreeLocalClientCreds(copy);
    IgnoreClient(client);
    assert(client->ignoreCount == 1);
    AttendClient(client);
    assert(client->ignoreCount == 0);
    id = FakeClientID(client->index);
    assert(LegalNewID(id, client));
    assert(!LegalNewID(FakeClientID(serverClient->index), client));
    assert(AddResource(id, type, &fixture));
    assert(!LegalNewID(id, client));

    client->clientPtr = &keyboard;
    client->sequence = 1;
    event.u.u.type = PropertyNotify;
    WriteEventsToClient(client, 1, &event);
    assert(fixture.events == 1 && fixture.length == sizeof(event));
    assert(((xEvent *) fixture.output)->u.u.sequenceNumber == client->sequence);
    generic.payload[EXTERNAL_TEST_PAYLOAD_WORDS - 1] = 0x12345678;
    WriteEventsToClient(client, 1, (xEvent *) &generic);
    assert(fixture.events == 2 && fixture.length == sizeof(generic));
    assert(memcmp(fixture.output, &generic, sizeof(generic)) == 0);
    assert(WriteToClient(client, sizeof(event), &event) == sizeof(event));
    assert(fixture.events == 2 && fixture.length == sizeof(event));
    assert(!InsertFakeRequest(client, (char *) &event, sizeof(event)));
    assert(ReadRequestFromClient(client) == 0);

    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    assert(WriteFdToClient(client, fd, FALSE) == 0);
    assert(fixture.fd >= 0 && fixture.fd != fd);
    assert(fcntl(fixture.fd, F_GETFD) & FD_CLOEXEC);
    close(fixture.fd);
    fixture.fd = -1;
    assert(fcntl(fd, F_GETFD) >= 0);
    fixture.fail_fd = TRUE;
    assert(WriteFdToClient(client, fd, TRUE) == -1);
    assert(fcntl(fd, F_GETFD) >= 0);
    fixture.fail_fd = FALSE;
    assert(WriteFdToClient(client, fd, TRUE) == 0);
    assert(fixture.fd == fd);
    close(fixture.fd);
    fixture.fd = -1;

    fixture.fail_write = TRUE;
    WriteEventsToClient(client, 1, &event);
    assert(client->noClientException != Success);
    CloseDownClient(client);
    assert(fixture.closed == 1 && fixture.freed == 1);
    assert(fixture.states[ClientStateGone] == 1);
    assert(!clients[1] && currentMaxClients == 1);
    assert(dispatchException & DE_RESET);
    dispatchException = 0;
    fixture.freed = 0;
    fixture.fail_write = FALSE;
    client = dixCreateExternalClient(&callbacks, &credentials, &fixture);
    assert(client);
    id = FakeClientID(client->index);
    assert(AddResource(id, type, &fixture));
    xResourceReq request = { .id = id, .length = bytes_to_int32(sizeof(request)) };
    server.requestBuffer = &request;
    server.req_len = request.length;
    assert(ProcKillClient(&server) == Success);
    assert(fixture.closed == 2);
    DeleteCallback(&ClientStateCallback, client_state, &fixture);
    DeleteCallback(&EventCallback, client_event, &fixture);
    FreeAllResources();
    dixFreePrivates(server.devPrivates, PRIVATE_CLIENT);
    serverClient = clients[0] = NULL;
    LimitClients = saved_limit;
    dixResetPrivates();
}

const testfunc_t *
external_test(void)
{
    static const testfunc_t tests[] = { external_clients, NULL };
    return tests;
}
