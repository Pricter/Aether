#include <kernel/process.h>

struct ProcessorLocal processor_local_data[32] = {0};
int processor_count = 1;