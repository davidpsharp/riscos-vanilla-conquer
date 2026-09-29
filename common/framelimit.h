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

// Game logic frames completed, counted by the game for speed diagnostics (VC_FPSLOG).
extern unsigned Logic_Frame_Count;

#endif /* FRAMELIMIT_H */
