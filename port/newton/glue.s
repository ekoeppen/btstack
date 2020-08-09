    .macro JT symbol, address
    .section ".text.\symbol\()", "ax"
    .global \symbol
\symbol\():
    .balign 4
    ldr pc, [pc, #-4]
    .word 0x\address
    .endm

    .arch armv4

    .global __vt__BluntServer
__vt__BluntServer:
    b _ZN11TUTaskWorldD1Ev
    b _ZN11BluntServer9GetSizeOfEv
    b _ZN11BluntServer15TaskConstructorEv
    b _ZN11BluntServer14TaskDestructorEv
    b _ZN11BluntServer8TaskMainEv

    .global __vt__BluntClient
__vt__BluntClient:
    b _ZN11BluntClientD1Ev
    b _ZN14TAEventHandler11AETestEventEP7TAEvent
    b _ZN11BluntClient13AEHandlerProcEP10TUMsgTokenPmP7TAEvent
    b _ZN14TAEventHandler16AECompletionProcEP10TUMsgTokenPmP7TAEvent
    b _ZN14TAEventHandler8IdleProcEP10TUMsgTokenPmP7TAEvent
