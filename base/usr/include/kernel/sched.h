#pragma once

#include <kernel/cpu.h>

#define STATE_RUNNING 0x00
#define STATE_WAITING 0x01
#define STATE_BLOCKED 0x02

/**
 * \struct thread
 * \brief The thread structure to be loaded into gs
*/
typedef struct thread {
	char* name; /*!< The name of the thread */
	char* description; /*!< Description of the thread */

	struct thread* parent_thread; /*!< The parent thread of the thread, it will be assigned to the one that spawns it */

	uint8_t state; /*!< The thread can be in different states, blocked, running, waiting */

	uint64_t runningTime; /*!< Running time, doesnt take into account time in waiting state */
	uint64_t startTime; /*!< Time since start, including waiting and blocked state */

	core_t* core; /*!< Current core the thread is running on */
} thread_t;

void sched_unreachable(void);