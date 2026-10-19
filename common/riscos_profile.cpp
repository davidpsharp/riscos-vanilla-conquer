/*
** A sampling profiler for real RISC OS hardware: see riscos_profile.h.
**
** How it works: a small handler on the IRQ processor vector (OS_ClaimProcessorVector)
** runs at every interrupt (the 100 Hz timer, vsync, sound DMA and the rest, all
** periodic), and when the interrupted code was this program's, in user mode, records
** its PC, its user-mode SP and LR, FP, the interrupt request bits (IOMD IRQ A and B,
** to pick out the 100 Hz timer if wanted) and up to 12 APCS frames (each frame's
** saved PC, which identifies the function owning it, and its saved LR, the return
** address into the caller), reading only words inside the application slot.
** Interrupts in the OS (SWIs, other handlers) and in other programs are only counted.
**
** The handler, its state and the samples live in a dynamic area of their own, which
** is always mapped, so the handler is safe whatever task is paged in; if the program
** dies without releasing the vector, the handler stays harmless (it only counts).
**
** A first version sampled from a thread at UnixLib's context switches; those only
** happen on the way out of a SWI, so it saw mostly the code just after SWIs.
**
** 26-bit RISC OS on IOMD machines (RISC OS 3.5-4.x: the Risc PC, A7000, RPCEmu), with the
** interrupt request bits, and 32-bit RISC OS 5 (a Raspberry Pi), without them.
*/
#ifdef __riscos__
#include "riscos_profile.h"

#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <swis.h>

extern "C" {
extern char Profile_Irq_Start[], Profile_Irq_End[], Profile_Irq32_Start[], Profile_Irq32_End[];
extern char __executable_start[], etext[];
}
extern volatile unsigned Profile_Game_Frames; // phasetime.cpp
extern "C" {
}

/*
** The handler. Copied to the start of the dynamic area; the state follows at +0x200
** and the samples at +0x400, 128 bytes each:
**   0 pc, 1 user sp, 2 user lr, 3 fp, 4 IRQ request bits (A | B << 8), 5-28 frames,
**   29 game frames begun (Phase_Frame_Begin), 31 frame count
** State words: 0 previous handler, 1 enabled, 2-3 our text, 4-5 the application slot,
**   6 samples, 7 room for, 8 in the OS, 9 in other code, 10 dropped when full,
**   11-13 this program's AIF header words at &8014, &8018 and &8020 (its sizes), to tell it
**   from another task paged in at the same addresses, 14 the address of Profile_Game_Frames.
*/
#define PROFILE_IRQ_HANDLER(START, END, MODE_AND_PC, IRQ_BITS)                                       \
    asm(".text\n" \
    ".align 5\n" \
    ".global " START "\n" START ":\n" \
    "stmfd r13!, {r0-r5, r12, lr}\n" \
    "sub r12, pc, #12\n" /* the handler's own address (pc is 8 ahead; this is the 2nd instruction) */ \
    "add r12, r12, #0x200\n" \
    "ldr r0, [r12, #4]\n" \
    "teq r0, #0\n" \
    "beq 90f\n" \
    MODE_AND_PC /* to 80 if not user mode; r1 = the interrupted instruction */ \
    "ldr r2, [r12, #8]\n" \
    "cmp r1, r2\n" \
    "bcc 81f\n" \
    "ldr r2, [r12, #12]\n" \
    "cmp r1, r2\n" \
    "bcs 81f\n" \
    "mov r0, #0x8000\n" /* is it this program? its AIF header's sizes at &8014-&8020 */ \
    "ldr r2, [r0, #0x14]\n" \
    "ldr r3, [r12, #44]\n" \
    "teq r2, r3\n" \
    "ldreq r2, [r0, #0x18]\n" \
    "ldreq r3, [r12, #48]\n" \
    "teqeq r2, r3\n" \
    "ldreq r2, [r0, #0x20]\n" \
    "ldreq r3, [r12, #52]\n" \
    "teqeq r2, r3\n" \
    "bne 81f\n" \
    "ldr r0, [r12, #24]\n" \
    "ldr r2, [r12, #28]\n" \
    "cmp r0, r2\n" \
    "bcs 82f\n" \
    "add r2, r12, #0x200\n" \
    "add r2, r2, r0, lsl #7\n" \
    "add r0, r0, #1\n" \
    "str r0, [r12, #24]\n" \
    "str r1, [r2]\n" \
    "add r0, r2, #4\n" \
    "stmia r0, {r13, r14}^\n" /* the user-mode SP and LR */ \
    "mov r0, r0\n" \
    "str r11, [r2, #12]\n" \
    IRQ_BITS /* r3 */ \
    "str r3, [r2, #16]\n" \
    "ldr r0, [r12, #56]\n" /* the game frames begun so far */ \
    "ldr r0, [r0]\n" \
    "str r0, [r2, #116]\n" \
    "ldr r4, [r12, #16]\n" \
    "ldr r5, [r12, #20]\n" \
    "mov r0, r11\n" \
    "add r1, r2, #20\n" \
    "mov r3, #0\n" \
    "1:\n" \
    "tst r0, #3\n" \
    "bne 2f\n" \
    "cmp r0, r4\n" /* fp must be at least the slot start + 12 (so never 0) */ \
    "bcc 2f\n" \
    "cmp r0, r5\n" \
    "bcs 2f\n" \
    "ldr lr, [r0]\n" \
    "str lr, [r1], #4\n" \
    "ldr lr, [r0, #-4]\n" \
    "str lr, [r1], #4\n" \
    "ldr r0, [r0, #-12]\n" \
    "add r3, r3, #1\n" \
    "cmp r3, #12\n" \
    "bcc 1b\n" \
    "2:\n" \
    "str r3, [r2, #124]\n" \
    "b 90f\n" \
    "80:\n" \
    "ldr r0, [r12, #32]\n" \
    "add r0, r0, #1\n" \
    "str r0, [r12, #32]\n" \
    "b 90f\n" \
    "81:\n" \
    "ldr r0, [r12, #36]\n" \
    "add r0, r0, #1\n" \
    "str r0, [r12, #36]\n" \
    "b 90f\n" \
    "82:\n" \
    "ldr r0, [r12, #40]\n" \
    "add r0, r0, #1\n" \
    "str r0, [r12, #40]\n" \
    "90:\n" \
    "ldr r0, [r12, #0]\n" \
    "str r0, [r13, #-4]\n" /* the previous handler, just below the saved registers */ \
    "ldmfd r13!, {r0-r5, r12, lr}\n" \
    "ldr pc, [r13, #-36]\n" /* ...which is now 9 words down */ \
    ".global " END "\n" END ":\n")

// 26-bit RISC OS 3.5-4.x (IOMD): the mode is in LR's low bits; IOMD's IRQ request bits.
PROFILE_IRQ_HANDLER("Profile_Irq_Start", "Profile_Irq_End",
                    "tst lr, #3\n"
                    "bne 80f\n"
                    "bic r1, lr, #0xFC000003\n"
                    "sub r1, r1, #4\n",
                    "mov r0, #0x03200000\n" // IOMD
                    "ldrb r3, [r0, #0x14]\n" // IRQRQA
                    "ldrb r4, [r0, #0x24]\n" // IRQRQB
                    "orr r3, r3, r4, lsl #8\n");
// 32-bit RISC OS 5: the mode is in SPSR; no IRQ request bits.
PROFILE_IRQ_HANDLER("Profile_Irq32_Start", "Profile_Irq32_End",
                    "mrs r0, spsr\n"
                    "and r0, r0, #0x1f\n"
                    "teq r0, #0x10\n"
                    "bne 80f\n"
                    "sub r1, lr, #4\n",
                    "mov r3, #0\n");


namespace {

const unsigned WORDS = 40; // per sample in the file (as vcprof.py reads it)
const unsigned STATE = 0x200 / 4, SAMPLES = 0x400 / 4;
unsigned* Area = nullptr; // the dynamic area
int Area_Number = 0;
unsigned Max_Samples = 40000;
const char* Path = nullptr;
bool Claimed = false;

bool Swi(int number, _kernel_swi_regs& r)
{
    return _kernel_swi(number, &r, &r) == nullptr;
}

void Stop_And_Write()
{
    if (Area == nullptr) {
        return;
    }
    Area[STATE + 1] = 0; // stop recording
    if (Claimed) {
        _kernel_swi_regs r;
        r.r[0] = 6; // release the IRQ vector
        r.r[1] = int(Area[STATE + 0]);
        r.r[2] = reinterpret_cast<int>(Area);
        Claimed = !Swi(OS_ClaimProcessorVector, r);
        if (Claimed) {
            fprintf(stderr, "profile: couldn't release the IRQ vector; leaving the handler in place\n");
        }
    }
    unsigned n = Area[STATE + 6];
    FILE* f = fopen(Path, "wb");
    if (f != nullptr) {
        unsigned header[8] = {0x32504356, WORDS, n, 0, 12, Area[STATE + 8], Area[STATE + 9], Area[STATE + 10]};
        fwrite(header, sizeof(header), 1, f);
        static unsigned s[WORDS];
        for (unsigned i = 0; i < n; ++i) {
            const unsigned* a = Area + SAMPLES + i * 32;
            memset(s, 0, sizeof(s));
            s[0] = i;          // order
            s[1] = 1;          // weight
            s[2] = 0;          // thread (unknown here)
            s[3] = a[0];       // pc
            s[4] = a[2];       // lr
            s[5] = a[1];       // sp
            s[6] = a[3];       // fp
            s[7] = a[31];      // frames
            for (unsigned k = 0; k < 24; ++k) {
                s[8 + k] = a[5 + k];
            }
            s[38] = a[29]; // game frames begun
            s[39] = a[4];  // IRQ request bits
            fwrite(s, sizeof(s), 1, f);
        }
        fclose(f);
        fprintf(stderr,
                "profile: %u samples written to %s (and %u interrupts in the OS, %u in other programs, %u after it was full)\n",
                n,
                Path,
                Area[STATE + 8],
                Area[STATE + 9],
                Area[STATE + 10]);
    } else {
        fprintf(stderr, "profile: can't write %s\n", Path);
    }
    fflush(stderr);
    if (!Claimed) {
        _kernel_swi_regs r;
        r.r[0] = 1;
        r.r[1] = Area_Number;
        Swi(OS_DynamicArea, r);
    }
    Area = nullptr;
}

void Write_At_Exit()
{
    Stop_And_Write();
}

} // namespace

/*
** A run that died without its exit handlers (a fatal signal) leaves the handler on the
** vector and its dynamic area behind. Find any such area by name, disable it, put the
** vector back and delete it. Returns how many were removed. Also riscos/tests/profoff.
*/
int Profile_Release_Stale()
{
    int removed = 0;
    _kernel_swi_regs r;
    int number = -1;
    for (;;) {
        r.r[0] = 3; // enumerate
        r.r[1] = number;
        if (!Swi(OS_DynamicArea, r) || r.r[1] == -1) {
            break;
        }
        number = r.r[1];
        _kernel_swi_regs info;
        info.r[0] = 2; // read its details: r3 base, r8 name
        info.r[1] = number;
        if (!Swi(OS_DynamicArea, info) || info.r[8] == 0 || strcmp(reinterpret_cast<const char*>(info.r[8]), "VC profile") != 0) {
            continue;
        }
        unsigned* area = reinterpret_cast<unsigned*>(info.r[3]);
        if (area == Area) {
            continue; // this run's own
        }
        area[STATE + 1] = 0; // stop it recording at once
        _kernel_swi_regs v;
        v.r[0] = 6; // release the IRQ vector
        v.r[1] = int(area[STATE + 0]);
        v.r[2] = reinterpret_cast<int>(area);
        if (!Swi(OS_ClaimProcessorVector, v)) {
            fprintf(stderr, "profile: an old handler at %p couldn't be released; left disabled\n", (void*)area);
            continue;
        }
        _kernel_swi_regs d;
        d.r[0] = 1;
        d.r[1] = number;
        Swi(OS_DynamicArea, d);
        ++removed;
        number = -1; // the list has changed: start again
    }
    return removed;
}

void Profile_Register_Thread(int)
{
    // Every thread is covered: the handler sees whatever runs.
}

void Profile_Start()
{
    Path = getenv("VC_PROFILE");
    if (Path == nullptr || *Path == '\0' || Area != nullptr) {
        return;
    }
    Path = strdup(Path);
    if (int n = Profile_Release_Stale()) {
        fprintf(stderr, "profile: removed %d handler(s) left by an earlier run\n", n);
    }
    if (getenv("VC_PROFILE_MAX") != nullptr) {
        Max_Samples = unsigned(atoi(getenv("VC_PROFILE_MAX")));
    }
    // In a 32-bit mode, TEQ PC, PC sets Z; in 26-bit, the PC with the PSR differs.
    unsigned is32;
    asm volatile("mov %0, #0\n"
                 "teq %0, %0\n"
                 "teq pc, pc\n"
                 "moveq %0, #1\n"
                 : "=r"(is32)
                 :
                 : "cc");
    const char* start = is32 ? Profile_Irq32_Start : Profile_Irq_Start;
    const char* end = is32 ? Profile_Irq32_End : Profile_Irq_End;
    _kernel_swi_regs r;
    unsigned size = 0x400 + Max_Samples * 128;
    r.r[0] = 0;  // create
    r.r[1] = -1; // any number
    r.r[2] = int(size);
    r.r[3] = -1; // anywhere
    r.r[4] = 0x80; // user read/write, not draggable
    r.r[5] = int(size);
    r.r[6] = 0;
    r.r[7] = 0;
    r.r[8] = reinterpret_cast<int>("VC profile");
    if (!Swi(OS_DynamicArea, r)) {
        fprintf(stderr, "profile: no dynamic area for %u samples\n", Max_Samples);
        return;
    }
    Area_Number = r.r[1];
    Area = reinterpret_cast<unsigned*>(r.r[3]);
    memset(Area, 0, 0x400);
    if (end - start > 0x200) {
        fprintf(stderr, "profile: the handler is too big\n");
        return;
    }
    memcpy(Area, start, size_t(end - start));
    Area[STATE + 2] = reinterpret_cast<unsigned>(__executable_start);
    Area[STATE + 3] = reinterpret_cast<unsigned>(etext);
    r.r[0] = 0; // OS_GetEnv: r1 = the top of the application slot
    _kernel_swi(OS_GetEnv, &r, &r);
    Area[STATE + 4] = 0x8000 + 12; // the lowest frame pointer worth reading
    Area[STATE + 5] = unsigned(r.r[1]);
    Area[STATE + 7] = Max_Samples;
    const unsigned* aif = reinterpret_cast<const unsigned*>(0x8000);
    Area[STATE + 11] = aif[0x14 / 4];
    Area[STATE + 12] = aif[0x18 / 4];
    Area[STATE + 13] = aif[0x20 / 4];
    Area[STATE + 14] = reinterpret_cast<unsigned>(&Profile_Game_Frames);
    r.r[0] = 1; // OS_SynchroniseCodeAreas: the whole range
    r.r[1] = reinterpret_cast<int>(Area);
    r.r[2] = reinterpret_cast<int>(Area) + 0x200;
    Swi(OS_SynchroniseCodeAreas, r);
    r.r[0] = 6 | 0x100; // claim the IRQ vector
    r.r[1] = reinterpret_cast<int>(Area);
    if (!Swi(OS_ClaimProcessorVector, r)) {
        fprintf(stderr, "profile: can't claim the IRQ vector\n");
        r.r[0] = 1;
        r.r[1] = Area_Number;
        Swi(OS_DynamicArea, r);
        Area = nullptr;
        return;
    }
    Area[STATE + 0] = unsigned(r.r[1]); // the previous handler
    Claimed = true;
    atexit(Write_At_Exit);
    Area[STATE + 1] = 1;
    fprintf(stderr, "profile: sampling at interrupts into %s\n", Path);
}
#endif
