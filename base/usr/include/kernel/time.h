#include <stdint.h>

typedef (*sleep_func)(uint64_t us);
extern sleep_func timer_sleep;

void timer_init(void);