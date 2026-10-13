#ifndef __ODAS_MODULE_SSL
#define __ODAS_MODULE_SSL

 

    #include <stdlib.h>
    #include <string.h>

    #include "../general/mic.h"
    #include "../general/samplerate.h"
    #include "../general/soundspeed.h"
    #include "../general/spatialfilter.h"

    #include "../signal/aimg.h"   
    #include "../signal/env.h"
    #include "../signal/freq.h"
    #include "../signal/xcorr.h"
    
    #include "../system/env2env.h"
    #include "../system/freq2env.h"
    #include "../system/freq2freq.h"
    #include "../system/freq2xcorr.h"
    #include "../system/xcorr2aimg.h"
    #include "../system/xcorr2xcorr.h"
    #include "../system/xcorr2spec.h"

    #include "../init/scanning.h"
    
    #include "../message/msg_spectra.h"
    #include "../message/msg_chatak_id.h"
    #include "../message/msg_powers.h"
    #include "../message/msg_pots.h"

    // Declare session_start as extern so it’s only defined once in the .c file
    extern unsigned long long session_start;

    typedef struct mod_ssl_obj {

        unsigned int nChannels;
        unsigned int nPairs;
        unsigned int nPots;
        unsigned int nLevels;
        unsigned int fS;
        unsigned int frameSize;
        unsigned int halfFrameSize;
        unsigned int frameSizeInterp;
        unsigned int halfFrameSizeInterp;
        unsigned int interpRate;
        unsigned int nMatches;
        
        float **spec_at_peak;
        
        scans_obj * scans;

        freqs_obj * phasors;   
        freqs_obj * products;
        freqs_obj * productsInterp;
        xcorrs_obj * xcorrs;
        xcorrs_obj * xcorrsMax;
       
        freq2env_obj * freq2env;
        freq2freq_phasor_obj * freq2freq_phasor;
        freq2freq_product_obj * freq2freq_product;
        freq2freq_interpolate_obj * freq2freq_interpolate;
        freq2xcorr_obj * freq2xcorr;
        xcorr2xcorr_obj * xcorr2xcorr;
        
        aimg_obj ** aimgs;
        xcorr2aimg_obj ** xcorr2aimg;

        pots_obj * pots;

        msg_spectra_obj * in;
        msg_chatak_id_obj * in1;
        msg_pots_obj * out;
        unsigned long long profile_frames;
        double profile_pre_xcorr_s;
        double profile_gcc_phat_xcorr_s;
        double profile_srp_accum_s;
        double profile_peak_map_s;
        double profile_freq_at_peak_s;
        double profile_format_doa_s;
        char profile_enabled;
        char enabled;

    } mod_ssl_obj;

    typedef struct mod_ssl_cfg {

        mics_obj * mics;
        samplerate_obj * samplerate;
        soundspeed_obj * soundspeed;
        spatialfilters_obj * spatialfilters;
        
        unsigned int fS;
        unsigned int interpRate;
        float epsilon; 
        unsigned int nLevels;
        unsigned int * levels;
        signed int * deltas;
        unsigned int nMatches;
        float probMin;
        unsigned int nRefinedLevels;
        unsigned int nThetas;
        float gainMin;

    } mod_ssl_cfg;

    mod_ssl_obj * mod_ssl_construct(const mod_ssl_cfg * mod_ssl_config, const msg_spectra_cfg * msg_spectra_config, const msg_pots_cfg * msg_pots_config);

    void mod_ssl_destroy(mod_ssl_obj * obj);

    int mod_ssl_process(mod_ssl_obj * obj);

    void mod_ssl_connect(mod_ssl_obj * obj, msg_spectra_obj * in, msg_chatak_id_obj* in1,msg_pots_obj * out);

    void mod_ssl_disconnect(mod_ssl_obj * obj);

    void mod_ssl_enable(mod_ssl_obj * obj);

    void mod_ssl_disable(mod_ssl_obj * obj);

    mod_ssl_cfg * mod_ssl_cfg_construct(void);

    void mod_ssl_cfg_destroy(mod_ssl_cfg * cfg);

    void mod_ssl_cfg_printf(const mod_ssl_cfg * cfg);
    
    void xcorr2true_spectrum_at_peak(unsigned int fS, float **channel_spectra, unsigned int *micB, unsigned int n_pairs, unsigned int n_channels, unsigned int frame_size, unsigned int peak_index, float *spec_output, float x, float y, float z);


#endif
