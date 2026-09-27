/* Time reading a file the way the game streams movies, to check whether the
** filing system can keep up. Usage: readspeed <file> [megabytes]
** 1. Plain sequential reads in 32 KB blocks.
** 2. The VQA pattern: an 8 byte chunk header, then the chunk body (a few KB),
**    with an ftell before each read as RawFileClass does for biased files.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static char buf[32768];

static void report(const char* what, long bytes, double secs)
{
    printf("%-22s %7ld KB in %6.2f s = %7.1f KB/s\n", what, bytes / 1024, secs, secs > 0 ? bytes / 1024.0 / secs : 0.0);
}

int main(int argc, char** argv)
{
    FILE* fp;
    long limit, total;
    double start;
    size_t n;

    if (argc < 2) {
        fprintf(stderr, "usage: readspeed <file> [megabytes]\n");
        return 1;
    }
    limit = (argc > 2 ? atol(argv[2]) : 4) * 1024 * 1024;

    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }

    total = 0;
    start = now();
    while (total < limit && (n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        total += (long)n;
    }
    report("sequential 32K", total, now() - start);

    /* Use a different part of the file so the first pass hasn't cached it. */
    fseek(fp, limit, SEEK_SET);
    total = 0;
    start = now();
    while (total < limit) {
        size_t body = 2048 + (size_t)(total / 8 % 8192);
        (void)ftell(fp);
        if (fread(buf, 1, 8, fp) != 8) {
            break;
        }
        (void)ftell(fp);
        n = fread(buf, 1, body, fp);
        total += 8 + (long)n;
        if (n != body) {
            break;
        }
    }
    report("chunked (VQA pattern)", total, now() - start);

    fclose(fp);
    return 0;
}
