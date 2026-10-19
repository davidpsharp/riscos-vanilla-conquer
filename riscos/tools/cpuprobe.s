@ cpuprobe: sets VanillaConquer$LongMul to 1 if this CPU runs UMULL (ARMv3M:
@ StrongARM and later), or to 0 if it doesn't (ARM610, ARM710, ARM7500).
@ !Run uses it to pick the program to run.
@
@ A RISC OS absolute (filetype FF8), loaded and entered at &8000 in user mode,
@ for RISC OS 3.5 to 5, in 26 or 32 bit modes. Built with plain ARMv3
@ instructions, position independent, using no stack.
@
@ An ARMv3 CPU without the long multiplies need not trap UMULL: RPCEmu's
@ ARM610/710 (and perhaps the real chips) decode it as ADD Rd,Rn,Rm,LSL Rs.
@ So the result is checked, and an undefined instruction trap is caught too.
@ Either way the answer is 0 unless UMULL gives exactly the right result.
@
@ Not used at the moment: one ARMv3 !RunImage runs everywhere (riscos/README.md,
@ "ARMv3 CPUs"). It's for a release with two, where !Run would do:
@   Run <VanillaTD$Dir>.Utils.cpuprobe
@   If "<VanillaConquer$LongMul>"="1" Then Set VanillaTD$Image !RunImage Else Set VanillaTD$Image RunImage3
@ Build (in riscos/scripts/sdk.sh):
@   arm-unknown-riscos-as -march=armv3 riscos/tools/cpuprobe.s -o build/cpuprobe.o
@   arm-unknown-riscos-objcopy -O binary build/cpuprobe.o build/cpuprobe,ff8
@ Tested on RISC OS 3.11 (ARM3, Arculator), 3.71 (ARM710, RPCEmu), 4.39 (StrongARM,
@ RPCEmu) and 5.30 (Raspberry Pi 3); and with the UMULL replaced by an undefined
@ instruction, on 3.71, 4.39 and 5.30, to try the error handler.

        .text
        .arm
        .global _start
_start:
        @ Our own error handler, so an undefined instruction comes back here
        @ rather than ending !Run with an error.
        mov     r0, #6                  @ error handler
        adr     r1, on_error
        mov     r2, #0
        adr     r3, errbuf
        swi     0x40                    @ OS_ChangeEnvironment
        adr     r4, old
        stmia   r4, {r1-r3}

        mov     r0, #0
        mov     r1, #0
        mov     r2, #0x10000
        mov     r3, #0x10000
        .word   0xe0810392              @ umull r0, r1, r2, r3: r1:r0 = 0x1:0x00000000
        mov     r5, #0                  @ 0: no long multiply
        cmp     r1, #1
        cmpeq   r0, #0
        moveq   r5, #1
        b       done

on_error:
        mov     r5, #0                  @ it trapped: no long multiply
done:
        @ Put the old error handler back.
        mov     r0, #6
        adr     r4, old
        ldmia   r4, {r1-r3}
        swi     0x40                    @ OS_ChangeEnvironment

        adr     r0, varname
        adr     r1, value0
        add     r1, r1, r5              @ value1 follows value0
        add     r1, r1, r5
        mov     r2, #1
        mov     r3, #0
        mov     r4, #4                  @ literal string
        swi     0x20024                 @ XOS_SetVarVal

        mov     r0, #0
        mov     r1, #0
        mov     r2, #0
        swi     0x11                    @ OS_Exit

value0: .ascii  "0"
        .byte   0
value1: .ascii  "1"
        .byte   0
varname:
        .asciz  "VanillaConquer$LongMul"
        .align  2
old:    .word   0, 0, 0
errbuf: .space  260
