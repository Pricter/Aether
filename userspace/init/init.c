/* Normal userspace init. Regression programs are built only by make test. */
#include <stdio.h>

/* Terminal input/process launching will extend this into a persistent init.
 * Until then, announce readiness once and return to the kernel's idle loop. */
int main(void) {
    puts("Aether init: ARM64 userspace ready.");

    return 0;
}
