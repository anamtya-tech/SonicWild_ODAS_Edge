#define _GNU_SOURCE
#include <utils/live_timing.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t timing_once = PTHREAD_ONCE_INIT;
static pthread_key_t timing_key;
static const char *timing_directory;
static __thread FILE *timing_file;
static __thread uint64_t timing_sequence;
static __thread int timing_open_attempted;

static void timing_close(void *file) {
    fclose((FILE *) file);
}

static void timing_initialize(void) {
    timing_directory = getenv("ODAS_TIMING_DIR");
    if (timing_directory != NULL && timing_directory[0] != '\0') {
        if (pthread_key_create(&timing_key, timing_close) != 0) {
            timing_directory = NULL;
        }
    }
}

int odas_timing_enabled(void) {
    pthread_once(&timing_once, timing_initialize);
    return timing_directory != NULL && timing_directory[0] != '\0';
}

uint64_t odas_timing_now_ns(void) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t) timestamp.tv_sec * 1000000000ULL + timestamp.tv_nsec;
}

static void timing_write(uint64_t timestamp, const char *module,
                         const void *instance, uint64_t sequence,
                         uint64_t frame, uint64_t bytes, const char *phase,
                         int queue_depth) {
    if (!timing_open_attempted) {
        char path[4096];
        timing_open_attempted = 1;
        snprintf(path, sizeof(path), "%s/thread-%ld.csv", timing_directory,
                 (long) syscall(SYS_gettid));
        timing_file = fopen(path, "w");
        if (timing_file == NULL) {
            perror("ODAS timing trace");
            return;
        }
        setvbuf(timing_file, NULL, _IOLBF, 0);
        pthread_setspecific(timing_key, timing_file);
        fprintf(timing_file, "monotonic_ns,module,instance,sequence,frame,bytes,phase,queue_depth\n");
    }
    if (timing_file != NULL) {
        fprintf(timing_file, "%llu,%s,%p,%llu,%llu,%llu,%s,%d\n",
                (unsigned long long) timestamp, module, instance,
                (unsigned long long) sequence, (unsigned long long) frame,
                (unsigned long long) bytes, phase, queue_depth);
    }
}

odas_timing_scope odas_timing_begin(const char *module, const void *instance) {
    return odas_timing_begin_frame(module, instance, 0);
}

odas_timing_scope odas_timing_begin_frame(const char *module, const void *instance,
                                         uint64_t frame) {
    odas_timing_scope scope = {module, instance, 0, frame};
    if (odas_timing_enabled()) {
        uint64_t timestamp = odas_timing_now_ns();
        scope.sequence = ++timing_sequence;
        timing_write(timestamp, module, instance, scope.sequence, frame, 0, "start", -1);
    }
    return scope;
}

void odas_timing_end(odas_timing_scope *scope) {
    if (scope->sequence != 0) {
        timing_write(odas_timing_now_ns(), scope->module, scope->instance,
                     scope->sequence, scope->frame, 0, "end", -1);
    }
}

void odas_timing_event(const char *module, const void *instance,
                       uint64_t frame, uint64_t bytes, const char *phase) {
    if (odas_timing_enabled()) {
        uint64_t timestamp = odas_timing_now_ns();
        timing_write(timestamp, module, instance, ++timing_sequence,
                     frame, bytes, phase, -1);
    }
}

void odas_timing_queue(const void *instance, uint64_t frame,
                       int queue_depth, const char *phase) {
    if (odas_timing_enabled()) {
        timing_write(odas_timing_now_ns(), "ssl_input_queue", instance,
                     ++timing_sequence, frame, 0, phase, queue_depth);
    }
}