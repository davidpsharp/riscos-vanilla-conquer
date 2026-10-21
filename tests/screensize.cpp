// Tests the screen size choice (common/screensize.h).
#include "common/screensize.h"

#include <stdio.h>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static bool Try(const char* setting, const char* fallback, int& w, int& h)
{
    std::string note;
    w = 640;
    h = 400;
    return Choose_Screen_Size(setting, fallback, w, h, note);
}

int main()
{
    int w, h;
    CHECK(Try("1366x768", "1280x800", w, h) && w == 1366 && h == 768);
    CHECK(Try("640x400", "1280x800", w, h) && w == 640 && h == 400);
    CHECK(Try("2048x2048", "1280x800", w, h) && w == 2048);
    CHECK(Try("1280x800", "1024x768", w, h) && w == 1280);
    CHECK(!Try("3000x2000", "1280x800", w, h) && w == 1280 && h == 800);
    CHECK(!Try("2049x800", "1280x800", w, h) && w == 1280);
    CHECK(!Try("320x200", "1280x800", w, h) && w == 1280);
    CHECK(!Try("639x480", "1280x800", w, h) && w == 1280);
    CHECK(!Try("800x399", "1280x800", w, h) && w == 1280);
    CHECK(!Try("auto", "1280x800", w, h) && w == 1280);
    CHECK(!Try("", "1280x800", w, h) && w == 1280);
    CHECK(!Try("garbage", "1280x800", w, h) && w == 1280);
    CHECK(!Try("1280x", "1280x800", w, h) && w == 1280);
    CHECK(!Try("3000x2000", "bad", w, h) && w == 640 && h == 400); // no usable fallback: the default stays
    // Limits: the biggest the display can open.
    {
        std::string n;
        int lw = 640, lh = 400;
        CHECK(Choose_Screen_Size("1280x800", "1280x800", lw, lh, n, 1280, 800) && lw == 1280);
        CHECK(Choose_Screen_Size("1366x768", "1280x800", lw = 640, lh = 400, n, 1440, 900) && lw == 1366 && lh == 768);
        CHECK(!Choose_Screen_Size("2048x1536", "1280x800", lw = 640, lh = 400, n, 1280, 800) && lw == 1280 && lh == 800);
        CHECK(n.find("bigger than the screen (1280x800)") != std::string::npos);
        CHECK(!Choose_Screen_Size("1920x1200", "1280x800", lw = 640, lh = 400, n, 1680, 1050) && lw == 1280);
        CHECK(!Choose_Screen_Size("1700x900", "1280x800", lw = 640, lh = 400, n, 1680, 1050)); // wider only
        CHECK(Choose_Screen_Size("2048x1536", "1280x800", lw = 640, lh = 400, n, 0, 0) && lw == 2048); // no limit known
    }
    std::string note;
    w = h = 0;
    Choose_Screen_Size("3000x2000", "1280x800", w, h, note);
    CHECK(note.find("3000x2000") != std::string::npos && note.find("bigger") != std::string::npos);
    if (failures == 0) {
        printf("screensize: all tests passed\n");
    }
    return failures != 0;
}
