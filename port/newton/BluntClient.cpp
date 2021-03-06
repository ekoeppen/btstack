#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <Newton.h>
#include "NSandDDKIncludes.h"
#include "EventsCommands.h"
#include "log.h"
#include "BluntClient.h"

extern ULong *__vt__BluntClient;

BluntClient *BluntClient::New(RefArg blunt, TObjectId server)
{
    einstein_here (31, __func__, __LINE__);
    BluntClient *client = new BluntClient(blunt, server);
    *(ULong ***) client = &__vt__BluntClient;
    return client;
}

BluntClient::BluntClient (RefArg blunt, TObjectId server)
{
    fBlunt = blunt;
    einstein_log (31, __func__, __LINE__, "%08x %08x", blunt, (long)fBlunt);
    Init (kBluntEventId, kBluntEventClass);
    fServerPort = server;
}

BluntClient::~BluntClient ()
{
}

void BluntClient::AEHandlerProc (TUMsgToken* token, ULong* size, TAEvent* event)
{
    einstein_log (CYAN, __func__, __LINE__, "%d", ((BluntEvent*) event)->fType);
    switch (((BluntEvent*) event)->fType) {
        case E_RESET_COMPLETE:
            reinterpret_cast<BluntResetCompleteEvent*>(event)->Process(this);
            break;
        case E_INQUIRY_RESULT:
            SendInquiryInfo ((BluntInquiryResultEvent *) event);
            break;
        case E_NAME_REQUEST_RESULT:
            SendNameRequestInfo ((BluntNameRequestResultEvent*) event);
            break;
        case E_LINK_KEY_NOTIFICATION:
            SendLinkKeyInfo ((BluntLinkKeyNotificationEvent *) event);
            break;
        case E_SERVICE_RESULT:
            SendServiceInfo ((BluntServiceResultEvent *) event);
            break;
    }
}

void BluntClient::Stop ()
{
    BluntStopCommand command;
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::Reset (Char* name)
{
    BluntResetCommand command;
    strcpy (command.fName, name);
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::Discover (UByte time, UByte amount)
{
    BluntInquiryCommand command;
    command.fTime = time;
    command.fAmount = amount;
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::CancelDiscover ()
{
    BluntInquiryCancelCommand command;
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::Pair (UByte* bdAddr, Char *PIN, ULong psRepMode, ULong psMode)
{
    BluntInitiatePairingCommand command;
    memcpy (command.fBdAddr, bdAddr, 6);
    strcpy (command.fPIN, PIN);
    command.fPSRepMode = psRepMode;
    command.fPSMode = psMode;
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::GetServices (UByte* bdAddr, ULong psRepMode, ULong psMode, UByte* linkKey)
{
    BluntServiceRequestCommand command;
    memcpy (command.fBdAddr, bdAddr, 6);
    command.fPSRepMode = psRepMode;
    command.fPSMode = psMode;
    memcpy (command.fLinkKey, linkKey, sizeof (command.fLinkKey));
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::Connect (UByte* bdAddr, ULong psRepMode, ULong psMode, UByte rfcommPort, UByte* linkKey)
{
    BluntConnectionCommand command;
    command.fToolPort = 0;
    memcpy (command.fBdAddr, bdAddr, 6);
    memcpy (command.fLinkKey, linkKey, sizeof (command.fLinkKey));
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::Disconnect (UByte* bdAddr)
{
    BluntDisconnectCommand command;
    command.fToolPort = 0;
    memcpy (command.fBdAddr, bdAddr, 6);
    fServerPort.Send (&command, sizeof (command), kNoTimeout, M_COMMAND);
}

void BluntClient::SendInquiryInfo (BluntInquiryResultEvent* event)
{
    RefVar device;
    RefVar addr;

    einstein_log (31, __func__, __LINE__, "BluntClient::SendInquiryInfo %d\n", event->fResult);
    if (event->fResult == noErr) {
        device = AllocateFrame ();
        addr = AllocateBinary (SYM (binary), 6);
        WITH_LOCKED_BINARY(addr, a)
        memcpy (a, event->fBdAddr, 6);
        END_WITH_LOCKED_BINARY(addr);
        SetFrameSlot (device, SYM (fBdAddr), addr);
        SetFrameSlot (device, SYM (fClass), MAKEINT ((event->fClass[0] << 16) + (event->fClass[1] << 8) + event->fClass[2]));
        SetFrameSlot (device, SYM (fClockOffset), MAKEINT ((event->fClockOffset[0] << 8) + event->fClockOffset[1]));
        SetFrameSlot (device, SYM (fPSRepMode), MAKEINT (event->fPSRepMode));
        SetFrameSlot (device, SYM (fPSPeriodMode), MAKEINT (event->fPSPeriodMode));
        SetFrameSlot (device, SYM (fPSMode), MAKEINT (event->fPSMode));
        NSSendIfDefined (fBlunt, SYM (MInquiryCallback), device);
    } else {
        NSSendIfDefined (fBlunt, SYM (MInquiryCallback), NILREF);
    }
}

void BluntClient::SendNameRequestInfo (BluntNameRequestResultEvent* event)
{
    RefVar addr;

    einstein_log (31, __func__, __LINE__, "BluntClient::SendNameRequestInfo (%s)\n", event->fName);
    addr = AllocateBinary (SYM (binary), 6);
    WITH_LOCKED_BINARY(addr, a)
    memcpy (a, event->fBdAddr, 6);
    END_WITH_LOCKED_BINARY(addr);
    NSSendIfDefined (fBlunt, SYM (MNameRequestCallback), addr, MakeString ((char*) event->fName));
}

void BluntClient::SendLinkKeyInfo (BluntLinkKeyNotificationEvent* event)
{
    RefVar addr;
    RefVar key;

    einstein_log (31, __func__, __LINE__, "BluntClient::SendLinkKeyInfo\n");
    addr = AllocateBinary (SYM (binary), 6);
    key = AllocateBinary (SYM (binary), 16);
    WITH_LOCKED_BINARY(addr, a)
    memcpy (a, event->fBdAddr, 6);
    END_WITH_LOCKED_BINARY(addr);
    WITH_LOCKED_BINARY(key, k)
    memcpy (k, event->fLinkKey, 16);
    END_WITH_LOCKED_BINARY(key);
    NSSendIfDefined (fBlunt, SYM (MLinkKeyCallback), addr, key);
}

void BluntClient::SendServiceInfo (BluntServiceResultEvent* event)
{
    RefVar service;
    RefVar addr;

    einstein_log (31, __func__, __LINE__, "BluntClient::SendServiceInfo %d\n", event->fResult);
    service = AllocateFrame ();
    SetFrameSlot (service, SYM (fResult), MAKEINT (event->fResult));
    if (event->fResult == noErr) {
        addr = AllocateBinary (SYM (binary), 6);
        WITH_LOCKED_BINARY(addr, a)
        memcpy (a, event->fBdAddr, 6);
        END_WITH_LOCKED_BINARY(addr);
        SetFrameSlot (service, SYM (fBdAddr), addr);
        SetFrameSlot (service, SYM (fService), MAKEINT (event->fServiceUUID));
        SetFrameSlot (service, SYM (fPort), MAKEINT (event->fServicePort));
    }
    NSSendIfDefined (fBlunt, SYM (MServicesCallback), service);
}

void BluntClient::ResetComplete(NewtonErr result)
{
    einstein_log(CYAN, __func__, __LINE__, "%08x", fBlunt);
    NSSendIfDefined (fBlunt, SYM (MResetCallback));
}
