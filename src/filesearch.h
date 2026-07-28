#ifndef RUN68_FILESEARCH_H
#define RUN68_FILESEARCH_H

#include <stddef.h>

#include "run68.h"

Long run68_files_first(UChar *buffer, size_t buffer_size,
                       const char *name, short attributes);
Long run68_files_next(UChar *buffer, size_t buffer_size);
void run68_files_close_all(void);

#endif
