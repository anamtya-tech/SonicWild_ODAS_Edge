#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef int (*process_fn)(void *);

static process_fn next_ssl_process;
static process_fn next_sst_process;
static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static _Atomic uint64_t ssl_frames;
static _Atomic uint64_t ssl_total_ns;
static _Atomic uint64_t sst_frames;
static _Atomic uint64_t sst_total_ns;

static void resolve_process_functions(void) {
    *(void **) (&next_ssl_process) = dlsym(RTLD_NEXT, "mod_ssl_process");
    *(void **) (&next_sst_process) = dlsym(RTLD_NEXT, "mod_sst_process");
}

static uint64_t elapsed_ns(const struct timespec *start,
                           const struct timespec *end) {
    int64_t elapsed = (int64_t) (end->tv_sec - start->tv_sec) * 1000000000LL +
                      (int64_t) (end->tv_nsec - start->tv_nsec);

    return elapsed > 0 ? (uint64_t) elapsed : 0;
}

int mod_ssl_process(void *obj) {
    struct timespec start;
    struct timespec end;
    int result;

    pthread_once(&resolve_once, resolve_process_functions);
    if (next_ssl_process == NULL) {
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    result = next_ssl_process(obj);
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (result == 0) {
        uint64_t elapsed = elapsed_ns(&start, &end);

        atomic_fetch_add(&ssl_total_ns, elapsed);
        atomic_fetch_add(&ssl_frames, 1);
    }
    return result;
}

int mod_sst_process(void *obj) {
    struct timespec start;
    struct timespec end;
    int result;

    pthread_once(&resolve_once, resolve_process_functions);
    if (next_sst_process == NULL) {
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    result = next_sst_process(obj);
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (result == 0) {
        uint64_t elapsed = elapsed_ns(&start, &end);

        atomic_fetch_add(&sst_total_ns, elapsed);
        atomic_fetch_add(&sst_frames, 1);
    }
    return result;
}

static void print_timing(FILE *output, const char *version, const char *name,
                         uint64_t frames, uint64_t total_ns) {
    double average_ms = frames != 0
                            ? (double) total_ns / (double) frames / 1000000.0
                            : 0.0;

    fprintf(output,
            "ODAS_TIMING version=%s module=%s frames=%llu avg_ms_per_frame=%.6f\n",
            version,
            name,
            (unsigned long long) frames,
            average_ms);
}

__attribute__((destructor)) static void print_timing_summary(void) {
    const char *path = getenv("ODAS_TIMING_FILE");
    const char *version = getenv("ODAS_TIMING_LABEL");
    FILE *output = stdout;

    if (version == NULL || version[0] == '\0') {
        version = "unlabeled";
    }
    if (path != NULL && path[0] != '\0') {
        output = fopen(path, "a");
        if (output == NULL) {
            perror("ODAS_TIMING_FILE");
            output = stdout;
        }
    }

    print_timing(output, version, "ssl", atomic_load(&ssl_frames),
                 atomic_load(&ssl_total_ns));
    print_timing(output, version, "sst", atomic_load(&sst_frames),
                 atomic_load(&sst_total_ns));
    fflush(output);
    if (output != stdout) {
        fclose(output);
    }
}
