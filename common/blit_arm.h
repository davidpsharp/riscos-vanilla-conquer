/*
** Hand-written ARM versions of the hottest transparent shape blitters (keybuff.cpp),
** for RISC OS. They do exactly what the C versions do, for count == 1 where fading
** is involved:
**   Trans:              d = s
**   Ghost_Trans:        f = lookup[s]; d = f == 0xFF ? s : ghost[d + f * 256]
**   Fading_Trans:       d = fade[s]
**   Ghost_Fading_Trans: as Ghost_Trans, then d = fade[d']
** skipping source pixels of 0, and four of them at a time once the source is word
** aligned. ARMv3 instructions only (no halfword loads or stores: the Risc PC's
** memory system mishandles them), and r10 (sl) and r11 (fp) are left alone for the
** APCS and UnixLib's thread switching.
**
** Arguments, as the BF_Function type: r0 width, r1 height, r2 dst, r3 src, then on
** the stack dst_pitch, src_pitch, ghost_lookup, ghost_tab, fade_tab, count.
*/
#if defined(__riscos__) && defined(__arm__)

#define BLIT_ARM_AVAILABLE 1

extern "C" {
void Blit_Trans_ARM(int width, int height, unsigned char* dst, unsigned char* src, int dst_pitch, int src_pitch,
                    unsigned char* ghost_lookup, unsigned char* ghost_tab, unsigned char* fade_tab, int count);
void Blit_Ghost_Trans_ARM(int width, int height, unsigned char* dst, unsigned char* src, int dst_pitch, int src_pitch,
                          unsigned char* ghost_lookup, unsigned char* ghost_tab, unsigned char* fade_tab, int count);
void Blit_Fading_Trans_ARM(int width, int height, unsigned char* dst, unsigned char* src, int dst_pitch,
                           int src_pitch, unsigned char* ghost_lookup, unsigned char* ghost_tab,
                           unsigned char* fade_tab, int count);
void Blit_Ghost_Fading_Trans_ARM(int width, int height, unsigned char* dst, unsigned char* src, int dst_pitch,
                                 int src_pitch, unsigned char* ghost_lookup, unsigned char* ghost_tab,
                                 unsigned char* fade_tab, int count);
}

/*
** Registers in the routines:
**   r0 pixels left in the row   r1 rows left        r2 dst          r3 src
**   r4 ghost_lookup             r5 ghost_tab        r6 fade_tab     r7 dst_pitch
**   r8 src_pitch                r9 width            r12, lr scratch
** PIXEL(off) handles the source byte in r12 for dst[off]; it may use lr.
*/
#define BLIT_STR2(x) #x
#define BLIT_STR(x) BLIT_STR2(x)

#define BLIT_ROUTINE(NAME, PIXEL0, PIXEL1, PIXEL2, PIXEL3)                                                            \
    asm(".text\n"                                                                                                     \
        ".align 2\n"                                                                                                  \
        ".global " #NAME "\n" #NAME ":\n"                                                                              \
        "stmfd sp!, {r4-r9, lr}\n"                                                                                    \
        "ldr r7, [sp, #28]\n" /* dst_pitch (7 registers pushed) */                                                    \
        "ldr r8, [sp, #32]\n" /* src_pitch */                                                                         \
        "ldr r4, [sp, #36]\n" /* ghost_lookup */                                                                      \
        "ldr r5, [sp, #40]\n" /* ghost_tab */                                                                         \
        "ldr r6, [sp, #44]\n" /* fade_tab */                                                                          \
        "mov r9, r0\n"                                                                                                \
        "cmp r1, #0\n"                                                                                                \
        "ble 9f\n"                                                                                                    \
        "cmp r9, #0\n"                                                                                                \
        "ble 9f\n"                                                                                                    \
        "1:\n" /* each row */                                                                                         \
        "mov r0, r9\n"                                                                                                \
        "2:\n" /* single pixels until src is word aligned */                                                          \
        "tst r3, #3\n"                                                                                                \
        "beq 3f\n"                                                                                                    \
        "ldrb r12, [r3], #1\n"                                                                                        \
        "cmp r12, #0\n"                                                                                               \
        PIXEL0                                                                                                        \
        "add r2, r2, #1\n"                                                                                            \
        "subs r0, r0, #1\n"                                                                                           \
        "bgt 2b\n"                                                                                                    \
        "b 7f\n"                                                                                                      \
        "3:\n" /* four at a time */                                                                                   \
        "subs r0, r0, #4\n"                                                                                           \
        "blt 5f\n"                                                                                                    \
        "4:\n"                                                                                                        \
        "ldr r12, [r3], #4\n"                                                                                         \
        "cmp r12, #0\n"                                                                                               \
        "beq 41f\n"                                                                                                   \
        "ldrb r12, [r3, #-4]\n"                                                                                       \
        "cmp r12, #0\n"                                                                                               \
        PIXEL0                                                                                                        \
        "ldrb r12, [r3, #-3]\n"                                                                                       \
        "cmp r12, #0\n"                                                                                               \
        PIXEL1                                                                                                        \
        "ldrb r12, [r3, #-2]\n"                                                                                       \
        "cmp r12, #0\n"                                                                                               \
        PIXEL2                                                                                                        \
        "ldrb r12, [r3, #-1]\n"                                                                                       \
        "cmp r12, #0\n"                                                                                               \
        PIXEL3                                                                                                        \
        "41:\n"                                                                                                       \
        "add r2, r2, #4\n"                                                                                            \
        "subs r0, r0, #4\n"                                                                                           \
        "bge 4b\n"                                                                                                    \
        "5:\n" /* the last 0-3 */                                                                                     \
        "adds r0, r0, #4\n"                                                                                           \
        "beq 7f\n"                                                                                                    \
        "6:\n"                                                                                                        \
        "ldrb r12, [r3], #1\n"                                                                                        \
        "cmp r12, #0\n"                                                                                               \
        PIXEL0                                                                                                        \
        "add r2, r2, #1\n"                                                                                            \
        "subs r0, r0, #1\n"                                                                                           \
        "bgt 6b\n"                                                                                                    \
        "7:\n" /* next row */                                                                                         \
        "add r2, r2, r7\n"                                                                                            \
        "add r3, r3, r8\n"                                                                                            \
        "subs r1, r1, #1\n"                                                                                           \
        "bgt 1b\n"                                                                                                    \
        "9:\n"                                                                                                        \
        "ldmfd sp!, {r4-r9, pc}\n")

/* Trans: store if non-zero. */
#define TRANS_PIXEL(off) "strneb r12, [r2, #" #off "]\n"

/* Ghost_Trans: lr = lookup[s]; if lr != 0xFF, s = ghost[d + lr * 256]; store. */
#define GHOST_PIXEL(off)                                                                                              \
    "beq 10" #off "f\n"                                                                                               \
    "ldrb lr, [r4, r12]\n"                                                                                            \
    "cmp lr, #255\n"                                                                                                  \
    "ldrneb r12, [r2, #" #off "]\n"                                                                                   \
    "addne lr, r12, lr, lsl #8\n"                                                                                     \
    "ldrneb r12, [r5, lr]\n"                                                                                          \
    "strb r12, [r2, #" #off "]\n"                                                                                     \
    "10" #off ":\n"

/* Fading_Trans with count 1: store fade[s]. */
#define FADE_PIXEL(off)                                                                                               \
    "ldrneb r12, [r6, r12]\n"                                                                                         \
    "strneb r12, [r2, #" #off "]\n"

/* Ghost_Fading_Trans with count 1: as GHOST_PIXEL, then fade. */
#define GHOST_FADE_PIXEL(off)                                                                                         \
    "beq 20" #off "f\n"                                                                                               \
    "ldrb lr, [r4, r12]\n"                                                                                            \
    "cmp lr, #255\n"                                                                                                  \
    "ldrneb r12, [r2, #" #off "]\n"                                                                                   \
    "addne lr, r12, lr, lsl #8\n"                                                                                     \
    "ldrneb r12, [r5, lr]\n"                                                                                          \
    "ldrb r12, [r6, r12]\n"                                                                                           \
    "strb r12, [r2, #" #off "]\n"                                                                                     \
    "20" #off ":\n"

#endif
