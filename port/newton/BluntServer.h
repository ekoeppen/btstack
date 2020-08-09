#pragma once

#include <UserTasks.h>
#include <SerialChipV2.h>
#include "Definitions.h"
#include "EventsCommands.h"
#include "btstack.h"
#include "btstack_state.h"

class BluntServer;

struct DiscoveredDevice {
    UByte fBdAddr[6];
    UByte fPageScanRepetitionMode;
    UByte fPageScanPeriodMode;
    UByte fPageScanMode;
    UByte fClass[3];
    UByte fName[249];
};

class BluntServer: public TUTaskWorld
{
public:
    static BluntServer      *New();

    btstack_state_t         *fStack;
    BluntServer*            fServer;
    Boolean                 fEnd;

    TUPort                  fPort;
    TUPort                  *fNewtPort;
    TUAsyncMessage          fIntMessage;
    TUAsyncMessage          fTimerMessage;
    TUAsyncMessage          fServerMessage;

    UByte                   fMessage[MAX_MESSAGE];

    // Device address

    UByte                   fBdAddr[6];

    UByte                   fName[65];

    // Discovery data

    DiscoveredDevice        *fDiscoveredDevices;
    Byte                    fNumDiscoveredDevices;
    Byte                    fCurrentDevice;

    // Services data

    ULong                   *fQueriedServices;
    Byte                    fCurrentService;
    Byte                    fNumQueriedServices;

    NewtonErr               Initialize(ULong location, ULong driver, ULong speed, Long logLevel);

    void                    HandleData();
    void                    HandleTimer();
    void                    HandleCommand(BluntCommand* command);

    void                    InquiryStart(BluntInquiryCommand* command);
    void                    InitiatePairing(BluntInitiatePairingCommand* command);
    void                    InitiateServiceRequest(BluntServiceRequestCommand* command);
    void                    SendData(BluntDataCommand* command);
    void                    Status();

    virtual ULong           GetSizeOf();

    virtual long            TaskConstructor();
    virtual void            TaskDestructor();
    virtual void            TaskMain();

    void                    Start();
    void                    Stop();

    void                    SetTimer(Handler *handler, int milliSecondDelay, void *userData = NULL);

    static TObjectId        Port(void);
};
