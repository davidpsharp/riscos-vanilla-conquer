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

#endif /* FRAMELIMIT_H */
