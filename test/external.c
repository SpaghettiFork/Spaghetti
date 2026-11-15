/**
 * Test suite for external, non-X11 protocol clients
 */
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

enum {
    EXTERNAL_TEST_PAYLOAD_WORDS = 8,
    EXTERNAL_TEST_BUFFER_SIZE = 256,
    EXTERNAL_TEST_PAYLOAD_SENTINEL = 0x12345678
};

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

    if (fixture->fail_write) {
        return -1;
    } else {
        assert(count <= sizeof(fixture->output));
        fixture->length = count;
        memcpy(fixture->output, data, count);
        return count;
    }
}

static int
send_fd(ClientPtr client, int fd, void *closure)
{
    struct external_fixture *fixture = closure;

    if (fixture->fail_fd) {
        return -1;
    } else {
        assert(fixture->fd == -1);
        fixture->fd = fd;
        return 0;
    }
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

struct external_test {
    struct external_fixture fixture;
    ClientRec server;
    DeviceIntRec keyboard;
    LocalClientCredRec credentials;
    int group;
    RESTYPE type;
    int saved_limit;
};

static const ExternalClientCallbacks external_test_callbacks = {
    .write = write_output,
    .send_fd = send_fd,
    .closed = client_closed
};

static void
external_test_init(struct external_test *test)
{
    *test = (struct external_test) { .fixture.fd = -1 };
    test->group = getgid();
    test->credentials = (LocalClientCredRec) {
        .fieldsSet = LCC_UID_SET | LCC_GID_SET | LCC_PID_SET,
        .euid = getuid(),
        .egid = getgid(),
        .pid = getpid(),
        .nSuppGids = 1,
        .pSuppGids = &test->group
    };
    test->keyboard.type = MASTER_KEYBOARD;
    test->saved_limit = LimitClients;

    serverClient = clients[0] = &test->server;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&test->server.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&test->server));
    assert(XFixesClientDisconnectInit());

    test->type = CreateNewResourceType(free_resource, "external-test");
    assert(test->type);

    assert(AddCallback(&ClientStateCallback, client_state, &test->fixture));
    assert(AddCallback(&EventCallback, client_event, &test->fixture));
    LimitClients = 2;
}

static void
external_test_fini(struct external_test *test)
{
    DeleteCallback(&ClientStateCallback, client_state, &test->fixture);
    DeleteCallback(&EventCallback, client_event, &test->fixture);
    FreeAllResources();
    dixFreePrivates(test->server.devPrivates, PRIVATE_CLIENT);
    serverClient = clients[0] = NULL;
    LimitClients = test->saved_limit;
    dixResetPrivates();
}

static ClientPtr
external_test_client(struct external_test *test)
{
    ClientPtr client, exhausted;

    assert(!dixCreateExternalClient(NULL, &test->credentials, &test->fixture));

    client = dixCreateExternalClient(&external_test_callbacks,
                                     &test->credentials, &test->fixture);
    assert(client);
    assert(client->index == 1 && currentMaxClients == 2);

    exhausted = dixCreateExternalClient(&external_test_callbacks,
                                        &test->credentials, &test->fixture);
    assert(!exhausted);
    assert(test->fixture.states[ClientStateInitial] == 1);
    assert(test->fixture.states[ClientStateRunning] == 1);
    assert(ClientIsLocal(client));
    assert(GetClientFd(client) == -1);

    return client;
}

static void
external_test_client_state(ClientPtr client)
{
    IgnoreClient(client);
    assert(client->ignoreCount == 1);
    AttendClient(client);
    assert(client->ignoreCount == 0);
}

static void
external_test_credentials(struct external_test *test, ClientPtr client)
{
    LocalClientCredRec *copy;

    assert(GetLocalClientCreds(client, &copy) == 0);
    assert(copy->pSuppGids != test->credentials.pSuppGids);
    assert(copy->pSuppGids[0] == test->group);

    copy->pSuppGids[0]++;
    FreeLocalClientCreds(copy);
    assert(GetLocalClientCreds(client, &copy) == 0);
    assert(copy->pSuppGids[0] == test->group);
    FreeLocalClientCreds(copy);
}

static void
external_test_resources(struct external_test *test, ClientPtr client)
{
    XID id = FakeClientID(client->index);

    assert(LegalNewID(id, client));
    assert(!LegalNewID(FakeClientID(serverClient->index), client));
    assert(AddResource(id, test->type, &test->fixture));
    assert(!LegalNewID(id, client));
}

static void
external_test_events(struct external_test *test, ClientPtr client)
{
    xEvent event = { 0 };
    struct {
        xGenericEvent header;
        CARD32 payload[EXTERNAL_TEST_PAYLOAD_WORDS];
    } generic = { .header = { .type = GenericEvent,
                              .length = EXTERNAL_TEST_PAYLOAD_WORDS } };

    client->clientPtr = &test->keyboard;
    client->sequence = 1;
    event.u.u.type = PropertyNotify;
    WriteEventsToClient(client, 1, &event);
    assert(test->fixture.events == 1 && test->fixture.length == sizeof(event));
    assert(((xEvent *) test->fixture.output)->u.u.sequenceNumber ==
           client->sequence);
    generic.payload[EXTERNAL_TEST_PAYLOAD_WORDS - 1] =
        EXTERNAL_TEST_PAYLOAD_SENTINEL;
    WriteEventsToClient(client, 1, (xEvent *) &generic);
    assert(test->fixture.events == 2 &&
           test->fixture.length == sizeof(generic));
    assert(memcmp(test->fixture.output, &generic, sizeof(generic)) == 0);
    assert(WriteToClient(client, sizeof(event), &event) == sizeof(event));
    assert(test->fixture.events == 2 && test->fixture.length == sizeof(event));
    assert(!InsertFakeRequest(client, (char *) &event, sizeof(event)));
    assert(ReadRequestFromClient(client) == 0);
}

static void
external_test_fds(struct external_test *test, ClientPtr client)
{
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);

    assert(fd >= 0);
    assert(WriteFdToClient(client, fd, FALSE) == 0);
    assert(test->fixture.fd >= 0 && test->fixture.fd != fd);
    assert(fcntl(test->fixture.fd, F_GETFD) & FD_CLOEXEC);
    close(test->fixture.fd);
    test->fixture.fd = -1;
    assert(fcntl(fd, F_GETFD) >= 0);

    test->fixture.fail_fd = TRUE;
    assert(WriteFdToClient(client, fd, TRUE) == -1);
    assert(fcntl(fd, F_GETFD) >= 0);
    test->fixture.fail_fd = FALSE;
    assert(WriteFdToClient(client, fd, TRUE) == 0);
    assert(test->fixture.fd == fd);
    close(test->fixture.fd);
    test->fixture.fd = -1;
}

static void
external_test_lifecycle(struct external_test *test, ClientPtr client)
{
    xEvent event = { .u.u.type = PropertyNotify };
    xResourceReq request = { 0 };
    ClientPtr replacement;
    XID id;

    test->fixture.fail_write = TRUE;
    WriteEventsToClient(client, 1, &event);
    assert(client->noClientException != Success);
    CloseDownClient(client);
    assert(test->fixture.closed == 1 && test->fixture.freed == 1);
    assert(test->fixture.states[ClientStateGone] == 1);
    assert(!clients[1] && currentMaxClients == 1);
    assert(dispatchException & DE_RESET);
    dispatchException = 0;

    test->fixture.freed = 0;
    test->fixture.fail_write = FALSE;
    replacement = dixCreateExternalClient(&external_test_callbacks,
                                          &test->credentials, &test->fixture);
    assert(replacement);
    id = FakeClientID(replacement->index);
    assert(AddResource(id, test->type, &test->fixture));
    request.id = id;
    request.length = bytes_to_int32(sizeof(request));
    test->server.requestBuffer = &request;
    test->server.req_len = request.length;
    assert(ProcKillClient(&test->server) == Success);
    assert(test->fixture.closed == 2);
}

static void
external_clients(void)
{
    struct external_test test;
    ClientPtr client;

    external_test_init(&test);
    client = external_test_client(&test);
    external_test_credentials(&test, client);
    external_test_client_state(client);
    external_test_resources(&test, client);
    external_test_events(&test, client);
    external_test_fds(&test, client);
    external_test_lifecycle(&test, client);
    external_test_fini(&test);
}

const testfunc_t *
external_test(void)
{
    static const testfunc_t tests[] = { external_clients, NULL };
    return tests;
}
