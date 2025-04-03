#ifndef IMR_UTIL_H
#define IMR_UTIL_H

extern "C" {

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

uint64_t imr_get_time_nano(void);
bool imr_read_file(const char* filename, size_t* size, unsigned char** output);

const char* imr_get_executable_location(void);

}

#endif
