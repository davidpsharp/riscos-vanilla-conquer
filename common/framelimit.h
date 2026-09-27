#ifndef FRAMELIMIT_H
#define FRAMELIMIT_H

enum FrameLimitFlags
{
    FL_NONE = 0,
    FL_FORCE_RENDER = 1 << 0,
    FL_NO_BLOCK = 1 << 1,
};

void Frame_Limiter(FrameLimitFlags flags = FL_FORCE_RENDER);

// Game logic frames completed, counted by the game for speed diagnostics (VC_FPSLOG).
extern unsigned Logic_Frame_Count;

#endif /* FRAMELIMIT_H */
