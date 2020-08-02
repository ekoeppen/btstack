#include <NewtonGestalt.h>
#include <HALOptions.h>
#include <SerialChipRegistry.h>
#include <stdio.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_newton.h"
#include "btstack_uart_block_newton.h"
#include "hal_newton.h"
#include "hal_uart_newton.h"
#include "bluetooth_company_id.h"
#include "classic/btstack_link_key_db_static.h"
#include "log.h"
#include "BluntServer.h"

typedef long Ref;

const Ref   NILREF = 0x02;
const Ref   TRUEREF = 0x1A;
const Ref   FALSEREF = NILREF;

class TObjectIterator;
class RefVar;
typedef const RefVar& RefArg;

typedef Ref(*MapSlotsFunction)(RefArg tag, RefArg value, ULong anything);

extern Ref  MakeInt(long i);
extern Ref  MakeChar(unsigned char c);
extern Ref  MakeBoolean(int val);

extern Boolean IsInt(RefArg r);
extern Boolean IsChar(RefArg r);
extern Boolean IsPtr(RefArg r);
extern Boolean IsMagicPtr(RefArg r);
extern Boolean IsRealPtr(RefArg r);

extern long RefToInt(RefArg r);
extern UniChar RefToUniChar(RefArg r);

extern "C" void EnterFIQAtomic(void);
extern "C" void ExitFIQAtomic(void);
extern "C" void _EnterFIQAtomic(void);
extern "C" void _ExitFIQAtomicFast(void);
extern TUPort* GetNewtTaskPort(void);
extern "C" long LockStack(TULockStack* lockRef, ULong additionalSpace); // Lock the entire stack plus additionalSpace # of bytes
extern "C" long UnlockStack(TULockStack* lockRef);
extern "C" long LockHeapRange(VAddr start, VAddr end, Boolean wire);    // Lock range from start up to but not including end.  Wire will prevent v->p mappings from changing.
extern "C" long UnlockHeapRange(VAddr start, VAddr end);

#define HEARTBEAT_PERIOD_MS 1000

static hci_transport_config_uart_t config = {
    HCI_TRANSPORT_CONFIG_UART,
    115200,
    1000000,  // main baudrate
    1,        // flow control
    NULL,
};

void heartbeat_handler(btstack_state_t *btstack, btstack_timer_source_t *ts){
    einstein_here(90, __func__, __LINE__);
    /*
    btstack_run_loop_set_timer(btstack, ts, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(btstack, ts);
    */
    btstack->run_loop->exit = 1;
}

void packet_handler(btstack_state_t *btstack, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    if (packet_type != HCI_EVENT_PACKET) return;
    einstein_log(31, __func__, __LINE__, "%d", packet_type);
    switch (hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                einstein_here(31, __func__, __LINE__);
                TUAsyncMessage *message = new TUAsyncMessage();
                message->Init(false);
                TUPort serverPort(BluntServer::Port());
                serverPort.Send(message,(void *) NULL, 0,(TTimeout) kNoTimeout, NULL, M_EVENT);
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

extern ULong *__vt__BluntServer;

ULong BluntServer::GetSizeOf()
{
    return sizeof(BluntServer);
}

long BluntServer::TaskConstructor()
{
    einstein_here(90, __func__, __LINE__);
    TUNameServer nameServer;
    fPort.Init();
    nameServer.RegisterName("BluntServer", "TUPort", fPort.fId, 0);
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
    einstein_here(90, __func__, __LINE__);
    TUNameServer nameServer;
    nameServer.UnRegisterName("BluntServer", "TUPort");
}

void BluntServer::TaskMain()
{
    ULong type = 0;
    TUMsgToken token;
    ULong n;
    btstack_timer_source_t heartbeat;
    btstack_packet_callback_registration_t hci_event_callback_registration;

    fIntMessage.Init(false);
    fTimerMessage.Init(false);
    fStack->hal->int_message = fIntMessage.GetMsgId();
    fStack->hal->timer_message = fTimerMessage.GetMsgId();
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(fStack, &hci_event_callback_registration);
    btstack_run_loop_set_timer(fStack, &heartbeat, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(fStack, &heartbeat);
    heartbeat.process = &heartbeat_handler;
    fEnd = false;

    einstein_here(31, __func__, __LINE__);
    while (!fEnd) {
        n = MAX_MESSAGE;
        fPort.Receive(&n, fMessage, MAX_MESSAGE, &token, &type);
        einstein_log(31, __func__, __LINE__, "%d", type);
        switch (type) {
            case M_DATA:
                HandleData();
                break;
            case M_COMMAND:
                HandleCommand((BluntCommand *) fMessage);
                break;
            case M_EVENT:
                HandleEvent((BluntEvent*) fMessage);
//              delete(BluntTimerEvent*) fMessage;
                break;
            case M_TIMER:
                HandleTimer();
            default:
                break;
        }
    }
}

void BluntServer::HandleData()
{
    if (fStack->uart->block_received) {
        einstein_here(90, __func__, __LINE__);
        hal_uart_newton_process_received_data(fStack);
    }
}

void BluntServer::HandleTimer()
{
    btstack_run_loop_embedded_execute_once(fStack);
}

void BluntServer::HandleCommand(BluntCommand* command)
{
    einstein_here(31, __func__, __LINE__);
    command->Process(this);
    if (command->fDelete) delete command->fOriginalCommand;
}

void BluntServer::HandleEvent(BluntEvent *event)
{
    einstein_log(31, __func__, __LINE__, "%d", event->fType);
    switch (event->fType) {
        case E_TIMER:
            HandleTimer(reinterpret_cast<BluntTimerEvent*>(event));
            break;
        default:
            break;
    }
}

void BluntServer::HandleTimer(BluntTimerEvent *event)
{
    btstack_run_loop_embedded_execute_once(fStack);
    delete event->fOriginalEvent;
}

void BluntServer::SendData(BluntDataCommand* command)
{
    einstein_here(90, __func__, __LINE__);
}

void BluntServer::Start()
{
    einstein_here(90, __func__, __LINE__);
    hci_set_inquiry_mode(fStack, INQUIRY_MODE_STANDARD);
    hci_power_control(fStack, HCI_POWER_ON);
}

void BluntServer::Stop()
{
    einstein_here(90, __func__, __LINE__);
    fEnd = true;
}

void BluntServer::InquiryStart(BluntInquiryCommand* command)
{
    int r;
    einstein_here(90, __func__, __LINE__);
    gap_start_scan(fStack);
}

void BluntServer::InitiatePairing(BluntInitiatePairingCommand* command)
{
    einstein_here(90, __func__, __LINE__);
}

void BluntServer::InitiateServiceRequest(BluntServiceRequestCommand* command)
{
    einstein_here(90, __func__, __LINE__);
}

void BluntServer::SendEvent(BluntEvent* event)
{
    einstein_here(90, __func__, __LINE__);
    TUPort* port = GetNewtTaskPort();
    port->Send(event,(void *) event, event->GetSizeOf());
}

BluntServer *BluntServer::New()
{
    BluntServer *server = new BluntServer();
    *(ULong ***) server = &__vt__BluntServer;
    return server;
}

TObjectId BluntServer::Port()
{
    TUNameServer nameServer;
    ULong id;
    ULong spec;

    nameServer.Lookup("BluntServer", "TUPort", &id, &spec);
    return id;
}
