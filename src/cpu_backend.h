#ifndef RUN68_CPU_BACKEND_H
#define RUN68_CPU_BACKEND_H

#include "run68.h"

typedef enum {
	RUN68_CPU_LEGACY = 0,
	RUN68_CPU_MUSASHI
} RUN68_CPU_BACKEND;

BOOL cpu_backend_select(const char *name);
const char *cpu_backend_name(void);
BOOL cpu_backend_is_musashi(void);
void cpu_backend_prepare(void);
BOOL cpu_backend_execute_one(void);
unsigned int cpu_backend_last_cycles(void);
void cpu_backend_set_irq(unsigned int level);
void cpu_backend_set_irq_vector(unsigned int level, int vector);

#endif
