#ifndef FRAMELIMIT_H
#define FRAMELIMIT_H

enum FrameLimitFlags
{
    FL_NONE = 0,
    FL_FORCE_RENDER = 1 << 0,
    FL_NO_BLOCK = 1 << 1,
    FL_NO_SLEEP = 1 << 2, // The game is behind: present if due, but never wait.
};

// max_sleep_ms, if not negative, caps any wait: when the game's next frame is due.
void Frame_Limiter(FrameLimitFlags flags = FL_FORCE_RENDER, int max_sleep_ms = -1);

// Frames presented (or, without a separate present, frames limited) so far: the scroll steps
// once per frame by this.
extern unsigned Present_Count;

// Frames presented per second when presenting steadily: the screen's refresh rate where presents
// follow its vertical sync (RISC OS at 60), or else the frame limit (60 if none).
int Present_Rate();

// Game logic frames completed, counted by the game for speed diagnostics (VC_FPSLOG).
extern unsigned Logic_Frame_Count;

/*
** The frame counter (Shift+F8) shows the game's own frame rate, and how busy it is. A game frame
** is one pass of its main loop (Game_Frame_Done, once a pass); the busy figure is the share of
** the time not spent waiting for the next frame (Idle_Enter / Idle_Leave around the waits: the
** game's wait for its next tick and the frame limiter's sleeps). All of it does nothing, and
** costs a test, while the counter is off.
*/
extern bool Rate_Meter_On;
void Rate_Meter_Enable(bool on);
void Rate_Frame_Slow();
void Rate_Idle_Enter_Slow();
void Rate_Idle_Leave_Slow();
bool Rate_Meter_Read(int& fps, int& busy_percent); // false: no game frames lately (a menu, say)

// Called once per pass of the game's main loop, after the logic.
inline void Game_Frame_Done()
{
    ++Logic_Frame_Count;
    if (Rate_Meter_On) {
        Rate_Frame_Slow();
    }
}

// Waiting begins / ends (these nest).
inline void Idle_Enter()
{
    if (Rate_Meter_On) {
        Rate_Idle_Enter_Slow();
    }
}

inline void Idle_Leave()
{
    if (Rate_Meter_On) {
        Rate_Idle_Leave_Slow();
    }
}

#endif /* FRAMELIMIT_H */
