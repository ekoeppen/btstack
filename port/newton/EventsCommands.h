#pragma once

#include <NewtonTime.h>
#include <AEvents.h>
#include <UserPorts.h>
#include "Definitions.h"

class BluntServer;

enum BluntEventType {
    E_BLUNT_EVENT,                      // 0
    E_GENERIC_EVENT,
    E_INQUIRY_RESULT,
    E_NAME_REQUEST_RESULT,
    E_COMMAND_COMPLETE,
    E_CONNECTION_COMPLETE,              // 5
    E_DISCONNECT_COMPLETE,
    E_RESET_COMPLETE,
    E_TIMER_EVENT,
    E_LINK_KEY_NOTIFICATION,
    E_SERVICE_RESULT,                   // 10
    E_TIMER,
    E_DATA,
    E_DATA_SENT,
    E_STATUS
};

enum ConnectionLayer {
    L_HCI,
    L_L2CAP,
    L_SDP,
    L_RFCOMM,
    L_TCS
};

enum {
    M_DATA,
    M_COMMAND,
    M_EVENT,
    M_TIMER
};

class Handler;
class CBufferList;

// ================================================================================
// BluntEvent classes
// ================================================================================

class BluntEvent: public TAEvent, public TUAsyncMessage
{
public:
    BluntEventType  fType;
    BluntEvent*     fOriginalEvent;
    NewtonErr       fResult;
    Boolean         fDelete;

                    BluntEvent(BluntEventType type, NewtonErr result);
    virtual         ~BluntEvent();
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntTimerEvent: public BluntEvent
{
public:
    Handler*        fHandler;
    void*           fUserData;

                    BluntTimerEvent(NewtonErr result, Handler *handler, void *userData):
                        BluntEvent(E_TIMER_EVENT, result), fHandler(handler), fUserData(userData) {}
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntResetCompleteEvent: public BluntEvent
{
public:
                    BluntResetCompleteEvent(NewtonErr result): BluntEvent(E_RESET_COMPLETE, result) {}
};

class BluntInquiryResultEvent: public BluntEvent
{
public:
    UByte           fBdAddr[6];
    UByte           fPSRepMode;
    UByte           fPSPeriodMode;
    UByte           fPSMode;
    UByte           fClass[3];
    UByte           fClockOffset[2];

                    BluntInquiryResultEvent(NewtonErr result);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntNameRequestResultEvent: public BluntEvent
{
public:
    UChar           fName[64];
    UByte           fBdAddr[6];

                    BluntNameRequestResultEvent(NewtonErr result, UChar* name, UByte* addr);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntLinkKeyNotificationEvent: public BluntEvent
{
public:
    UByte           fBdAddr[6];
    UByte           fLinkKey[16];

                    BluntLinkKeyNotificationEvent(NewtonErr result, UByte* addr, UByte* key);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntServiceResultEvent: public BluntEvent
{
public:
    UByte           fBdAddr[6];
    ULong           fServiceUUID;
    Byte            fServicePort;

                    BluntServiceResultEvent(NewtonErr result): BluntEvent(E_SERVICE_RESULT, result) {}
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntCommandCompleteEvent: public BluntEvent
{
public:
    ULong           fStatus;

                    BluntCommandCompleteEvent(NewtonErr result, ULong s);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntConnectionCompleteEvent: public BluntEvent
{
public:
    Short           fHCIHandle;
    Byte            fL2CAPLocalCID;
    Byte            fL2CAPRemoteCID;
    Byte            fRFCOMMPort;
    Handler         *fHandler;

                    BluntConnectionCompleteEvent(NewtonErr result);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntDisconnectCompleteEvent: public BluntEvent
{
public:
                    BluntDisconnectCompleteEvent(NewtonErr result);
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntDataEvent: public BluntEvent
{
public:
    UByte           *fData;
    Long            fLength;
    Boolean         fDelete;
    Handler         *fHandler;

                    BluntDataEvent(NewtonErr result, UByte *data, Long len, Handler *handler):
                        BluntEvent(E_DATA, result), fDelete(false), fData(data), fLength(len), fHandler(handler) {}
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

class BluntDataSentEvent: public BluntEvent
{
public:
    Long            fAmount;

                    BluntDataSentEvent(NewtonErr result, Long len):
                        BluntEvent(E_DATA_SENT, result), fAmount(len) {}
    virtual ULong   GetSizeOf(void) { return sizeof(*this); }
};

// ================================================================================
// BluntCommand classes
// ================================================================================

class BluntCommand
{
public:
    BluntCommand*       fOriginalCommand;
    Boolean             fDelete;

                        BluntCommand();
    virtual             ~BluntCommand() { }
    virtual void        Process(BluntServer *server) { };
};

class BluntStartCommand: public BluntCommand
{
public:
    virtual void        Process(BluntServer *server) override;
};

class BluntStopCommand: public BluntCommand
{
public:
    virtual void        Process(BluntServer *server) override;
};

class BluntResetCommand: public BluntCommand
{
public:
    Char                fName[64];
    virtual void        Process(BluntServer *server) override;
};

class BluntConnectionCommand: public BluntCommand
{
public:
    TObjectId           fToolPort;
    ConnectionLayer     fTargetLayer;

    // HCI Data
    UByte               fBdAddr[6];
    UByte               fPSRepMode;
    UByte               fPSMode;
    Char                fPINCode[16];
    UByte               fLinkKey[16];

    // L2CAP Data
    Short               fL2CAPProtocol;

    // RFCOMM Data
    UByte               fRFCOMMPort;

                        BluntConnectionCommand(): fPSRepMode(1), fPSMode(0) {}
    virtual void        Process(BluntServer *server) override;
};

class BluntInquiryCommand: public BluntCommand
{
public:
    UByte               fTime;
    UByte               fAmount;

    virtual void        Process(BluntServer *server) override;
};

class BluntInquiryCancelCommand: public BluntCommand
{
public:
    virtual void        Process(BluntServer *server) override;
};

class BluntDisconnectCommand: public BluntCommand
{
public:
    TObjectId           fToolPort;
    UByte               fBdAddr[6];

    virtual void        Process(BluntServer *server) override;
};

class BluntInitiatePairingCommand: public BluntCommand
{
public:
    UByte               fBdAddr[6];
    Char                fPIN[64];
    UByte               fPSRepMode;
    UByte               fPSMode;

    virtual void        Process(BluntServer *server) override;
};

class BluntServiceRequestCommand: public BluntCommand
{
public:
    UByte               fBdAddr[6];
    UByte               fPSRepMode;
    UByte               fPSMode;
    UByte               fLinkKey[16];

    virtual void        Process(BluntServer *server) override;
};

class BluntDataCommand: public BluntCommand
{
public:
    Short               fHCIHandle;
    Byte                fRFCOMMPort;
    CBufferList         *fData;

    virtual void        Process(BluntServer *server) override;
};
