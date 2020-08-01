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

#include <NewtonScript.h>
#include <Objects.h>

#include <UserTasks.h>
#include "SerialChipRegistry.h"

#include "log.h"

extern "C" Ref MCreate(RefArg inRcvr)
{
    einstein_here(90, __func__, __LINE__);
    BluntServer *server = BluntServer::New();
    server->StartTask();
    return NILREF;
}

extern "C" Ref MStart(RefArg inRcvr)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntStartCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}

extern "C" Ref MInquiryStart(RefArg inRcvr)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntInquiryCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}

extern "C" Ref MStop(RefArg inRcvr)
{
    einstein_here(90, __func__, __LINE__);
    TUPort serverPort(BluntServer::Port());
    BluntStopCommand cmd;
    serverPort.Send (&cmd, sizeof(cmd), (TTimeout) kNoTimeout, M_COMMAND);
    return NILREF;
}
