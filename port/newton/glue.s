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
