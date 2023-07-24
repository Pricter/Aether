#include <stdint.h>

/**
 * Sleeps in nanoseconds
*/
typedef void (*sleep_func)(uint64_t us);
extern sleep_func timer_sleep;

typedef void (*timer_reset_func)(void);
extern timer_reset_func timer_reset;


/**
 * Returns in nanoseconds
*/
typedef uint64_t(*timer_since_func)(void);
extern timer_since_func timer_since;

void timer_init(void);