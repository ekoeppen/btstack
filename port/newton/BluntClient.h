#pragma once

#include <NewtonTime.h>
#include <AEvents.h>
#include <AEventHandler.h>
#include <UserTasks.h>
#include <NewtonScript.h>

class BluntClient: public TAEventHandler
{
public:
                        BluntClient (RefArg blunt, TObjectId serverPort);
                        ~BluntClient ();

    static BluntClient* New(RefArg blunt, TObjectId server);

    void                AEHandlerProc (TUMsgToken* token, ULong* size, TAEvent* event) override;

    void                Reset (Char* name);
    void                Discover (UByte time, UByte amount);
    void                CancelDiscover ();
    void                NameRequest (UByte* bdAddr, ULong psRepMode, ULong psMode);
    void                Pair (UByte* bdAddr, Char *PIN, ULong psRepMode, ULong psMode);
    void                GetServices (UByte* bdAddr, ULong psRepMode, ULong psMode, UByte* linkKey);
    void                Stop ();
    void                Connect (UByte* bdAddr, ULong psRepMode, ULong psMode, UByte rfcommPort, UByte* linkKey);
    void                Disconnect (UByte* bdAddr);

    void                SendInquiryInfo (BluntInquiryResultEvent* event);
    void                SendNameRequestInfo (BluntNameRequestResultEvent* event);
    void                SendLinkKeyInfo (BluntLinkKeyNotificationEvent* event);
    void                SendServiceInfo (BluntServiceResultEvent* event);

    void                ResetComplete(NewtonErr result);

    RefStruct           fBlunt;
    TUPort              fServerPort;
};
