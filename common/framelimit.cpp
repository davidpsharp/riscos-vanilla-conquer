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
        PhaseTimer phase_timer(PHASE_SLEEP);
        if (!(flags & FrameLimitFlags::FL_NO_BLOCK)) {
            // Oversleeping the game's next frame makes each one take a whole extra present slot.
            ms_sleep(unsigned(max_sleep_ms >= 0 && max_sleep_ms < render_remaining ? max_sleep_ms : render_remaining));
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
            if (!(flags & FrameLimitFlags::FL_NO_SLEEP)) {
                PhaseTimer phase_timer(PHASE_SLEEP);
                unsigned wait = unsigned(min_frame_time - cur_frame_time);
                if (max_sleep_ms >= 0 && unsigned(max_sleep_ms) * 1000 < wait) {
                    wait = unsigned(max_sleep_ms) * 1000;
                }
                us_sleep(wait);
            }
        } else {
            frame_start = frame_end;
        }
    }
}
