/* Allocates one 40MB block, like caching SCORES.MIX, and shows where it lands.
 * On RISC OS 3.x the application slot ends at 28MB (&1C00000); anything above
 * that is in a dynamic area. */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char* small = malloc(1024);
    char* big = malloc(40 * 1024 * 1024);
    printf("small block at %p, 40MB block at %p (%s)\n", (void*)small, (void*)big,
           big == NULL ? "FAILED" : ((unsigned long)big >= 0x1C00000 ? "outside the application slot" : "inside the application slot"));
    return big == NULL;
}
