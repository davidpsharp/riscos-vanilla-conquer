#include "framelimit.h"
#include "wwmouse.h"
#include "settings.h"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

#include "mssleep.h"
#include "phasetime.h"

extern WWMouseClass* WWMouse;

unsigned Logic_Frame_Count = 0;

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

void Frame_Limiter(FrameLimitFlags flags, int max_sleep_ms)
{
    static auto frame_start = std::chrono::steady_clock::now();
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
