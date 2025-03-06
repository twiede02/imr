extern "C" {

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

uint64_t shd_get_time_nano(void);
bool shd_read_file(const char* filename, size_t* size, unsigned char** output);

}