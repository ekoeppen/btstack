#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
#include "BluntServer.h"
#include "BluntClient.h"

#include <NewtonScript.h>
#include <Objects.h>

#include <UserTasks.h>
#include "SerialChipRegistry.h"

#include "log.h"

extern "C" Ref MCreate(RefArg inRcvr)
{
    return NILREF;
    einstein_here(90, __func__, __LINE__);
    BluntServer *server = BluntServer::New();
    BluntClient *client = BluntClient::New(inRcvr, BluntServer::Port());
    server->StartTask();
    return NILREF;
}

extern "C" Ref MStart (RefArg rcvr /*, RefArg location, RefArg driver, RefArg speed, RefArg logLevel*/)
{
    einstein_log(90, __func__, __LINE__, "%08x %08x", static_cast<long>(rcvr));
    UChar* pc;
    ULong l;

    /*
    WITH_LOCKED_BINARY(location, p);
    pc = (UChar *) p; l = (pc[1] << 24) + (pc[3] << 16) + (pc[5] << 8) + pc[7];
    END_WITH_LOCKED_BINARY(location);
    */

    BluntServer *server = BluntServer::New();
    BluntClient *client = BluntClient::New(rcvr, BluntServer::Port());
    client->ResetComplete(noErr);
    // server->Initialize (l, RINT (driver), RINT (speed), RINT (logLevel));
    server->StartTask();
    TUPort serverPort(BluntServer::Port());
    BluntStartCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    SetFrameSlot (rcvr, SYM (server), MAKEINT ((ULong) server));
    SetFrameSlot (rcvr, SYM (client), MAKEINT ((ULong) client));
    return TRUEREF;
}

extern "C" Ref MInquiryStart(RefArg rcvr, RefArg time, RefArg amount)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntInquiryCommand cmd;
    cmd.fTime = RINT(time);
    cmd.fAmount = RINT(amount);
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}

extern "C" Ref MInquiryCancel(RefArg rcvr)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntInquiryCancelCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}

extern "C" Ref MStop(RefArg rcvr)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntStopCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}
