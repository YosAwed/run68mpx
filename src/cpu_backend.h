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

#endif
