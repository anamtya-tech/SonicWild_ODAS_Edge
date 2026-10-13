#ifndef ODAS_LIVE_TIMING_H
#define ODAS_LIVE_TIMING_H

#include <stdint.h>

typedef struct {
    const char *module;
    const void *instance;
    uint64_t sequence;
    uint64_t frame;
} odas_timing_scope;

int odas_timing_enabled(void);
uint64_t odas_timing_now_ns(void);
odas_timing_scope odas_timing_begin(const char *module, const void *instance);
odas_timing_scope odas_timing_begin_frame(const char *module, const void *instance,
                                         uint64_t frame);
void odas_timing_end(odas_timing_scope *scope);
void odas_timing_event(const char *module, const void *instance,
                       uint64_t frame, uint64_t bytes, const char *phase);
void odas_timing_queue(const void *instance, uint64_t frame,
                       int queue_depth, const char *phase);

#define ODAS_TIMING_SCOPE(instance) \
    odas_timing_scope timing_scope __attribute__((cleanup(odas_timing_end))) = \
        odas_timing_begin(__func__, instance)

#define ODAS_TIMING_FRAME(instance, frame) \
    odas_timing_scope timing_scope __attribute__((cleanup(odas_timing_end))) = \
        odas_timing_begin_frame(__func__, instance, frame)

#endif