/*
** Poll_Once_At_Start: for test programs that time things, run from an Obey file as a task of
** their own (riscos/scripts, vcport-work). On the Risc PC (RISC OS 4.39, Kinetic StrongARM) on
** 2026-10-19 such a program ran from uncached pages until its first Wimp_Poll: about 28 times
** too slowly, though *Cache said on and the control register had both caches on. A program that
** has polled once stays fast, also after Wimp_CloseDown. So this becomes a Wimp task, polls for
** 1.5 s (long enough for the Rorth request that started it to have finished, as a task quitting
** while Rorth serves one can crash RISC OS 4), and closes down. Output redirection is off while
** it polls, so the desktop's redrawing doesn't go into the results. Nothing is done outside the
** desktop or in a TaskWindow (which has polled already), or with VC_NOPOLL set.
*/
#ifndef POLL_ONCE_H
#define POLL_ONCE_H
#ifdef __riscos__
#include <kernel.h>
#include <stdlib.h>
#include <swis.h>

static void Poll_Once_At_Start(void)
{
    _kernel_swi_regs r;
    if (getenv("VC_NOPOLL") != NULL) {
        return;
    }
    const char* state = getenv("Wimp$State");
    if (state == NULL || state[0] != 'd') {
        return;
    }
    if (_kernel_swi(TaskWindow_TaskInfo, (r.r[0] = 0, &r), &r) == NULL && r.r[0] != 0) {
        return;
    }
    static const int messages[] = {0};
    r.r[0] = 310;
    r.r[1] = 0x4B534154; /* "TASK" */
    r.r[2] = (int)"PollOnce";
    r.r[3] = (int)messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL) {
        return;
    }
    int handle = r.r[1];
    r.r[0] = -1;
    r.r[1] = -1;
    _kernel_swi(OS_ChangeRedirection, &r, &r); /* read the current redirection */
    int in = r.r[0], out = r.r[1];
    r.r[0] = 0;
    r.r[1] = 0;
    _kernel_swi(OS_ChangeRedirection, &r, &r);
    int block[64];
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    unsigned until = (unsigned)r.r[0] + 150;
    for (;;) {
        _kernel_swi(OS_ReadMonotonicTime, &r, &r);
        unsigned now = (unsigned)r.r[0];
        if ((int)(now - until) >= 0) {
            break;
        }
        r.r[0] = 0;
        r.r[1] = (int)block;
        r.r[2] = (int)(now + 5);
        if (_kernel_swi(Wimp_PollIdle, &r, &r) != NULL) {
            break;
        }
        if ((r.r[0] == 17 || r.r[0] == 18) && block[4] == 0) { /* Message_Quit: just stop polling */
            break;
        }
    }
    r.r[0] = handle;
    r.r[1] = 0x4B534154;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    r.r[0] = in;
    r.r[1] = out;
    _kernel_swi(OS_ChangeRedirection, &r, &r);
}
#else
static void Poll_Once_At_Start(void)
{
}
#endif
#endif
