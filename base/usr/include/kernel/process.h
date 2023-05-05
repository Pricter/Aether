#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

typedef struct {
	intptr_t refcount;
	union PML* directory;
	spin_lock_t lock;
} page_directory_t;

typedef struct process {
    pid_t id; /* Process ID */
    pid_t group; /* Thread group */
    pid_t job; /* tty job */
    pid_t session; /* tty session */
    int status; /* status code */
    unsigned int flags; /* finished, started, running */
    int owner;

    uid_t user;
    uid_t real_user;

    gid_t user_group;
    gid_t real_user_group;

    unsigned int mask;

    char* name;
    char* description;
    char** cmdline;

    char* wd_name;

    spin_lock_t sched_lock;

    int supplimentary_group_count;
    gid_t* supplimentary_group_list;

	/* Process times */
	uint64_t time_prev;         /* user time from previous update of usage[] */
	uint64_t time_total;        /* user time */
	uint64_t time_sys;          /* system time */
	uint64_t time_in;           /* tsc stamp of when this process last entered the running state */
	uint64_t time_switch;       /* tsc stamp of when this process last started doing system things */
	uint64_t time_children;     /* sum of user times from waited-for children */
	uint64_t time_sys_children; /* sum of sys times from waited-for children */
	uint16_t usage[4];          /* four permille samples over some period (currently 4Hz) */

    /* Tracing */
	pid_t tracer;
	spin_lock_t wait_lock;

    /* Syscall restarting */
	long interrupted_system_call;
} process_t;

struct ProcessorLocal {
    /**
     * @brief The running process on this core.
     * 
     * The current_process is a pointer to the process struct for
     * the process, or kernel tasklet currently executing.
     * Once the scheduler is active, this should always be set.
     * If a core is not current doing, its current_process should
     * be the core's idle task.
     * 
     * Because a process's data can be modified by nested interrupt
     * contexts, we mark them as volatile to avoid making assumptions
     * based on register-stored cached values
    */
   volatile process_t* current_process;

   /**
	 * @brief Idle loop.
	 *
	 * This is a special kernel tasklet that sits in a loop
	 * waiting for an interrupt from a preemption source or hardware
	 * device. Its context should never be saved, it should never
	 * be added to a sleep queue, and it should be scheduled whenever
	 * there is nothing else to do.
	 */
	process_t * kernel_idle_task;

    /**
	 * @brief Process this core was last scheduled to run.
	 */
	volatile process_t * previous_process;

	int cpu_id;
	union PML *current_pml;

	struct regs *interrupt_registers;

    int lapic_id;
	/* Processor information loaded at startup. */
	int  cpu_model;
	int  cpu_family;
	char cpu_model_name[48];
	const char * cpu_manufacturer;
};

extern struct ProcessorLocal processor_local_data[];
extern int processor_count;

static struct ProcessorLocal __seg_gs* const this_core = 0;