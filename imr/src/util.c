#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include <stdint.h>
#if defined(__MINGW64__) | defined(__MINGW32__)
#include <pthread.h>
uint64_t shd_get_time_nano() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec * 1000000000 + t.tv_nsec;
}
#else
#include <time.h>
uint64_t shd_get_time_nano(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1000000000 + t.tv_nsec;
}
#endif

static long get_file_size(FILE* f) {
    if (fseek(f, 0, SEEK_END) != 0)
        return -1;

    long fsize = ftell(f);

    if (fsize == -1)
        return -1;

    if (fseek(f, 0, SEEK_SET) != 0)  /* same as rewind(f); */
        return -1;

    return fsize;
}

bool shd_read_file(const char* filename, size_t* size, char** output) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
        return false;

    long fsize = get_file_size(f);
    if (fsize < 0)
        goto err_post_open;

    // pad an extra zero at the end so this can be safely treated like a string
    unsigned char* string = malloc(fsize + 1);
    if (!string)
        goto err_post_open;

    if (fread(string, fsize, 1, f) != 1)
        goto err_post_alloc;

    fclose(f);

    string[fsize] = 0;
    if (output)
        *output = string;
    if (size)
        *size = fsize;
    return true;

    err_post_alloc:
    free(string);
    err_post_open:
    fclose(f);
    return false;
}

bool shd_write_file(const char* filename, size_t size, const char* data) {
    FILE* f = fopen(filename, "wb");
    if (f == NULL)
        return false;

    if (fwrite(data, size, 1, f) != 1)
        goto err_post_open;

    fclose(f);

    return true;

    err_post_open:
    fclose(f);
    return false;
}

#include <assert.h>
#ifdef WIN32
#include <windows.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <unistd.h>
#include <stdio.h>
#endif
const char* shd_get_executable_location(void) {
    size_t len = 256;
    char* buf = calloc(len + 1, 1);
#ifdef WIN32
    size_t final_len = GetModuleFileNameA(NULL, buf, len);
#elif __APPLE__
    uint32_t final_len = len;
    _NSGetExecutablePath(buf, &final_len);
#else
    size_t final_len = readlink("/proc/self/exe", buf, len);
#endif
    assert(final_len <= len);
    return buf;
}
