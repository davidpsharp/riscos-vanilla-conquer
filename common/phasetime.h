// Where the time goes, for tuning on slow machines (VC_FPSLOG).
//
// PhaseTimer adds the time spent in a scope to one of the phases below;
// Phase_Report prints the totals as milliseconds per second of wall time.
// With VC_FPSLOG unset each timer costs a test and a branch.
#ifndef PHASETIME_H
#define PHASETIME_H

enum PhaseId
{
    PHASE_RENDER,   // GScreenClass::Render, inclusive
    PHASE_TACTICAL, // DisplayClass::Draw_It, inclusive (the map)
    PHASE_RADAR,    // RadarClass::Draw_It, inclusive
    PHASE_POWER,    // PowerClass::Draw_It, inclusive
    PHASE_SIDEBAR,  // SidebarClass::Draw_It, inclusive
    PHASE_LOGIC,    // Logic.AI
    PHASE_QUEUE,    // Queue_AI (events, network)
    PHASE_CALLBACK, // Call_Back (sound, music, network)
    PHASE_PRESENT,  // Video_Render_Frame (cursor, copy to screen)
    PHASE_SLEEP,    // the frame limiter sleeping
    // Audio mixing isn't timed: it runs on its own thread, overlapping the rest
    // (about 55 ms/s on a StrongARM Risc PC).
    PHASE_COUNT
};

// Counts for the report: full-screen Blit_Displays, and presents by kind.
enum PhaseCountId
{
    COUNT_BLIT_DISPLAY,
    COUNT_PRESENT_FULL,
    COUNT_PRESENT_PARTIAL,
    COUNT_PRESENT_SKIPPED,
    COUNT_DELAY_ASKED_MS, // Call_Back_Delay: time asked for...
    COUNT_DELAY_TOOK_MS,  // ...and time taken
    COUNT_MOTION_BETWEEN_FRAMES, // pointer moves picked up while the frame limiter sleeps
    COUNT_MOTION_BY_GAME,        // and by the game's own input handling
    COUNT_MAX
};

extern bool Phase_Timing;
extern unsigned Phase_Counts[COUNT_MAX];
unsigned Phase_Now_Us();
void Phase_Add(PhaseId id, unsigned us);
void Phase_Report(unsigned elapsed_ms);

class PhaseTimer
{
public:
    explicit PhaseTimer(PhaseId id)
        : Id(id)
        , Start(Phase_Timing ? Phase_Now_Us() : 0)
    {
    }
    ~PhaseTimer()
    {
        if (Phase_Timing) {
            Phase_Add(Id, Phase_Now_Us() - Start);
        }
    }

private:
    PhaseId Id;
    unsigned Start;
};

#endif
