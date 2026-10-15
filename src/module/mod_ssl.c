
    
    #include <module/mod_ssl.h>
    #include <ctype.h>
    #include <errno.h>
    #include <limits.h>

    #define MOD_SSL_MIN_HZ 1000U

        #include <stdio.h>
        #include <stdlib.h>
        #include <time.h>

        static double mod_ssl_elapsed_seconds(const struct timespec * start, const struct timespec * end) {

            return ((double) (end->tv_sec - start->tv_sec)) +
                   (((double) (end->tv_nsec - start->tv_nsec)) / 1000000000.0);

        }

        static void mod_ssl_profile_printf(const mod_ssl_obj * obj) {

            double frame_count;
            double pre_xcorr_ms;
            double gcc_phat_xcorr_ms;
            double srp_accum_ms;
            double peak_map_ms;
            double freq_at_peak_ms;
            double format_doa_ms;
            double total_ms;

            if ((obj->profile_enabled == 0) || (obj->profile_frames == 0)) {
                return;
            }

            frame_count = (double) obj->profile_frames;
            pre_xcorr_ms = 1000.0 * obj->profile_pre_xcorr_s / frame_count;
            gcc_phat_xcorr_ms = 1000.0 * obj->profile_gcc_phat_xcorr_s / frame_count;
            srp_accum_ms = 1000.0 * obj->profile_srp_accum_s / frame_count;
            peak_map_ms = 1000.0 * obj->profile_peak_map_s / frame_count;
            freq_at_peak_ms = 1000.0 * obj->profile_freq_at_peak_s / frame_count;
            format_doa_ms = 1000.0 * obj->profile_format_doa_s / frame_count;
            total_ms = pre_xcorr_ms + gcc_phat_xcorr_ms + srp_accum_ms + peak_map_ms + freq_at_peak_ms + format_doa_ms;

            printf("+--------------------------------------------------+\n");
            printf("|           SSL Stage Timing (avg / frame)         |\n");
            printf("+--------------------------------------------------+\n");
            printf("| Frames profiled............. %12llu |\n", obj->profile_frames);
            printf("| Pre-XCorr................... %12.3f ms |\n", pre_xcorr_ms);
            printf("| GCC-PHAT XCorr.............. %12.3f ms |\n", gcc_phat_xcorr_ms);
            printf("| SRP Accumulation............ %12.3f ms |\n", srp_accum_ms);
            printf("| Peak Extraction + Mapping... %12.3f ms |\n", peak_map_ms);
            printf("| Freq at Peak................ %12.3f ms |\n", freq_at_peak_ms);
            printf("| Format DOA Angles........... %12.3f ms |\n", format_doa_ms);
            printf("+--------------------------------------------------+\n");
            printf("| Full SSL Frame.............. %12.3f ms |\n", total_ms);
            printf("+--------------------------------------------------+\n");

        }

    static int mod_ssl_bandpass_error(mod_ssl_obj * obj, int error) {

        if (obj->bandpassError != error) {
            fprintf(stderr, "SSL: cannot read bandpass '%s': %s; keeping previous band\n",
                    obj->bandpassPath, strerror(error));
        }
        obj->bandpassError = error;
        return -1;

    }

    int mod_ssl_reload_bandpass(mod_ssl_obj * obj) {

        struct stat state;
        FILE * file;
        char line[128];
        unsigned int lowHz = 0;
        unsigned int highHz = 0;
        unsigned int seen = 0;
        unsigned int lowCut;
        unsigned int highCut;
        unsigned int effectiveLowHz;
        int valid = 1;
        int error = 0;

        if (stat(obj->bandpassPath, &state) != 0) {
            return mod_ssl_bandpass_error(obj, errno);
        }

        if ((obj->bandpassError == 0) &&
            (state.st_dev == obj->bandpassStat.st_dev) &&
            (state.st_ino == obj->bandpassStat.st_ino) &&
            (state.st_size == obj->bandpassStat.st_size) &&
            (state.st_mtim.tv_sec == obj->bandpassStat.st_mtim.tv_sec) &&
            (state.st_mtim.tv_nsec == obj->bandpassStat.st_mtim.tv_nsec)) {
            return obj->bandpassRejected ? -1 : 0;
        }

        file = fopen(obj->bandpassPath, "r");
        if (file == NULL) {
            return mod_ssl_bandpass_error(obj, errno);
        }
        if (fstat(fileno(file), &state) != 0) {
            error = errno;
            fclose(file);
            return mod_ssl_bandpass_error(obj, error);
        }

        while (fgets(line, sizeof(line), file) != NULL) {
            char * key = line;
            char * separator;
            char * value;
            char * end;
            unsigned long hz;
            unsigned int field;

            while (isspace((unsigned char) *key)) key++;
            if (*key == '\0') continue;
            separator = strchr(key, '=');
            if (separator == NULL) {
                valid = 0;
                break;
            }
            end = separator;
            while ((end > key) && isspace((unsigned char) end[-1])) end--;
            *end = '\0';
            field = (strcmp(key, "lowHz") == 0) ? 1 :
                    (strcmp(key, "highHz") == 0) ? 2 : 0;
            value = separator + 1;
            while (isspace((unsigned char) *value)) value++;
            errno = 0;
            hz = strtoul(value, &end, 10);
            if (!isdigit((unsigned char) *value) || (errno != 0) || (hz > UINT_MAX)) {
                valid = 0;
                break;
            }
            while (isspace((unsigned char) *end)) end++;
            if ((*end != '\0') || (field == 0) || ((seen & field) != 0)) {
                valid = 0;
                break;
            }
            if (field == 1) lowHz = (unsigned int) hz;
            if (field == 2) highHz = (unsigned int) hz;
            seen |= field;
        }
        if (ferror(file)) error = errno ? errno : EIO;
        if ((fclose(file) != 0) && (error == 0)) error = errno;
        if (error != 0) {
            return mod_ssl_bandpass_error(obj, error);
        }

        obj->bandpassStat = state;
        obj->bandpassError = 0;
        obj->bandpassRejected = 1;
        if (!valid || (seen != 3) || (obj->fS == 0) ||
            (lowHz >= highHz) || (highHz > obj->fS / 2)) {
            fprintf(stderr, "SSL: invalid bandpass '%s': expected integer "
                    "0 <= lowHz < highHz <= %u; keeping previous band\n",
                    obj->bandpassPath, obj->fS / 2);
            return -1;
        }

        effectiveLowHz = (lowHz < MOD_SSL_MIN_HZ) ? MOD_SSL_MIN_HZ : lowHz;
        /* Round inward so only bin centers within the effective band vote. */
        lowCut = (unsigned int) (((unsigned long long) effectiveLowHz * obj->frameSize + obj->fS - 1) / obj->fS);
        highCut = (unsigned int) (((unsigned long long) highHz * obj->frameSize) / obj->fS);
        if (highHz <= MOD_SSL_MIN_HZ) {
            lowCut = highCut + 1;
        }
        else if (lowCut > highCut) {
            fprintf(stderr, "SSL: bandpass '%s' contains no FFT bins; keeping previous band\n",
                    obj->bandpassPath);
            return -1;
        }

        obj->freq2freq_bandpass->lowCut = lowCut;
        obj->freq2freq_bandpass->highCut = highCut;
        obj->bandpassRejected = 0;
        if (lowCut > highCut) {
            printf("SSL: bandpass updated: %u-%u Hz; localization disabled "
                   "(upper cutoff <= %u Hz minimum)\n", lowHz, highHz, MOD_SSL_MIN_HZ);
        }
        else {
            printf("SSL: bandpass updated: requested %u-%u Hz, effective %u-%u Hz (%u-%u bins)\n",
                   lowHz, highHz, effectiveLowHz, highHz, lowCut, highCut);
        }
        return 0;

    }

    mod_ssl_obj * mod_ssl_construct(const mod_ssl_cfg * mod_ssl_config, const msg_spectra_cfg * msg_spectra_config, const msg_pots_cfg * msg_pots_config) {

        mod_ssl_obj * obj;
        unsigned int iLevel;
        unsigned int iPot;

        points_obj * points;

        obj = (mod_ssl_obj *) malloc(sizeof(mod_ssl_obj));

        obj->nChannels = mod_ssl_config->mics->nChannels;
        obj->nPairs = obj->nChannels * (obj->nChannels - 1) / 2;
        obj->nPots = msg_pots_config->nPots;
        obj->nLevels = mod_ssl_config->nLevels;
        obj->fS = msg_spectra_config->fS;
        obj->frameSize = 2 * (msg_spectra_config->halfFrameSize - 1);
        obj->halfFrameSize = msg_spectra_config->halfFrameSize; 
        obj->frameSizeInterp = obj->frameSize * mod_ssl_config->interpRate;
        obj->halfFrameSizeInterp = (obj->halfFrameSize - 1) * mod_ssl_config->interpRate + 1;
        obj->interpRate = mod_ssl_config->interpRate;
        snprintf(obj->bandpassPath, sizeof(obj->bandpassPath), "%s", mod_ssl_config->bandpassPath);
        memset(&obj->bandpassStat, 0, sizeof(obj->bandpassStat));
        obj->bandpassError = 0;
        obj->bandpassRejected = 0;
        obj->freq2freq_bandpass = freq2freq_bandpass_construct_zero(obj->halfFrameSize, 0, obj->halfFrameSize - 1);
        if (mod_ssl_reload_bandpass(obj) != 0) {
            fprintf(stderr, "SSL: cannot initialize localization band; aborting\n");
            exit(EXIT_FAILURE);
        }

        obj->scans = scanning_init_scans(mod_ssl_config->mics, 
                                         mod_ssl_config->spatialfilters,
                                         mod_ssl_config->nLevels, 
                                         mod_ssl_config->levels, 
                                         mod_ssl_config->samplerate->mu, 
                                         mod_ssl_config->soundspeed, 
                                         mod_ssl_config->nMatches, 
                                         obj->frameSize, 
                                         mod_ssl_config->deltas,
                                         mod_ssl_config->probMin, 
                                         mod_ssl_config->nRefinedLevels, 
                                         mod_ssl_config->nThetas, 
                                         mod_ssl_config->gainMin,
                                         mod_ssl_config->interpRate);      

        obj->freq2freq_phasor = freq2freq_phasor_construct_zero(obj->halfFrameSize,
                                                                mod_ssl_config->epsilon);

        obj->phasors = freqs_construct_zero(mod_ssl_config->mics->nChannels, 
                                            msg_spectra_config->halfFrameSize);

        obj->freq2freq_product = freq2freq_product_construct_zero(obj->halfFrameSize);

        obj->products = freqs_construct_zero(mod_ssl_config->mics->nPairs, 
                                             msg_spectra_config->halfFrameSize);

        obj->freq2freq_interpolate = freq2freq_interpolate_construct_zero(obj->halfFrameSize,
                                                                          obj->halfFrameSizeInterp);

        obj->productsInterp = freqs_construct_zero(mod_ssl_config->mics->nPairs,
                                                   obj->halfFrameSizeInterp);

        obj->freq2xcorr = freq2xcorr_construct_zero(obj->frameSizeInterp, 
                                                    obj->halfFrameSizeInterp);
        
        obj->xcorrs = xcorrs_construct_zero(mod_ssl_config->mics->nPairs,
                                            obj->frameSizeInterp);

        obj->xcorrsMax = xcorrs_construct_zero(mod_ssl_config->mics->nPairs,
                                               obj->frameSizeInterp);

        obj->aimgs = (aimg_obj **) malloc(sizeof(aimg_obj *) * msg_pots_config->nPots);

        for (iLevel = 0; iLevel < mod_ssl_config->nLevels; iLevel++) {

            obj->aimgs[iLevel] = aimg_construct_zero(obj->scans->points[iLevel]->nPoints);

        }

        obj->xcorr2xcorr = xcorr2xcorr_construct_zero(obj->frameSizeInterp);
        
        obj->xcorr2aimg = (xcorr2aimg_obj **) malloc(sizeof(xcorr2aimg_obj *) * mod_ssl_config->nLevels);

        for (iLevel = 0; iLevel < mod_ssl_config->nLevels; iLevel++) {
            
            obj->xcorr2aimg[iLevel] = xcorr2aimg_construct_zero(obj->scans->points[iLevel]->nPoints);	

        }       

        obj->pots = pots_construct_zero(msg_pots_config->nPots);

        obj->in = (msg_spectra_obj *) NULL;
        obj->in1 = (msg_chatak_id_obj*) NULL;
        obj->out = (msg_pots_obj *) NULL;

        obj->profile_frames = 0;
        obj->profile_pre_xcorr_s = 0.0;
        obj->profile_gcc_phat_xcorr_s = 0.0;
        obj->profile_srp_accum_s = 0.0;
        obj->profile_peak_map_s = 0.0;
        obj->profile_freq_at_peak_s = 0.0;
        obj->profile_format_doa_s = 0.0;
        obj->profile_enabled = (getenv("ODAS_SSL_PROFILE") != NULL) ? 0x01 : 0x00;

        obj->enabled = 0;
      
        return obj;

    }

    void mod_ssl_destroy(mod_ssl_obj * obj) {

        unsigned int iLevel;
        unsigned int iPot;

            mod_ssl_profile_printf(obj);

        scans_destroy(obj->scans);

        freq2freq_phasor_destroy(obj->freq2freq_phasor);
        freq2freq_product_destroy(obj->freq2freq_product);
        freq2freq_bandpass_destroy(obj->freq2freq_bandpass);
        freq2freq_interpolate_destroy(obj->freq2freq_interpolate);
        freqs_destroy(obj->phasors);
        freqs_destroy(obj->products);
        freqs_destroy(obj->productsInterp);
        freq2xcorr_destroy(obj->freq2xcorr);
        xcorrs_destroy(obj->xcorrs);
        xcorrs_destroy(obj->xcorrsMax);

        for (iLevel = 0; iLevel < obj->nLevels; iLevel++) {
            aimg_destroy(obj->aimgs[iLevel]);
        }

        free((void *) obj->aimgs);

        xcorr2xcorr_destroy(obj->xcorr2xcorr);

        for (iLevel = 0; iLevel < obj->nLevels; iLevel++) {
            xcorr2aimg_destroy(obj->xcorr2aimg[iLevel]);
        }

        free((void *) obj->xcorr2aimg);

        pots_destroy(obj->pots);

        free((void *) obj);

    }

    int mod_ssl_process(mod_ssl_obj * obj) {

        int rtnValue;
        unsigned int iPot;
        unsigned int iLevel;
        unsigned int iPoint;

        float maxValue;
        unsigned int maxIndex;
        
        int debug = 0;
        struct timespec t_start, t_end;
        double t_profile_pre_xcorr_s;
        double t_profile_gcc_phat_xcorr_s;
        double t_profile_srp_accum_s;
        double t_profile_peak_map_s;
        double t_profile_freq_at_peak_s;
        double t_profile_format_doa_s;

        if (msg_spectra_isZero(obj->in) == 0) {

            if (obj->enabled == 1) {

                t_profile_pre_xcorr_s = 0.0;
                t_profile_gcc_phat_xcorr_s = 0.0;
                t_profile_srp_accum_s = 0.0;
                t_profile_peak_map_s = 0.0;
                t_profile_freq_at_peak_s = 0.0;
                t_profile_format_doa_s = 0.0;

                if (obj->profile_enabled == 0x01) {
                    clock_gettime(CLOCK_MONOTONIC, &t_start);
                }
                    

                freq2freq_phasor_process(obj->freq2freq_phasor, 
                                         obj->in->freqs, 
                                         obj->phasors);

                freq2freq_product_process(obj->freq2freq_product, 
                                          obj->phasors, 
                                          obj->phasors,
                                          obj->scans->pairs,
                                          obj->products);        

                mod_ssl_reload_bandpass(obj);
                freq2freq_bandpass_process(obj->freq2freq_bandpass,
                                          obj->products,
                                          obj->products);

                freq2freq_interpolate_process(obj->freq2freq_interpolate,
                                              obj->products,
                                              obj->productsInterp);

                if (obj->profile_enabled == 0x01) {
                    clock_gettime(CLOCK_MONOTONIC, &t_end);
                    t_profile_pre_xcorr_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                    clock_gettime(CLOCK_MONOTONIC, &t_start);
                }
                                              


                freq2xcorr_process(obj->freq2xcorr, 
                                   obj->productsInterp, 
                                   obj->scans->pairs,
                                   obj->xcorrs);

                if (obj->freq2freq_bandpass->lowCut > obj->freq2freq_bandpass->highCut) {
                    memset(obj->pots->array, 0, sizeof(float) * obj->pots->nPots * 4);
                    for (iPot = 0; iPot < obj->pots->nPots; iPot++) {
                        memset(obj->pots->spec_at_peak[iPot], 0, sizeof(float) * obj->halfFrameSize);
                    }
                    pots_copy(obj->out->pots, obj->pots);
                    obj->out->timeStamp = obj->in->timeStamp;
                    return 0;
                }

                if (obj->profile_enabled == 0x01) {
                    clock_gettime(CLOCK_MONOTONIC, &t_end);
                    t_profile_gcc_phat_xcorr_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                }
                                   

                for (iPot = 0; iPot < obj->nPots; iPot++) {
                    
                    if (iPot > 0) {

                        xcorr2xcorr_process_reset(obj->xcorr2xcorr, 
                    	                          obj->scans->tdoas[obj->nLevels-1],
                                                  obj->scans->deltas[obj->nLevels-1],
                                                  obj->scans->pairs,
                                                  maxIndex,
                                                  obj->xcorrs);

                    }

                    maxIndex = 0;

                    if (obj->profile_enabled == 0x01) {
                        clock_gettime(CLOCK_MONOTONIC, &t_start);
                    }

                    for (iLevel = 0; iLevel < obj->nLevels; iLevel++) {

                        xcorr2xcorr_process_max(obj->xcorr2xcorr, 
                	                            obj->xcorrs, 
                	                            obj->scans->tdoas[iLevel],
             	                                obj->scans->deltas[iLevel],
                                                obj->scans->pairs,
                	                            obj->xcorrsMax);

                        xcorr2aimg_process(obj->xcorr2aimg[iLevel],
                    	                   obj->scans->tdoas[iLevel],
                    	                   obj->scans->indexes[iLevel],
                                           obj->scans->spatialindexes[iLevel],
                                           maxIndex,
                    	                   obj->xcorrsMax,
                    	                   obj->aimgs[iLevel]);

                        maxValue = obj->aimgs[iLevel]->array[0];

                        for (iPoint = 0; iPoint < obj->aimgs[iLevel]->aimgSize; iPoint++) {

                            if (obj->aimgs[iLevel]->array[iPoint] > maxValue) {
       
                        	    maxValue = obj->aimgs[iLevel]->array[iPoint];
                        	    maxIndex = iPoint;

                            }

                        }
                       
                    }

                    if (obj->profile_enabled == 0x01) {
                        clock_gettime(CLOCK_MONOTONIC, &t_end);
                        t_profile_srp_accum_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                        clock_gettime(CLOCK_MONOTONIC, &t_start);
                    }
                    

                    obj->pots->array[iPot * 4 + 0] = obj->scans->points[obj->nLevels-1]->array[maxIndex * 3 + 0];
                    obj->pots->array[iPot * 4 + 1] = obj->scans->points[obj->nLevels-1]->array[maxIndex * 3 + 1];
                    obj->pots->array[iPot * 4 + 2] = obj->scans->points[obj->nLevels-1]->array[maxIndex * 3 + 2];
                    obj->pots->array[iPot * 4 + 3] = maxValue * ((float) obj->interpRate);

                    if (obj->profile_enabled == 0x01) {
                        clock_gettime(CLOCK_MONOTONIC, &t_end);
                        t_profile_peak_map_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                        clock_gettime(CLOCK_MONOTONIC, &t_start);
                    }
                  
                   
                    float x = obj->pots->array[iPot * 4 + 0];
                    float y = obj->pots->array[iPot * 4 + 1];
                    float z = obj->pots->array[iPot * 4 + 2];

                        xcorr2true_spectrum_at_peak(
                            obj->fS,
                            obj->in->freqs->array,                              // [nChannels][frameSize] ? channel-level FFT spectra
                            obj->scans->tdoas[obj->nLevels - 1]->array,         // [nPoints * nPairs] ? TDOAs per pair at this pot
                            obj->nPairs,                                        // total mic pairs
                            obj->nChannels,                                     // total microphones
                            obj->frameSize,                               // number of frequency bins
                            maxIndex,                                           // spatial bin index (pot)
                            obj->pots->spec_at_peak[iPot],                       // output buffer for final spectrum
                            x,y,z
                        );

                        if (obj->profile_enabled == 0x01) {
                            clock_gettime(CLOCK_MONOTONIC, &t_end);
                            t_profile_freq_at_peak_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                            clock_gettime(CLOCK_MONOTONIC, &t_start);
                        }
                        
                        pots_copy(obj->out->pots, obj->pots);

                        if (obj->profile_enabled == 0x01) {
                            clock_gettime(CLOCK_MONOTONIC, &t_end);
                            t_profile_format_doa_s += mod_ssl_elapsed_seconds(&t_start, &t_end);
                        }

                        //printf("after xcoor call in ssl");
                        //pots_printf(obj->pots);

                        unsigned int total_nonzero = 0;
                        unsigned int nBuckets = 5;
                        unsigned int bucketSize = obj->frameSizeInterp / nBuckets;

                        //printf("[mod_ssl] spec_at_peak[%u]: frameSizeInterp = %u\n", iPot, obj->frameSizeInterp);

                        for (unsigned int b = 0; b < nBuckets; b++) {
                            unsigned int start = b * bucketSize;
                            unsigned int end = start + bucketSize;

                            unsigned int nonzero_count = 0;
                            float sum = 0.0f;

                            for (unsigned int bin = start; bin < end; bin++) {
                                float val = obj->pots->spec_at_peak[iPot][bin];
                                if (fabsf(val) > 1e-6) nonzero_count++;
                                sum += val;
                            }

                            float avg = sum / bucketSize;
                            total_nonzero += nonzero_count;

                            //printf("  Bucket %u [%uG%u]: nonzero=%u, avg=%.4f\n",
                                   //b, start, end - 1, nonzero_count, avg);
                        }

                       // printf("  Total nonzero bins: %u / %u\n", total_nonzero, obj->frameSizeInterp);


             }

    
                memcpy(obj->out->pots->array, obj->pots->array, sizeof(float) * obj->pots->nPots * 4);

                    if (obj->profile_enabled == 0x01) {
                        obj->profile_frames += 1;
                        obj->profile_pre_xcorr_s += t_profile_pre_xcorr_s;
                        obj->profile_gcc_phat_xcorr_s += t_profile_gcc_phat_xcorr_s;
                        obj->profile_srp_accum_s += t_profile_srp_accum_s;
                        obj->profile_peak_map_s += t_profile_peak_map_s;
                        obj->profile_freq_at_peak_s += t_profile_freq_at_peak_s;
                        obj->profile_format_doa_s += t_profile_format_doa_s;
                    }
  
    
            }
            else {

                pots_zero(obj->out->pots);

            }

            obj->out->timeStamp = obj->in->timeStamp;

            rtnValue = 0;

        }
        else {

            msg_pots_zero(obj->out);

            rtnValue = -1;

        }

        return rtnValue;

    }

    void mod_ssl_connect(mod_ssl_obj * obj, msg_spectra_obj * in,msg_chatak_id_obj* in1, msg_pots_obj * out) {

        obj->in = in;
        obj->in1 = in1;
        obj->out = out;

    }

    void mod_ssl_disconnect(mod_ssl_obj * obj) {

        obj->in = (msg_spectra_obj *) NULL;
        obj->in1 = (msg_chatak_id_obj *) NULL;
        obj->out = (msg_pots_obj *) NULL;

    }

    void mod_ssl_enable(mod_ssl_obj * obj) {

        obj->enabled = 1;

    }

    void mod_ssl_disable(mod_ssl_obj * obj) {

        obj->enabled = 0;

    }

    mod_ssl_cfg * mod_ssl_cfg_construct(void) {

        mod_ssl_cfg * cfg;

        cfg = (mod_ssl_cfg *) malloc(sizeof(mod_ssl_cfg));

        cfg->fS = 0;
        snprintf(cfg->bandpassPath, sizeof(cfg->bandpassPath),
                 "/home/chatak/ChatakGUI/config/bandpass.cfg");

        cfg->mics = (mics_obj *) NULL;
        cfg->samplerate = (samplerate_obj *) NULL;
        cfg->soundspeed = (soundspeed_obj *) NULL;
        cfg->spatialfilters = (spatialfilters_obj *) NULL;
        
        cfg->nLevels = 0;;
        cfg->levels = (unsigned int *) NULL;
        cfg->deltas = (unsigned int *) NULL;
        cfg->nMatches = 0;
        cfg->probMin = 0.0f;
        cfg->nRefinedLevels = 0;
        cfg->nThetas = 0;
        cfg->gainMin = 0.0f;    

        return cfg;

    }

    void mod_ssl_cfg_destroy(mod_ssl_cfg * cfg) {

        if (cfg->mics != NULL) {
            mics_destroy(cfg->mics);
        }

        if (cfg->samplerate != NULL) {
            samplerate_destroy(cfg->samplerate);
        }

        if (cfg->soundspeed != NULL) {
            soundspeed_destroy(cfg->soundspeed);
        }

        if (cfg->spatialfilters != NULL) {
            spatialfilters_destroy(cfg->spatialfilters);
        }

        if (cfg->levels != NULL) {
            free((void *) cfg->levels);
        }

        if (cfg->deltas != NULL) {
            free((void *) cfg->deltas);
        }

        free((void *) cfg);

    }

    void mod_ssl_cfg_printf(const mod_ssl_cfg * cfg) {

        unsigned int iLevel;

        mics_printf(cfg->mics);
        samplerate_printf(cfg->samplerate);
        soundspeed_printf(cfg->soundspeed);
        spatialfilters_printf(cfg->spatialfilters);

        for (iLevel = 0; iLevel < cfg->nLevels; iLevel++) {

            if (iLevel == 0) {

                printf("levels = (%u): level = %u, delta = %d\n", iLevel, cfg->levels[iLevel], cfg->deltas[iLevel]);

            }
            else {

                printf("         (%u): level = %u, delta = %d\n", iLevel, cfg->levels[iLevel], cfg->deltas[iLevel]);

            }


        }

        printf("nMatches = %u\n", cfg->nMatches);        
        printf("probMin = %f\n", cfg->probMin);
        printf("nRefinedLevels = %u\n", cfg->nRefinedLevels);
        printf("nThetas = %u\n", cfg->nThetas);
        printf("gainMin = %f\n", cfg->gainMin);
        printf("bandpassPath = %s\n", cfg->bandpassPath);

    }
