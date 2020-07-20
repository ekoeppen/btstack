#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg_nwt.h>

#include "btstack_config.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_newton.h"
#include "btstack_uart_block_newton.h"
#include "hal_newton.h"
#include "bluetooth_company_id.h"
#include "classic/btstack_link_key_db_static.h"
#include "hci.h"
#include "hci_dump.h"
#include "log.h"

#include <NewtonScript.h>
#include <Objects.h>

#include <UserTasks.h>
#include "SerialChipRegistry.h"

#include "log.h"

extern "C" int main(void);

static const int MAX_MESSAGE = 512;

class BluntServer: public TUTaskWorld
{
public:
    UByte                   fMessage[MAX_MESSAGE];
    TUPort                  fPort;
    TUAsyncMessage          fIntMessage;
    btstack_state_t         *fStack;

    virtual ULong           GetSizeOf ();

    virtual long            TaskConstructor ();
    virtual void            TaskDestructor ();
    virtual void            TaskMain ();

    static TObjectId        Port (void);
    static BluntServer      *New();
};

static hci_transport_config_uart_t config = {
    HCI_TRANSPORT_CONFIG_UART,
    115200,
    1000000,  // main baudrate
    1,        // flow control
    NULL,
};

void LOG(int color, const char *func, int line, const char *fmt, ...)
{
    char buffer[120];
    va_list args;
    sprintf(buffer, "\x1b[%dm%s %d\x1b[0m ", color, func, line);
    va_start(args, fmt);
    vsprintf(buffer + strlen(buffer), fmt, args);
    nwt_log(buffer);
    va_end(args);
}

#define HEARTBEAT_PERIOD_MS 1000

void heartbeat_handler(btstack_state_t *btstack, btstack_timer_source_t *ts){
    LH(__func__, __LINE__, 0);
    /*
    btstack_run_loop_set_timer(btstack, ts, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(btstack, ts);
    */
    btstack->run_loop->exit = 1;
}

void packet_handler (btstack_state_t *btstack, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    if (packet_type != HCI_EVENT_PACKET) return;
    LHC(31, __func__, __LINE__, packet_type);
    switch(hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                LHC(31, __func__, __LINE__, 0);
                TUAsyncMessage *message = new TUAsyncMessage();
                message->Init(false);
                TUPort serverPort(BluntServer::Port());
                serverPort.Send (message, (void *) NULL, 0, (TTimeout) kNoTimeout, NULL, M_HCI_UP);
            }
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information)){
                uint16_t manufacturer   = little_endian_read_16(packet, 10);
                uint16_t lmp_subversion = little_endian_read_16(packet, 12);
            }
            break;
        default:
            break;
    }
}

extern ULong __vt__BluntServer[5];

ULong BluntServer::GetSizeOf()
{
    return sizeof(BluntServer);
}

long BluntServer::TaskConstructor()
{
    LH(__func__, __LINE__, 0);
    TUNameServer nameServer;
    fPort.Init ();
    nameServer.RegisterName ("BluntServer", "TUPort", fPort.fId, 0);
    fStack = static_cast<btstack_state_t *>(calloc(1, sizeof(btstack_state_t)));
    btstack_hal_init(fStack);
    fStack->hal->server_port = fPort.fId;
    btstack_run_loop_init(fStack, btstack_run_loop_embedded_get_instance());
    const hci_transport_t *transport = hci_transport_h4_instance(fStack, btstack_uart_block_newton_instance());

    hci_init(fStack, transport, &config);
    hci_set_link_key_db(fStack, btstack_link_key_db_static_instance());

    return noErr;
}

void BluntServer::TaskDestructor()
{
    LH(__func__, __LINE__, 0);
    TUNameServer nameServer;
    nameServer.UnRegisterName ("BluntServer", "TUPort");
}

void BluntServer::TaskMain()
{
    ULong type = 0;
    Boolean end = false;
    TUMsgToken token;
    ULong n;
    btstack_timer_source_t heartbeat;
    btstack_packet_callback_registration_t hci_event_callback_registration;
    long r;

    LH(__func__, __LINE__, 0);
    fIntMessage.Init(false);
    fStack->hal->int_message = fIntMessage.GetMsgId();
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(fStack, &hci_event_callback_registration);
    btstack_run_loop_set_timer(fStack, &heartbeat, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(fStack, &heartbeat);
    heartbeat.process = &heartbeat_handler;
    LH(__func__, __LINE__, 0);
    while (!end) {
        n = MAX_MESSAGE;
        r = fPort.Receive (&n, fMessage, MAX_MESSAGE, &token, &type);
        if (r == noErr) {
            LH(__func__, __LINE__, type);
            switch (type) {
                case M_START:
                    hci_power_control(fStack, HCI_POWER_ON);
                    break;
                case M_STOP:
                    end = true;
                    break;
                case M_DATA_RECEIVED:
                    if (fStack->uart->block_received) {
                        LH(__func__, __LINE__, type);
                        fStack->uart->block_received(fStack);
                        btstack_run_loop_embedded_execute_once(fStack);
                    }
                    break;
                case M_HCI_UP:
                    break;
                case M_TIMER:
                    btstack_run_loop_embedded_execute_once(fStack);
                    break;
            }
        }
    }
}

BluntServer *BluntServer::New()
{
    BluntServer *server = new BluntServer();
    *(ULong *) server = &__vt__BluntServer;
    return server;
}

TObjectId BluntServer::Port()
{
    TUNameServer nameServer;
    ULong id;
    ULong spec;

    nameServer.Lookup ("BluntServer", "TUPort", &id, &spec);
    return id;
}

extern "C" Ref MCreate(RefArg inRcvr)
{
    LH(__func__, __LINE__, 0);
    BluntServer *server = BluntServer::New();
    server->StartTask();
    return NILREF;
}

extern "C" Ref MStart(RefArg inRcvr)
{
    LH(__func__, __LINE__, 0);
    TUPort serverPort(BluntServer::Port());
    serverPort.Send ((void *) NULL, 0, (TTimeout) kNoTimeout, M_START);
    return NILREF;
}

extern "C" Ref MStop(RefArg inRcvr)
{
    LH(__func__, __LINE__, 0);
    TUPort serverPort(BluntServer::Port());
    serverPort.Send ((void *) NULL, 0, (TTimeout) kNoTimeout, M_STOP);
    return NILREF;
}
