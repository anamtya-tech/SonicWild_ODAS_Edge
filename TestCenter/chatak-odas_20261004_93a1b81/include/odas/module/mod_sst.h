#ifndef __ODAS_MODULE_SST
#define __ODAS_MODULE_SST

// LOG_DIR is now dynamically configured via sst.classifier_log_dir in config file

/**
 * \file     mod_sst.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <json-c/json.h>
#include <limits.h>

#include "../general/mic.h"

#include "../signal/coherence.h"
#include "../signal/kalman.h"
#include "../signal/mixture.h"
#include "../signal/particle.h"

#include "../system/kalman2kalman.h"
#include "../system/particle2particle.h"
#include "../system/kalman2coherence.h"
#include "../system/particle2coherence.h"
#include "../system/mixture2mixture.h"
#include "../system/track2gain.h"
#include "../system/gain2mask.h"
#include "../system/track2steer.h"
#include "../system/steer2demixing.h"
#include "../system/demixing2freq.h"
#include "../signal/gain.h"
#include "../signal/mask.h"
#include "../signal/steer.h"
#include "../signal/demixing.h"

#include "../message/msg_pots.h"
#include "../message/msg_targets.h"
#include "../message/msg_tracks.h"
#include "../message/msg_spectra.h"
#include "../module/mod_ssl.h"

// YAMNet C API: single source of truth for all YAMNet declarations
#include "yamnet/yamnet_c_api.h"

// Top-K and rolling history configuration
#define TOPK 5
#define ROLLING_HOPS 6

// One-time first-frame START_FLAG payload for tracks output
#define SST_START_FLAG_TAG "dynamic"
#define SST_START_FLAG_CLASS "START_FLAG"
#define SST_START_FLAG_POS 0.100f
#define SST_START_FLAG_ACTIVITY 0.000f
#define SST_START_FLAG_CONF 0.100f

// Track spectra buffer struct
typedef struct track_spectrum_obj {
    unsigned int id;                 // Track ID
    char type;                       // Track type
    unsigned int count;              // Frame counter (spectral frames stored)
    unsigned int hop_age;            // Hop age — increments every hop unconditionally
    unsigned int lastFrameSeen;
    float **buffer;                  // Circular buffer of spectral snapshots
    unsigned long int timestamp;
} track_spectrum_obj;

// Rolling Top-K history for each track
typedef struct topk_hop_t {
    int class_ids[TOPK];             // Top-K class IDs
    float confidences[TOPK];         // Top-K confidences
    unsigned long long timestamp;    // When this classification occurred
} topk_hop_t;

/**
 * Result of compute_event(): the mode class across the 6-hop rolling window,
 * its vote count, and the mean confidence of the agreeing hops.
 * class_id == -1 means no hops have been filled yet.
 */
typedef struct sst_event_t {
    int   class_id;   /* Mode top-1 class ID across ROLLING_HOPS hops */
    int   votes;      /* Number of hops that agreed on class_id (0–ROLLING_HOPS) */
    float avg_conf;   /* Mean confidence across all top-K appearances of winner */
    float max_conf;   /* Best single-hop top-1 confidence for the winner */
} sst_event_t;

typedef struct mod_sst_obj {

    unsigned int nPots;
    unsigned int nTracksMax;
    unsigned int nTracks;
    unsigned int nTargetsMax;

    unsigned int interpRate;

    unsigned int frameSize;
    unsigned int halfFrameSize;

    unsigned int hopSize;
    unsigned int fS;
    unsigned int nBits;

    track_spectrum_obj *trackSpectra;

    track2gain_obj * track2gain;
    gain2mask_obj * gain2mask;
    track2steer_obj * track2steer;
    steer2demixing_ds_obj * steer2demixing;
    demixing2freq_obj * demixing2freq;

    gains_obj * gains;
    masks_obj * masks;
    steers_obj * steers;
    demixings_obj * demixingsNow;

    char mode;
    char add;

    mixture_obj ** mixtures;
    coherences_obj ** coherences;
    postprobs_obj ** postprobs;

    unsigned long long * ids;
    unsigned long long * idsAdded;
    unsigned long long * idsRemoved;
    char ** tags;

    char * type;

    kalman_obj ** kalmans;
    kalman2kalman_obj * kalman2kalman_prob;
    kalman2kalman_obj * kalman2kalman_active;
    kalman2kalman_obj * kalman2kalman_target;
    kalman2coherence_obj * kalman2coherence_prob;
    kalman2coherence_obj * kalman2coherence_active;
    kalman2coherence_obj * kalman2coherence_target;

    particles_obj ** particles;
    particle2particle_obj * particle2particle_prob;
    particle2particle_obj * particle2particle_active;
    particle2particle_obj * particle2particle_target;
    particle2coherence_obj * particle2coherence_prob;
    particle2coherence_obj * particle2coherence_active;
    particle2coherence_obj * particle2coherence_target;

    mixture2mixture_obj * mixture2mixture;

    float * sourceActivities;

    float theta_new;
    unsigned int N_prob;
    float theta_prob;
    unsigned int * N_inactive;
    float theta_inactive;

    unsigned int * n_prob;
    float * mean_prob;
    unsigned int * n_inactive;

    unsigned long long id;

    msg_pots_obj * in1;
    msg_targets_obj * in2;
    msg_tracks_obj * out;
    int startup_dummy_sent;

    unsigned long long profile_frames;
    double profile_track_mgmt_s;
    double profile_predict_s;
    double profile_coherence_s;
    double profile_mixture_assign_s;
    double profile_update_s;
    double profile_activity_classify_s;
    double profile_transitions_dynamic_s;
    double profile_output_copy_s;
    char profile_enabled;

    char enabled;
    char enable_classifier_output;  // Flag to enable JSON output with classification data
    char * classifier_log_dir;      // Directory path for classifier output files

    // YAMNet handle per module if you want instance isolation
    yamnet_handle_t* yamnet; 

    /* Per-track classification smoothing state */
    int *last_class_id;            /* -1 = none */
    float *last_class_conf;
    unsigned long long *last_class_ts;
    
    /* Rolling Top-K history buffer (6 hops × nTracksMax) */
    topk_hop_t **topk_history;     /* [iTrack][hop_index] */
    int *topk_head;                /* Current write position in circular buffer */
    int *topk_count;               /* Number of valid entries (0 to ROLLING_HOPS) */

    /**
     * sim_mode — Controls .bin sidecar output per YAMNet hop.
     *   0 = Pi/edge: JSON only. No .bin files written. Minimises disk I/O.
     *   1 = Simulator/VM: writes a 96×257 float32 .bin file per hop so the
     *       Python simulator can stitch 6 hops (~3s) via
     *       AudioReconstructor.reconstruct_multi_frame() for human review.
     *
     * min_event_votes — Noise gate for JSON emission. An event entry is only
     *   written to the session JSONL when this many of the last ROLLING_HOPS (6)
     *   YAMNet top-1 predictions agree on the same class.
     *   Range: 1–6. Default: 4 (≥4/6 = robust majority).
     *   Tip: use 3 in noisy outdoor environments, 5–6 for strict monitoring.
     *
     * last_patch_path — Per-track path of the most recently written .bin
     *   sidecar. Empty string when sim_mode==0 or no hop classified yet.
     */
    int   sim_mode;
    int   min_event_votes;
    char **last_patch_path;        /* [iTrack] → malloc'd char[512] */

    char sstParametersPath[512];
    time_t lastSstParamsUpdateTime;

} mod_sst_obj;


typedef struct mod_sst_cfg {
    char mode;
    char add;

    unsigned int nTracksMax;
    unsigned int frameSize;
    unsigned int halfFrameSize;
    unsigned int hopSize;
    unsigned int fS;
    unsigned int nBits;

    mics_obj * mics;
    samplerate_obj * samplerate;
    soundspeed_obj * soundspeed;
    float gainMin;

    float sigmaQ;

    unsigned int nParticles;
    float st_alpha;
    float st_beta;
    float st_ratio;
    float ve_alpha;
    float ve_beta;
    float ve_ratio;
    float ac_alpha;
    float ac_beta;
    float ac_ratio;
    float Nmin;

    float epsilon;
    float sigmaR_active;
    float sigmaR_prob;
    float sigmaR_target;
    gaussians_1d_obj * active_gmm;
    gaussians_1d_obj * inactive_gmm;
    float Pfalse;
    float Pnew;
    float Ptrack;

    float theta_new;
    unsigned int N_prob;
    float theta_prob;
    unsigned int * N_inactive;
    float theta_inactive;

    char enable_classifier_output;  // Flag to enable JSON output with classification data
    char * classifier_log_dir;      // Directory path for classifier output files
    char * model_path;              // Directory containing yamnet_core.tflite and yamnet_class_map.csv

    /**
     * sim_mode — Set to 1 on the simulator/VM, 0 on the Raspberry Pi.
     *   1: Writes a 96×257 float32 little-endian .bin sidecar for every YAMNet
     *      hop. The Python simulator loads these via:
     *        np.fromfile(path, dtype=np.float32).reshape(96, 257)
     *      and stitches 6 consecutive hops into ~3s audio using Griffin-Lim
     *      for human review and training-data quality control.
     *   0: No .bin files written. The JSON spectra_file field is empty string.
     *      Use this on Pi to avoid unnecessary disk writes.
     *
     * min_event_votes — Noise gate threshold. Only emit an event to the
     *   session JSONL when this many of the last ROLLING_HOPS=6 YAMNet hops
     *   return the same top-1 class. Range: 1–6. Default: 4.
     *   - 4/6: majority vote (~67%%) — recommended for outdoor wildlife.
     *   - 3/6: lenient — use when vocalisations are short or intermittent.
     *   - 6/6: strict — all hops agree; very few false positives.
     */
    int sim_mode;          /* 0 = Pi (no .bin), 1 = Simulator (write .bin) */
    int min_event_votes;   /* 1–6, default 4 */

    char sstParametersPath[512];

} mod_sst_cfg;

mod_sst_obj * mod_sst_construct(const mod_sst_cfg * mod_sst_config, const mod_ssl_cfg * mod_ssl_config, const msg_pots_cfg * msg_pots_config, const msg_targets_cfg * msg_targets_config, const msg_tracks_cfg * msg_tracks_config, const msg_spectra_cfg * msg_spectra_config);

void mod_sst_destroy(mod_sst_obj * obj);

int mod_sst_process(mod_sst_obj * obj);

void mod_sst_connect(mod_sst_obj * obj, msg_pots_obj * in1, msg_targets_obj * in2, msg_tracks_obj * out);

void mod_sst_disconnect(mod_sst_obj * obj);

void mod_sst_enable(mod_sst_obj * obj);

void mod_sst_disable(mod_sst_obj * obj);

mod_sst_cfg * mod_sst_cfg_construct(void);

void mod_sst_cfg_destroy(mod_sst_cfg * cfg);

void mod_sst_cfg_printf(const mod_sst_cfg * cfg);

void push_pot_to_track_buffer(mod_sst_obj* obj, unsigned int iPot, unsigned int iTrack, unsigned long long trackID, unsigned int nFramesPerTrack, int debug);

void dump_track_buffers_to_json(mod_sst_obj *obj, const char *filename, unsigned int nFramesPerTrack);

void reset_track_slot(mod_sst_obj *obj, unsigned int iTrackMax, unsigned int nFramesPerTrack);

void log_classification_event(mod_sst_obj *obj, unsigned int iTrack, unsigned long long trackID);

void dump_track_fingerprint_only(mod_sst_obj *obj, const char *basename, unsigned int nFramesPerTrack);

void ensure_log_dir_exists(const char* log_dir);

#endif
