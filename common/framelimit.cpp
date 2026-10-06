#include "framelimit.h"
#include "wwmouse.h"
#include "settings.h"
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

#include "mssleep.h"
#include "phasetime.h"

extern WWMouseClass* WWMouse;

unsigned Logic_Frame_Count = 0;
unsigned Present_Count = 0;

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>

/*
** At 60 frames a second on RISC OS (a fast machine), presents are paced by the screen's vertical
** sync rather than by sleeping: the clock and sleeps there are in centiseconds, so slept frames
** came 10 or 20 ms apart and motion juddered. OS_Byte 176 reads the VSync counter (it counts
** down once per vsync), and OS_Byte 19 waits for the next. VC_NOVSYNC turns this off.
*/
static bool VSync_Paced()
{
    static const bool off = getenv("VC_NOVSYNC") != nullptr;
    return !off && Settings.Video.FrameLimit >= 50;
}

static int VSync_Counter()
{
    return _kernel_osbyte(176, 0, 255) & 0xFF;
}

// True if a vsync has passed since the last present, or else (when allowed to) waits for one.
static bool VSync_Due(bool wait)
{
    static int last = -1;
    int now = VSync_Counter();
    if (now == last) {
        if (!wait) {
            return false;
        }
        PhaseTimer phase_timer(PHASE_SLEEP);
        _kernel_osbyte(19, 0, 0);
        now = VSync_Counter();
    }
    last = now;
    return true;
}
#endif

#ifdef NEW_VIDEO_BUILD
void Video_Render_Frame();
#endif
#ifdef SDL1_BUILD
void Video_Update_Cursor();
#endif

/*
** Sleeps for "us" microseconds, moving the cursor at up to 60 Hz meanwhile. The
** frame limit holds presents to 30 a second, but a cursor-only update is cheap,
** and a pointer that only moves 30 times a second feels sluggish. The pieces run
** to a fixed end time, so splitting the sleep doesn't make it any longer.
*/
static void Sleep_Updating_Cursor(unsigned us)
{
#ifdef SDL1_BUILD
    using namespace std::chrono;
    const auto step = microseconds(16667);
    const auto end = steady_clock::now() + microseconds(us);
    for (;;) {
        auto left = end - steady_clock::now();
        if (left <= steady_clock::duration::zero()) {
            break;
        }
        us_sleep(unsigned(duration_cast<microseconds>(left < step ? left : step).count()));
        if (steady_clock::now() + milliseconds(2) < end) {
            Video_Update_Cursor(); // not when the next present is about due anyway
        }
    }
#else
    us_sleep(us);
#endif
}

int Present_Rate()
{
#ifdef __riscos__
    if (VSync_Paced()) {
        /*
        ** The screen's refresh rate (75 Hz for some modes), measured once: vsyncs counted
        ** (OS_Byte 176, which counts down in 8 bits) over at least a second of the centisecond
        ** clock (OS_ReadMonotonicTime). Until then, 60.
        */
        static int rate = 0;
        static int start_cs = -1, start_count = 0;
        if (rate > 0) {
            return rate;
        }
        _kernel_swi_regs regs;
        _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
        int const now_cs = regs.r[0];
        int const count = VSync_Counter();
        if (start_cs < 0) {
            start_cs = now_cs;
            start_count = count;
        } else if (now_cs - start_cs >= 100) {
            int const vsyncs = (start_count - count) & 0xFF;
            int const elapsed = now_cs - start_cs;
            if (elapsed <= 250) { // under 256 vsyncs even at 100 Hz, so the count hasn't wrapped
                rate = (vsyncs * 100 + elapsed / 2) / elapsed;
                if (rate < 40 || rate > 150) {
                    rate = 60;
                }
                return rate;
            }
            start_cs = now_cs; // too long between calls to trust the count: measure again
            start_count = count;
        }
        return 60;
    }
#endif
    return Settings.Video.FrameLimit > 0 ? Settings.Video.FrameLimit : 60;
}

void Frame_Limiter(FrameLimitFlags flags, int max_sleep_ms)
{
    static auto frame_start = std::chrono::steady_clock::now();
#if defined(__riscos__) && defined(NEW_VIDEO_BUILD)
    if (VSync_Paced() && !(flags & FrameLimitFlags::FL_NO_BLOCK)) {
        // Present at most once per vsync: waiting for one unless the game is behind (or its
        // next frame is due now, when it presents only if a vsync has already passed).
        bool may_wait = !(flags & FrameLimitFlags::FL_NO_SLEEP) && max_sleep_ms != 0;
        if (!VSync_Due(may_wait)) {
            return;
        }
        {
            PhaseTimer phase_timer(PHASE_PRESENT);
            Video_Render_Frame();
        }
        ++Present_Count;
        Present_Rate(); // measuring the refresh rate in the first seconds
        frame_start = std::chrono::steady_clock::now();
        return;
    }
#endif
#ifdef NEW_VIDEO_BUILD
    static auto render_avg = 0;

    auto render_start = std::chrono::steady_clock::now();
    auto render_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(frame_start - render_start).count();

    if (!(flags & FrameLimitFlags::FL_FORCE_RENDER) && render_remaining > render_avg) {
        if (flags & FrameLimitFlags::FL_NO_SLEEP) {
            return;
        }
        if (max_sleep_ms == 0) {
            return; // The game's next frame is due any moment: don't sleep.
        }
        PhaseTimer phase_timer(PHASE_SLEEP);
        if (!(flags & FrameLimitFlags::FL_NO_BLOCK)) {
            // Oversleeping the game's next frame makes each one take a whole extra present slot.
            Sleep_Updating_Cursor(
                unsigned(max_sleep_ms >= 0 && max_sleep_ms < render_remaining ? max_sleep_ms : render_remaining) * 1000);
        } else {
            ms_sleep(1); // Unconditionally yield for minimum time.
        }
        return;
    }

    {
        PhaseTimer phase_timer(PHASE_PRESENT);
        Video_Render_Frame();
    }

    auto render_end = std::chrono::steady_clock::now();
    auto render_time = std::chrono::duration_cast<std::chrono::milliseconds>(render_end - render_start).count();

    // keep up some average so we have an idea if we need to skip a frame or not
    render_avg = (render_avg + render_time) / 2;
#endif
    ++Present_Count;

    if (Settings.Video.FrameLimit > 0 && !(flags & FrameLimitFlags::FL_NO_BLOCK)) {
#ifdef NEW_VIDEO_BUILD
        auto frame_end = render_end;
#else
        auto frame_end = std::chrono::steady_clock::now();
#endif
        unsigned int min_frame_time = 1000000 / Settings.Video.FrameLimit;
        auto cur_frame_time = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();
        if (cur_frame_time < min_frame_time) {
            frame_start += std::chrono::microseconds{min_frame_time};
            if (!(flags & FrameLimitFlags::FL_NO_SLEEP) && max_sleep_ms != 0) {
                PhaseTimer phase_timer(PHASE_SLEEP);
                unsigned wait = unsigned(min_frame_time - cur_frame_time);
                if (max_sleep_ms >= 0 && unsigned(max_sleep_ms) * 1000 < wait) {
                    wait = unsigned(max_sleep_ms) * 1000;
                }
                Sleep_Updating_Cursor(wait);
            }
        } else {
            frame_start = frame_end;
        }
    }
}
