    .arch armv3

    .macro JT symbol, address
    .section ".text.\symbol\()", "ax"
    .global \symbol
\symbol\():
    .balign 4
    ldr pc, [pc, #-4]
    .word 0x\address
    .endm

    .macro D0DtorGlue symbol, address
    .section ".text.\symbol\()", "ax"
    .global \symbol
\symbol\():
    .balign 4
    mov r1, #1
    ldr pc, [pc, #-4]
    .word 0x\address
    .endm

    .macro D1DtorGlue symbol, address
    .section ".text.\symbol\()", "ax"
    .global \symbol
\symbol\():
    .balign 4
    mov r1, #0
    ldr pc, [pc, #-4]
    .word 0x\address
    .endm

    .macro D2DtorGlue symbol, address
    D1DtorGlue \symbol\(), \address\()
    .endm

    .macro DtorGlue symbol, address
    D0DtorGlue \symbol\()D0Ev, \address\()
    D1DtorGlue \symbol\()D1Ev, \address\()
    D2DtorGlue \symbol\()D2Ev, \address\()
    .endm

    .macro StaticNew class
    .section ".text.New__\class\()Fv", "ax"
    .global New__\class\()Fv
New__\class\()Fv:
    .balign 4
    mov r0, #0
    b __ct__\class\()Fv
    .endm

    JT _ZN11TSerialChip3NewEPc, 00384B0C
    JT _ZN19PSerialChipRegistry8RegisterEP11TSerialChipm, 00384E88
    JT _ZN19PSerialChipRegistry10GetChipPtrEm, 00384EAC
    JT _ZN19PSerialChipRegistry14FindByLocationEm, 00384EDC
    JT _ZN11TSerialChip7PutByteEh, 00384B6C
    JT _ZN11TSerialChip13ProcessOptionEP7TOption, 00384C8C
    JT _ZN7TOptionC2Em, 0014AA38
