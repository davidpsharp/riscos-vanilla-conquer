// Tests the frame counter's timing (common/ratemeter.h) with made-up times: no window, no clock.
#include "common/ratemeter.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

// Frames every "period_us" for "count" frames from "t", each followed by "idle_us" of waiting.
static void Run(RateMeter& m, uint64_t& t, uint64_t& idle, int count, uint64_t period_us, uint64_t idle_us)
{
    for (int i = 0; i < count; ++i) {
        t += period_us;
        m.Frame(t, idle);
        idle += idle_us; // (the wait that follows the frame stamp, ending before the next)
    }
}

int main()
{
    // Nothing yet, and a single frame, are no reading.
    {
        RateMeter m;
        CHECK(!m.Read(0).Valid);
        m.Frame(1000, 0);
        CHECK(!m.Read(2000).Valid);
    }

    // A steady 15 frames a second, 90% waiting: 15 and 10%.
    {
        RateMeter m;
        uint64_t t = 5000000, idle = 0;
        Run(m, t, idle, 60, 66667, 60000);
        RateMeter::Reading r = m.Read(t);
        CHECK(r.Valid && r.Fps == 15 && r.Busy == 10);
        // And it stays so for any amount of waiting between 0 and the whole.
        RateMeter full;
        t = 0;
        idle = 0;
        Run(full, t, idle, 60, 66667, 0);
        r = full.Read(t);
        CHECK(r.Valid && r.Fps == 15 && r.Busy == 100);
    }

    // Appears early: a rough number from the first frames, as soon as 100 ms have passed.
    {
        RateMeter m;
        uint64_t t = 0, idle = 0;
        Run(m, t, idle, 1, 66667, 0);
        CHECK(!m.Read(t).Valid); // one frame
        Run(m, t, idle, 1, 66667, 0);
        CHECK(!m.Read(t).Valid); // 66 ms is under the minimum
        Run(m, t, idle, 1, 66667, 0);
        RateMeter::Reading r = m.Read(t);
        CHECK(r.Valid && r.Fps == 15); // 133 ms
    }

    // Slowing down: the number drops below the target and the busy figure goes to 100%.
    {
        RateMeter m;
        uint64_t t = 0, idle = 0;
        Run(m, t, idle, 30, 66667, 50000); // 15 fps with room to spare
        CHECK(m.Read(t).Fps == 15 && m.Read(t).Busy == 25);
        Run(m, t, idle, 40, 100000, 0); // 10 fps flat out; the window (1.5 s) is now all of it
        RateMeter::Reading r = m.Read(t);
        CHECK(r.Valid && r.Fps == 10 && r.Busy == 100);
    }

    // Steady: a long run with the frames' times jittering by a few ms keeps the same rounded figures.
    {
        RateMeter m;
        uint64_t t = 0, idle = 0, seed = 12345;
        int lo = 1000, hi = -1;
        for (int i = 0; i < 600; ++i) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            uint64_t jitter = (seed >> 33) % 6000; // up to 6 ms
            t += 62000 + jitter;                   // 65 ms on average, so 15.4 fps
            m.Frame(t, idle);
            idle += 40000;
            RateMeter::Reading r = m.Read(t);
            if (i > 40 && r.Valid) {
                lo = r.Fps < lo ? r.Fps : lo;
                hi = r.Fps > hi ? r.Fps : hi;
            }
        }
        CHECK(hi - lo <= 1);
    }

    // A pause (no frames, as in a menu): stale after a second.
    {
        RateMeter m;
        uint64_t t = 0, idle = 0;
        Run(m, t, idle, 30, 66667, 10000);
        CHECK(m.Read(t + 500000).Valid);
        CHECK(!m.Read(t + 1500000).Valid);
        // Back again after the menu: the old frames are outside the window, so two new frames are needed.
        t += 20000000;
        m.Frame(t, idle);
        CHECK(!m.Read(t).Valid);
        Run(m, t, idle, 3, 66667, 10000);
        CHECK(m.Read(t).Valid && m.Read(t).Fps == 15);
    }

    // Waiting time can't exceed the span (a counter reset mid-wait): busy never goes below 0.
    {
        RateMeter m;
        m.Frame(0, 0);
        m.Frame(200000, 500000);
        RateMeter::Reading r = m.Read(200000);
        CHECK(r.Valid && r.Busy == 0);
    }

    // The time counter counts in 64 bits: the ring wraps around after 256 frames without trouble.
    {
        RateMeter m;
        uint64_t t = 1ull << 40, idle = 0;
        Run(m, t, idle, 1000, 5000, 1000); // 200 fps, 20% waiting
        RateMeter::Reading r = m.Read(t);
        CHECK(r.Valid && r.Fps == 200 && r.Busy == 80);
    }

    if (failures == 0) {
        printf("ratemeter: all tests passed\n");
    }
    return failures != 0;
}
