// The game's frame rate and how busy it is, for the frame counter (Shift+F8).
//
// The game reports each frame it finishes (its main loop pass) with the time and the total time it
// has spent waiting so far; Read gives the frames a second, and the share of the time the game was
// NOT waiting, over the last second and a half or so. No SDL, no clock of its own, so it can be
// tested with made-up times (tests/ratemeter.cpp).
#ifndef RATEMETER_H
#define RATEMETER_H

#include <cstdint>

class RateMeter
{
public:
    struct Reading
    {
        bool Valid;
        int Fps;  // frames a second, rounded
        int Busy; // percent of the time spent not waiting, 0 to 100
    };

    // The span the figures are taken over, and the least that makes a first, rough, figure.
    static const uint64_t WINDOW_US = 1500000;
    static const uint64_t MIN_SPAN_US = 100000;
    // No frame for this long: the game isn't running frames (a menu, a dialog), so no reading.
    static const uint64_t STALE_US = 1000000;

    RateMeter()
    {
        Reset();
    }

    void Reset()
    {
        Head = 0;
        Count = 0;
    }

    // A game frame has finished at "now_us"; "idle_us" is all the time spent waiting up to now.
    void Frame(uint64_t now_us, uint64_t idle_us)
    {
        Head = (Head + 1) % SIZE;
        Times[Head] = now_us;
        Idles[Head] = idle_us;
        if (Count < SIZE) {
            ++Count;
        }
    }

    Reading Read(uint64_t now_us) const
    {
        Reading r = {false, 0, 0};
        if (Count < 2 || now_us - Times[Head] > STALE_US) {
            return r;
        }
        // The oldest frame within the window (the ring is newest first going back).
        unsigned oldest = 0;
        for (unsigned back = 1; back < Count; ++back) {
            unsigned i = (Head + SIZE - back) % SIZE;
            if (Times[Head] - Times[i] > WINDOW_US) {
                break;
            }
            oldest = back;
        }
        if (oldest == 0) {
            return r;
        }
        unsigned o = (Head + SIZE - oldest) % SIZE;
        uint64_t span = Times[Head] - Times[o];
        if (span < MIN_SPAN_US) {
            return r;
        }
        uint64_t idle = Idles[Head] - Idles[o];
        if (idle > span) {
            idle = span;
        }
        r.Valid = true;
        r.Fps = int((uint64_t(oldest) * 1000000 + span / 2) / span);
        r.Busy = int(((span - idle) * 100 + span / 2) / span);
        return r;
    }

private:
    static const unsigned SIZE = 256;
    uint64_t Times[SIZE];
    uint64_t Idles[SIZE];
    unsigned Head;
    unsigned Count;
};

#endif
