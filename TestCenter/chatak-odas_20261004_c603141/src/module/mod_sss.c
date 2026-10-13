
   /**
    * \file     mod_sss.c
    */
    
    #include <module/mod_sss.h>

    /* ── SSB reverse-shift for YAMNet patch ─────────────────────────────────
     * patch   : float array [nFrames * halfFrameSize] (magnitude spectrum)
     * nFrames : number of time frames (96)
     * H       : halfFrameSize (257)
     * B       : ssbShiftBins (e.g. 64 for 2000 Hz shift with N=512, fS=16000)
     *
     * Reverse: shifted_back[t][b] = patch[t][b + B], zeros at top B bins.
     * Applied in-place.
     */
    static void ssb_unshift_patch(float * patch, unsigned int nFrames,
                                   unsigned int H, unsigned int B) {
        if (B == 0 || B >= H) return;
        unsigned int t, b;
        for (t = 0; t < nFrames; t++) {
            float * row = patch + t * H;
            for (b = 0; b < H - B; b++) {
                row[b] = row[b + B];
            }
            for (b = H - B; b < H; b++) {
                row[b] = 0.0f;
            }
        }
    }
    /* ───────────────────────────────────────────────────────────────── */

    #include <stdio.h>

    FILE * fidTmp1;
    FILE * fidTmp2;

    mod_sss_obj * mod_sss_construct(const mod_sss_cfg * mod_sss_config, const msg_tracks_cfg * msg_tracks_config, const msg_spectra_cfg * msg_spectra_config) {

        mod_sss_obj * obj;
        
        obj = (mod_sss_obj *) malloc(sizeof(mod_sss_obj));

        obj->nSeps = msg_tracks_config->nTracks;
        obj->nChannels = msg_spectra_config->nChannels;
        obj->halfFrameSize = msg_spectra_config->halfFrameSize;
        obj->mode_sep = mod_sss_config->mode_sep;
        obj->mode_pf = mod_sss_config->mode_pf;

        /* Pre-compute SSB reverse-shift in bins */
        if (mod_sss_config->ssbShiftHz > 0 && msg_spectra_config->fS > 0) {
            unsigned int frameSize = 2 * (msg_spectra_config->halfFrameSize - 1);
            obj->ssbShiftBins = (unsigned int)(
                (float)mod_sss_config->ssbShiftHz * (float)frameSize / (float)msg_spectra_config->fS + 0.5f);
        } else {
            obj->ssbShiftBins = 0;
        }

        obj->nFramesPerTrack = 96;  // default FIFO length
        obj->trackSpectra = malloc(sizeof(sss_track_spectrum_obj) * obj->nSeps);

        for (unsigned int iTrack = 0; iTrack < obj->nSeps; iTrack++) {
            obj->trackSpectra[iTrack].buffer = malloc(sizeof(float*) * obj->nFramesPerTrack);
            for (unsigned int f = 0; f < obj->nFramesPerTrack; f++) {
                obj->trackSpectra[iTrack].buffer[f] = malloc(sizeof(float) * obj->halfFrameSize);
                memset(obj->trackSpectra[iTrack].buffer[f], 0x00, sizeof(float) * obj->halfFrameSize);
            }
            obj->trackSpectra[iTrack].count = 0;
            obj->trackSpectra[iTrack].id = 0;
            obj->trackSpectra[iTrack].type = 'I';
            obj->trackSpectra[iTrack].lastFrameSeen = 0;
        }


        obj->sep_ds_beampatterns_mics = (beampatterns_obj *) NULL;
        obj->sep_ds_steers = (steers_obj *) NULL;
        obj->sep_ds_gains = (gains_obj *) NULL;
        obj->sep_ds_masks = (masks_obj *) NULL;
        obj->sep_ds_tracksPrev = (tracks_obj *) NULL;
        obj->sep_ds_tracksNow = (tracks_obj *) NULL;
        obj->sep_ds_track2gain = (track2gain_obj *) NULL;
        obj->sep_ds_gain2mask = (gain2mask_obj *) NULL;
        obj->sep_ds_track2steer = (track2steer_obj *) NULL;
        obj->sep_ds_steer2demixing = (steer2demixing_ds_obj *) NULL;
        obj->sep_ds_demixingsPrev = (demixings_obj *) NULL;
        obj->sep_ds_demixingsNow = (demixings_obj *) NULL;
        obj->sep_ds_demixing2freq = (demixing2freq_obj *) NULL;

        obj->sep_gss_beampatterns_mics = (beampatterns_obj *) NULL;
        obj->sep_gss_steers = (steers_obj *) NULL;
        obj->sep_gss_gains = (gains_obj *) NULL;
        obj->sep_gss_masks = (masks_obj *) NULL;
        obj->sep_gss_tracksPrev = (tracks_obj *) NULL;
        obj->sep_gss_tracksNow = (tracks_obj *) NULL;
        obj->sep_gss_track2gain = (track2gain_obj *) NULL;
        obj->sep_gss_gain2mask = (gain2mask_obj *) NULL;
        obj->sep_gss_track2steer = (track2steer_obj *) NULL;
        obj->sep_gss_steer2demixing = (steer2demixing_gss_obj *) NULL;
        obj->sep_gss_demixingsPrev = (demixings_obj *) NULL;
        obj->sep_gss_demixingsNow = (demixings_obj *) NULL;
        obj->sep_gss_demixing2freq = (demixing2freq_obj *) NULL;        

        switch(obj->mode_sep) {

            case 'd':

                obj->sep_ds_beampatterns_mics = directivity_beampattern_mics(mod_sss_config->mics, 
                                                                             mod_sss_config->nThetas);

                obj->sep_ds_steers = steers_construct_zero(msg_spectra_config->halfFrameSize, 
                                                           msg_tracks_config->nTracks, 
                                                           msg_spectra_config->nChannels);

                obj->sep_ds_gains = gains_construct_zero(msg_tracks_config->nTracks,
                                                         msg_spectra_config->nChannels);

                obj->sep_ds_masks = masks_construct_zero(msg_tracks_config->nTracks, 
                                                         msg_spectra_config->nChannels);

                obj->sep_ds_tracksPrev = tracks_construct_zero(msg_tracks_config->nTracks);

                obj->sep_ds_tracksNow = tracks_construct_zero(msg_tracks_config->nTracks);

                obj->sep_ds_track2gain = track2gain_construct_zero(msg_tracks_config->nTracks,
                                                                   msg_spectra_config->nChannels,
                                                                   mod_sss_config->mics->direction);

                obj->sep_ds_gain2mask = gain2mask_construct_zero(msg_tracks_config->nTracks,
                                                                 msg_spectra_config->nChannels,
                                                                 mod_sss_config->gainMin);

                obj->sep_ds_track2steer = track2steer_construct_zero(msg_tracks_config->nTracks, 
                                                                     msg_spectra_config->nChannels, 
                                                                     msg_spectra_config->halfFrameSize, 
                                                                     mod_sss_config->soundspeed->mu,
                                                                     mod_sss_config->samplerate->mu,
                                                                     mod_sss_config->mics->mu);

                obj->sep_ds_steer2demixing = steer2demixing_ds_construct_zero(msg_tracks_config->nTracks, 
                                                                              msg_spectra_config->nChannels, 
                                                                              msg_spectra_config->halfFrameSize, 
                                                                              mod_sss_config->epsilon);

                obj->sep_ds_demixingsPrev = demixings_construct_zero(msg_spectra_config->halfFrameSize, 
                                                                     msg_tracks_config->nTracks, 
                                                                     msg_spectra_config->nChannels);

                obj->sep_ds_demixingsNow = demixings_construct_zero(msg_spectra_config->halfFrameSize, 
                                                                    msg_tracks_config->nTracks, 
                                                                    msg_spectra_config->nChannels);

                obj->sep_ds_demixing2freq = demixing2freq_construct_zero(msg_tracks_config->nTracks,
                                                                         msg_spectra_config->nChannels,
                                                                         msg_spectra_config->halfFrameSize);

            break;

            case 'g':

                obj->sep_gss_beampatterns_mics = directivity_beampattern_mics(mod_sss_config->mics, 
                                                                              mod_sss_config->nThetas);

                obj->sep_gss_steers = steers_construct_zero(msg_spectra_config->halfFrameSize, 
                                                            msg_tracks_config->nTracks, 
                                                            msg_spectra_config->nChannels);

                obj->sep_gss_gains = gains_construct_zero(msg_tracks_config->nTracks,
                                                          msg_spectra_config->nChannels);

                obj->sep_gss_masks = masks_construct_zero(msg_tracks_config->nTracks, 
                                                          msg_spectra_config->nChannels);

                obj->sep_gss_tracksPrev = tracks_construct_zero(msg_tracks_config->nTracks);

                obj->sep_gss_tracksNow = tracks_construct_zero(msg_tracks_config->nTracks);

                obj->sep_gss_track2gain = track2gain_construct_zero(msg_tracks_config->nTracks,
                                                                    msg_spectra_config->nChannels,
                                                                    mod_sss_config->mics->direction);

                obj->sep_gss_gain2mask = gain2mask_construct_zero(msg_tracks_config->nTracks,
                                                                  msg_spectra_config->nChannels,
                                                                  mod_sss_config->gainMin);

                obj->sep_gss_track2steer = track2steer_construct_zero(msg_tracks_config->nTracks, 
                                                                      msg_spectra_config->nChannels, 
                                                                      msg_spectra_config->halfFrameSize, 
                                                                      mod_sss_config->soundspeed->mu,
                                                                      mod_sss_config->samplerate->mu,
                                                                      mod_sss_config->mics->mu);

                obj->sep_gss_steer2demixing = steer2demixing_gss_construct_zero(msg_tracks_config->nTracks, 
                                                                                msg_spectra_config->nChannels, 
                                                                                msg_spectra_config->halfFrameSize, 
                                                                                mod_sss_config->sep_gss_mu, 
                                                                                mod_sss_config->sep_gss_lambda,
                                                                                mod_sss_config->epsilon);

                obj->sep_gss_demixingsPrev = demixings_construct_zero(msg_spectra_config->halfFrameSize, 
                                                                      msg_tracks_config->nTracks, 
                                                                      msg_spectra_config->nChannels);

                obj->sep_gss_demixingsNow = demixings_construct_zero(msg_spectra_config->halfFrameSize, 
                                                                     msg_tracks_config->nTracks, 
                                                                     msg_spectra_config->nChannels);

                obj->sep_gss_demixing2freq = demixing2freq_construct_zero(msg_tracks_config->nTracks,
                                                                          msg_spectra_config->nChannels,
                                                                          msg_spectra_config->halfFrameSize);                

            break;

            default:

                printf("Invalid separation method.\n");
                exit(EXIT_FAILURE);

            break;

        }

        obj->sep_demixings = demixings_construct_zero(msg_spectra_config->halfFrameSize,
                                                      msg_tracks_config->nTracks,
                                                      msg_spectra_config->nChannels);

        obj->sep_masks = masks_construct_zero(msg_tracks_config->nTracks, 
                                              msg_spectra_config->nChannels);

        obj->pf_ms_freq2env = (freq2env_obj *) NULL;
        obj->pf_ms_noisys = (envs_obj *) NULL;
        obj->pf_ms_demixing2env = (demixing2env_obj *) NULL;
        obj->pf_ms_noisesEst = (envs_obj *) NULL;
        obj->pf_ms_env2env_mcra = (env2env_mcra_obj *) NULL;
        obj->pf_ms_noisesSep = (envs_obj *) NULL;
        obj->pf_ms_env2env_interf = (env2env_interf_obj *) NULL;
        obj->pf_ms_interfs = (envs_obj *) NULL;
        obj->pf_ms_env2env_gainspeech = (env2env_gainspeech_obj *) NULL;
        obj->pf_ms_gainspeeches = (envs_obj *) NULL;
        obj->pf_ms_snrs = (envs_obj *) NULL;
        obj->pf_ms_vs = (envs_obj *) NULL;
        obj->pf_ms_env2env_probspeech = (env2env_probspeech_obj *) NULL;
        obj->pf_ms_probspeeches = (envs_obj *) NULL;
        obj->pf_ms_env2env_gainall = (env2env_gainall_obj *) NULL;
        obj->pf_ms_gainalls = (envs_obj *) NULL;
        obj->pf_ms_freq2freq_gain = (freq2freq_gain_obj *) NULL;

        obj->pf_ss_freq2env_channels = (freq2env_obj *) NULL;
        obj->pf_ss_env_channels = (envs_obj *) NULL;
        obj->pf_ss_demixing2env = (demixing2env_obj *) NULL;
        obj->pf_ss_env_channelseps = (envs_obj *) NULL;
        obj->pf_ss_freq2env_seps = (freq2env_obj *) NULL;
        obj->pf_ss_env_seps = (envs_obj *) NULL;
        obj->pf_ss_env2env_gainratio = (env2env_gainratio_obj *) NULL;
        obj->pf_ss_gainratio = (envs_obj *) NULL;
        obj->pf_ss_freq2freq_gain = (freq2freq_gain_obj *) NULL;

        switch(obj->mode_pf) {

            case 'm':

                obj->pf_ms_freq2env = freq2env_construct_zero(msg_spectra_config->halfFrameSize);

                obj->pf_ms_noisys = envs_construct_zero(msg_tracks_config->nTracks,
                                                        msg_spectra_config->halfFrameSize);

                obj->pf_ms_demixing2env = demixing2env_construct_zero(msg_tracks_config->nTracks, 
                                                                      msg_spectra_config->nChannels, 
                                                                      msg_spectra_config->halfFrameSize);

                obj->pf_ms_noisesEst = envs_construct_zero(msg_tracks_config->nTracks, 
                                                           msg_spectra_config->halfFrameSize);

                obj->pf_ms_env2env_mcra = env2env_mcra_construct_zero(msg_tracks_config->nTracks, 
                                                                      msg_spectra_config->halfFrameSize,
                                                                      mod_sss_config->pf_ms_bSize, 
                                                                      mod_sss_config->pf_ms_alphaS, 
                                                                      mod_sss_config->pf_ms_L, 
                                                                      mod_sss_config->pf_ms_delta, 
                                                                      mod_sss_config->pf_ms_alphaD);

                obj->pf_ms_noisesSep = envs_construct_zero(msg_tracks_config->nTracks, 
                                                           msg_spectra_config->halfFrameSize);

                obj->pf_ms_env2env_interf = env2env_interf_construct_zero(msg_tracks_config->nTracks, 
                                                                          msg_spectra_config->halfFrameSize, 
                                                                          mod_sss_config->pf_ms_eta,
                                                                          mod_sss_config->pf_ms_alphaZ);

                obj->pf_ms_interfs = envs_construct_zero(msg_tracks_config->nTracks,
                                                         msg_spectra_config->halfFrameSize);

                obj->pf_ms_env2env_gainspeech = env2env_gainspeech_construct_zero(msg_tracks_config->nTracks,
                                                                                  msg_spectra_config->halfFrameSize,
                                                                                  mod_sss_config->pf_ms_alphaPmin,
                                                                                  mod_sss_config->epsilon);

                obj->pf_ms_gainspeeches = envs_construct_zero(msg_tracks_config->nTracks,
                                                              msg_spectra_config->halfFrameSize);

                obj->pf_ms_snrs = envs_construct_zero(msg_tracks_config->nTracks,
                                                      msg_spectra_config->halfFrameSize);

                obj->pf_ms_vs = envs_construct_zero(msg_tracks_config->nTracks,
                                                    msg_spectra_config->halfFrameSize);

                obj->pf_ms_env2env_probspeech = env2env_probspeech_construct_zero(msg_tracks_config->nTracks,
                                                                                  msg_spectra_config->halfFrameSize, 
                                                                                  mod_sss_config->pf_ms_thetaWin, 
                                                                                  mod_sss_config->pf_ms_alphaWin,
                                                                                  mod_sss_config->pf_ms_maxAbsenceProb, 
                                                                                  mod_sss_config->pf_ms_Gmin, 
                                                                                  mod_sss_config->pf_ms_winSizeLocal, 
                                                                                  mod_sss_config->pf_ms_winSizeGlobal, 
                                                                                  mod_sss_config->pf_ms_winSizeFrame);

                obj->pf_ms_probspeeches = envs_construct_zero(msg_tracks_config->nTracks,
                                                              msg_spectra_config->halfFrameSize);

                obj->pf_ms_env2env_gainall = env2env_gainall_construct_zero(msg_tracks_config->nTracks,
                                                                            msg_spectra_config->halfFrameSize,
                                                                            mod_sss_config->pf_ms_Gmin);

                obj->pf_ms_gainalls = envs_construct_zero(msg_tracks_config->nTracks,
                                                          msg_spectra_config->halfFrameSize);

                obj->pf_ms_freq2freq_gain = freq2freq_gain_construct_zero(msg_spectra_config->halfFrameSize);

            break;

            case 's':

                obj->pf_ss_freq2env_channels = freq2env_construct_zero(msg_spectra_config->halfFrameSize);
                
                obj->pf_ss_env_channels = envs_construct_zero(msg_spectra_config->nChannels,
                                                              msg_spectra_config->halfFrameSize);

                obj->pf_ss_demixing2env = demixing2env_construct_zero(msg_tracks_config->nTracks, 
                                                                      msg_spectra_config->nChannels, 
                                                                      msg_spectra_config->halfFrameSize);

                obj->pf_ss_env_channelseps = envs_construct_zero(msg_tracks_config->nTracks, 
                                                                 msg_spectra_config->halfFrameSize);

                obj->pf_ss_freq2env_seps = freq2env_construct_zero(msg_spectra_config->halfFrameSize);

                obj->pf_ss_env_seps = envs_construct_zero(msg_tracks_config->nTracks,
                                                          msg_spectra_config->halfFrameSize);

                obj->pf_ss_env2env_gainratio = env2env_gainratio_construct_zero(msg_spectra_config->nChannels,
                                                                                msg_tracks_config->nTracks, 
                                                                                msg_spectra_config->halfFrameSize, 
                                                                                mod_sss_config->pf_ss_Gmin, 
                                                                                mod_sss_config->pf_ss_Gmid, 
                                                                                mod_sss_config->pf_ss_Gslope,
                                                                                mod_sss_config->epsilon);

                obj->pf_ss_gainratio = envs_construct_zero(msg_tracks_config->nTracks,
                                                           msg_spectra_config->halfFrameSize);

                obj->pf_ss_freq2freq_gain = freq2freq_gain_construct_zero(msg_spectra_config->halfFrameSize);

            break;

            default:

                printf("Invalid post-filtering method.\n");
                exit(EXIT_FAILURE);

            break;

        }

        obj->in1 = (msg_spectra_obj *) NULL;
        obj->in2 = (msg_powers_obj *) NULL;
        obj->in3 = (msg_tracks_obj *) NULL;
        obj->out1 = (msg_spectra_obj *) NULL;
        obj->out2 = (msg_spectra_obj *) NULL;

        obj->enabled = 0;

        // Initialize YAMNet classifier (same as mod_sst)
    if (mod_sss_config->model_path != NULL) {
        char tflite_path[512], csv_path[512];
        snprintf(tflite_path, sizeof(tflite_path), "%s/yamnet_core.tflite", mod_sss_config->model_path);
        snprintf(csv_path,    sizeof(csv_path),    "%s/yamnet_class_map.csv", mod_sss_config->model_path);
        obj->yamnet = yamnet_create(tflite_path, csv_path);
    } else {
        obj->yamnet = NULL;
    }

    if (obj->yamnet == NULL) {
        printf("Failed to initialize YAMNet in mod_sss (model_path not set or files missing)\n");
        exit(EXIT_FAILURE);
    }


        return obj;

    }

    void mod_sss_destroy(mod_sss_obj * obj) {

        // --- Free trackSpectra rolling buffers ---
            if (obj->trackSpectra != NULL) {
                for (unsigned int iTrack = 0; iTrack < obj->nSeps; iTrack++) {
                    if (obj->trackSpectra[iTrack].buffer != NULL) {
                        for (unsigned int f = 0; f < obj->nFramesPerTrack; f++) {
                            free(obj->trackSpectra[iTrack].buffer[f]);  // free each frame
                        }
                        free(obj->trackSpectra[iTrack].buffer);        // free buffer array
                    }
                }
                free(obj->trackSpectra);  // free the trackSpectra array itself
                obj->trackSpectra = NULL;
            }

            // --- Free YAMNet handle if present ---
            if (obj->yamnet) {
                yamnet_destroy(obj->yamnet);
                obj->yamnet = NULL;
            }


        switch(obj->mode_sep) {

            case 'd':

                beampatterns_destroy(obj->sep_ds_beampatterns_mics);
                steers_destroy(obj->sep_ds_steers);
                gains_destroy(obj->sep_ds_gains);
                masks_destroy(obj->sep_ds_masks);
                tracks_destroy(obj->sep_ds_tracksPrev);
                tracks_destroy(obj->sep_ds_tracksNow);
                track2gain_destroy(obj->sep_ds_track2gain);
                gain2mask_destroy(obj->sep_ds_gain2mask);
                track2steer_destroy(obj->sep_ds_track2steer);
                steer2demixing_ds_destroy(obj->sep_ds_steer2demixing);
                demixings_destroy(obj->sep_ds_demixingsPrev);
                demixings_destroy(obj->sep_ds_demixingsNow);
                demixing2freq_destroy(obj->sep_ds_demixing2freq);

            break;

            case 'g':

                beampatterns_destroy(obj->sep_gss_beampatterns_mics);
                steers_destroy(obj->sep_gss_steers);
                gains_destroy(obj->sep_gss_gains);
                masks_destroy(obj->sep_gss_masks);
                tracks_destroy(obj->sep_gss_tracksPrev);
                tracks_destroy(obj->sep_gss_tracksNow);
                track2gain_destroy(obj->sep_gss_track2gain);
                gain2mask_destroy(obj->sep_gss_gain2mask);
                track2steer_destroy(obj->sep_gss_track2steer);
                steer2demixing_gss_destroy(obj->sep_gss_steer2demixing);
                demixings_destroy(obj->sep_gss_demixingsPrev);
                demixings_destroy(obj->sep_gss_demixingsNow);
                demixing2freq_destroy(obj->sep_gss_demixing2freq);

            break;

            default:

                printf("Invalid separation method.\n");
                exit(EXIT_FAILURE);

            break;

        }

        demixings_destroy(obj->sep_demixings);
        masks_destroy(obj->sep_masks);

        switch(obj->mode_pf) {

            case 'm':

                freq2env_destroy(obj->pf_ms_freq2env);
                envs_destroy(obj->pf_ms_noisys);
                demixing2env_destroy(obj->pf_ms_demixing2env);
                envs_destroy(obj->pf_ms_noisesEst);
                env2env_mcra_destroy(obj->pf_ms_env2env_mcra);
                envs_destroy(obj->pf_ms_noisesSep);
                env2env_interf_destroy(obj->pf_ms_env2env_interf);
                envs_destroy(obj->pf_ms_interfs);
                env2env_gainspeech_destroy(obj->pf_ms_env2env_gainspeech);
                envs_destroy(obj->pf_ms_gainspeeches);
                envs_destroy(obj->pf_ms_snrs);
                envs_destroy(obj->pf_ms_vs);
                env2env_probspeech_destroy(obj->pf_ms_env2env_probspeech);
                envs_destroy(obj->pf_ms_probspeeches);
                env2env_gainall_destroy(obj->pf_ms_env2env_gainall);
                envs_destroy(obj->pf_ms_gainalls);
                freq2freq_gain_destroy(obj->pf_ms_freq2freq_gain);

            break;

            case 's':

                freq2env_destroy(obj->pf_ss_freq2env_channels);
                envs_destroy(obj->pf_ss_env_channels);
                demixing2env_destroy(obj->pf_ss_demixing2env);
                envs_destroy(obj->pf_ss_env_channelseps);
                freq2env_destroy(obj->pf_ss_freq2env_seps);
                envs_destroy(obj->pf_ss_env_seps);
                env2env_gainratio_destroy(obj->pf_ss_env2env_gainratio);
                envs_destroy(obj->pf_ss_gainratio);
                freq2freq_gain_destroy(obj->pf_ss_freq2freq_gain);

            break;

            default:

                printf("Invalid post-filtering method.\n");
                exit(EXIT_FAILURE);

            break;

        }

        if (obj->yamnet) {
        yamnet_destroy(obj->yamnet);
        obj->yamnet = NULL;
    }


        free((void *) obj);

    }

    int mod_sss_process(mod_sss_obj * obj) {

        int rtnValue1;
        int rtnValue2;
        int rtnValue;

        switch(obj->mode_sep) {

            case 'd':

                rtnValue1 = mod_sss_process_dds(obj);

            break;

            case 'g':

                rtnValue1 = mod_sss_process_dgss(obj);

            break;

            default:

                printf("Invalid separation method.\n");
                exit(EXIT_FAILURE);

            break;

        }

        switch(obj->mode_pf) {

            case 'm':

                rtnValue2 = mod_sss_process_mspf(obj);

            break;

            case 's':
            
                rtnValue2 = mod_sss_process_sspf(obj);

            break;

            default:

                printf("Invalid post-filtering method.\n");
                exit(EXIT_FAILURE);

            break;

        }        

        if (rtnValue1 != rtnValue2) {
            printf("Return value mismatch.\n");
            exit(EXIT_FAILURE);
        }
        else {
            rtnValue = rtnValue1;
        }

        return rtnValue;

    }

    int mod_sss_process_dds(mod_sss_obj * obj) {

        int rtnValue;

        if ((obj->in1->timeStamp != obj->in2->timeStamp) ||
            (obj->in1->timeStamp != obj->in3->timeStamp)) {

            printf("Time stamp mismatch.\n");
            exit(EXIT_FAILURE);

        }

        if (msg_spectra_isZero(obj->in1) == 0) {

            if (obj->enabled == 1) {

                tracks_copy(obj->sep_ds_tracksPrev,
                            obj->sep_ds_tracksNow);

                tracks_copy(obj->sep_ds_tracksNow,
                            obj->in3->tracks);

                track2gain_process(obj->sep_ds_track2gain, 
                                   obj->sep_ds_beampatterns_mics, 
                                   obj->sep_ds_tracksNow,
                                   obj->sep_ds_gains);
  
                gain2mask_process(obj->sep_ds_gain2mask, 
                                  obj->sep_ds_gains, 
                                  obj->sep_ds_masks);

                track2steer_process(obj->sep_ds_track2steer, 
                                    obj->sep_ds_tracksNow,
                                    obj->sep_ds_gains,
                                    obj->sep_ds_masks,
                                    obj->sep_ds_steers);

                demixings_copy(obj->sep_ds_demixingsPrev, 
                               obj->sep_ds_demixingsNow);

                steer2demixing_ds_process(obj->sep_ds_steer2demixing, 
                                          obj->sep_ds_tracksNow,
                                          obj->sep_ds_steers, 
                                          obj->sep_ds_masks, 
                                          obj->sep_ds_demixingsNow);

                demixing2freq_process(obj->sep_ds_demixing2freq, 
                                      obj->sep_ds_tracksNow,
                                      obj->sep_ds_demixingsNow, 
                                      obj->sep_ds_masks, 
                                      obj->in1->freqs, 
                                      obj->out1->freqs);

                demixings_copy(obj->sep_demixings,
                               obj->sep_ds_demixingsNow);

                masks_copy(obj->sep_masks,
                           obj->sep_ds_masks);
                
                // Push all separated tracks into FIFO + classify
                //push_track_to_buffer(obj, obj->in3, 1 /*debug*/);

            }
            else {

                masks_zero(obj->sep_masks);

            }

            obj->out1->timeStamp = obj->in1->timeStamp;
            
            rtnValue = 0;

        }
        else {

            msg_spectra_zero(obj->out1);

            rtnValue = -1;

        }

        return rtnValue;        

    }

    int mod_sss_process_dgss(mod_sss_obj * obj) {

        int rtnValue;

        if ((obj->in1->timeStamp != obj->in2->timeStamp) ||
            (obj->in1->timeStamp != obj->in3->timeStamp)) {

            printf("Time stamp mismatch.\n");
            exit(EXIT_FAILURE);

        }

        if (msg_spectra_isZero(obj->in1) == 0) {

            if (obj->enabled == 1) {
  
                tracks_copy(obj->sep_gss_tracksPrev,
                            obj->sep_gss_tracksNow);

                tracks_copy(obj->sep_gss_tracksNow,
                            obj->in3->tracks);

                track2gain_process(obj->sep_gss_track2gain, 
                                   obj->sep_gss_beampatterns_mics,
                                   obj->sep_gss_tracksNow,
                                   obj->sep_gss_gains);

                gain2mask_process(obj->sep_gss_gain2mask, 
                                  obj->sep_gss_gains, 
                                  obj->sep_gss_masks);

                track2steer_process(obj->sep_gss_track2steer, 
                                    obj->sep_gss_tracksNow,
                                    obj->sep_gss_gains,
                                    obj->sep_gss_masks,
                                    obj->sep_gss_steers);

                demixings_copy(obj->sep_gss_demixingsPrev,
                               obj->sep_gss_demixingsNow);

                steer2demixing_gss_process(obj->sep_gss_steer2demixing, 
                                           obj->sep_gss_tracksPrev, 
                                           obj->sep_gss_tracksNow, 
                                           obj->sep_gss_steers, 
                                           obj->sep_gss_masks, 
                                           obj->in1->freqs,
                                           obj->sep_gss_demixingsPrev,
                                           obj->sep_gss_demixingsNow);

                demixing2freq_process(obj->sep_gss_demixing2freq, 
                                      obj->sep_gss_tracksNow,
                                      obj->sep_gss_demixingsNow, 
                                      obj->sep_gss_masks, 
                                      obj->in1->freqs, 
                                      obj->out1->freqs);        

                demixings_copy(obj->sep_demixings,
                               obj->sep_gss_demixingsNow);

                masks_copy(obj->sep_masks,
                           obj->sep_gss_masks);
                
                // Push all separated tracks into FIFO + classify
                //push_track_to_buffer(obj, obj->in3, 1 /*debug*/);
              
            }
            else {

                masks_zero(obj->sep_masks);

            }          
          
            obj->out1->timeStamp = obj->in1->timeStamp;
            
            rtnValue = 0;

        }
        else {

            msg_spectra_zero(obj->out1);

            rtnValue = -1;

        }

        return rtnValue;       

    }

    int mod_sss_process_mspf(mod_sss_obj * obj) {

        int rtnValue;

        if ((obj->in1->timeStamp != obj->in2->timeStamp) ||
            (obj->in1->timeStamp != obj->in3->timeStamp)) {

            printf("Time stamp mismatch.\n");
            exit(EXIT_FAILURE);

        }

        if (msg_spectra_isZero(obj->in1) == 0) {

            if (obj->enabled == 1) {

                freq2env_process(obj->pf_ms_freq2env,
                                 obj->out1->freqs,
                                 obj->pf_ms_noisys);

                demixing2env_process(obj->pf_ms_demixing2env,
                                     obj->in3->tracks, 
                                     obj->sep_demixings, 
                                     obj->sep_masks, 
                                     obj->in2->envs,
                                     obj->pf_ms_noisesEst);

                env2env_mcra_process(obj->pf_ms_env2env_mcra, 
                                     obj->in3->tracks, 
                                     obj->pf_ms_noisys, 
                                     obj->pf_ms_noisesEst,
                                     obj->pf_ms_noisesSep);

                env2env_interf_process(obj->pf_ms_env2env_interf, 
                                       obj->in3->tracks, 
                                       obj->pf_ms_noisys, 
                                       obj->pf_ms_noisesSep, 
                                       obj->pf_ms_interfs);           

                env2env_gainspeech_process(obj->pf_ms_env2env_gainspeech, 
                                           obj->in3->tracks, 
                                           obj->pf_ms_noisys, 
                                           obj->pf_ms_interfs, 
                                           obj->pf_ms_gainspeeches, 
                                           obj->pf_ms_snrs, 
                                           obj->pf_ms_vs);

                env2env_probspeech_process(obj->pf_ms_env2env_probspeech, 
                                           obj->in3->tracks,
                                           obj->pf_ms_snrs, 
                                           obj->pf_ms_vs, 
                                           obj->pf_ms_probspeeches);

                env2env_gainall_process(obj->pf_ms_env2env_gainall, 
                                        obj->in3->tracks, 
                                        obj->pf_ms_gainspeeches, 
                                        obj->pf_ms_probspeeches, 
                                        obj->pf_ms_gainalls);

                freq2freq_gain_process(obj->pf_ms_freq2freq_gain, 
                                       obj->out1->freqs, 
                                       obj->pf_ms_gainalls,
                                       obj->out2->freqs);

            }
            else {

                freqs_zero(obj->out2->freqs);

            }

            obj->out2->timeStamp = obj->in1->timeStamp;

            rtnValue = 0;

        }
        else {

            msg_spectra_zero(obj->out2);

            rtnValue = -1;

        }

        return rtnValue;  

    }

    int mod_sss_process_sspf(mod_sss_obj * obj) {

        int rtnValue;

        if ((obj->in1->timeStamp != obj->in2->timeStamp) ||
            (obj->in1->timeStamp != obj->in3->timeStamp)) {

            printf("Time stamp mismatch.\n");
            exit(EXIT_FAILURE);

        }

        if (msg_spectra_isZero(obj->in1) == 0) {
 
            if (obj->enabled == 1) {

                freq2env_process(obj->pf_ss_freq2env_channels,
                                 obj->in1->freqs,
                                 obj->pf_ss_env_channels);

                demixing2env_process(obj->pf_ss_demixing2env,
                                     obj->in3->tracks, 
                                     obj->sep_demixings, 
                                     obj->sep_masks, 
                                     obj->pf_ss_env_channels,
                                     obj->pf_ss_env_channelseps);   

                freq2env_process(obj->pf_ss_freq2env_seps,
                                 obj->out1->freqs,
                                 obj->pf_ss_env_seps);         

                env2env_gainratio_process(obj->pf_ss_env2env_gainratio, 
                                          obj->in3->tracks, 
                                          obj->pf_ss_env_seps, 
                                          obj->pf_ss_env_channelseps,
                                          obj->pf_ss_gainratio);

                freq2freq_gain_process(obj->pf_ss_freq2freq_gain, 
                                       obj->out1->freqs, 
                                       obj->pf_ss_gainratio,
                                       obj->out2->freqs);

            }
            else {

                freqs_zero(obj->out2->freqs);

            }

            obj->out2->timeStamp = obj->in1->timeStamp;

            rtnValue = 0;

        }
        else {

            msg_spectra_zero(obj->out2);

            rtnValue = -1;

        }

        return rtnValue;

    }

    void mod_sss_connect(mod_sss_obj * obj, msg_spectra_obj * in1, msg_powers_obj * in2, msg_tracks_obj * in3, msg_spectra_obj * out1, msg_spectra_obj * out2) {

        obj->in1 = in1;
        obj->in2 = in2;
        obj->in3 = in3;
        obj->out1 = out1;
        obj->out2 = out2;

    }

    void mod_sss_disconnect(mod_sss_obj * obj) {

        obj->in1 = (msg_spectra_obj *) NULL;
        obj->in2 = (msg_powers_obj *) NULL;
        obj->in3 = (msg_tracks_obj *) NULL;
        obj->out1 = (msg_spectra_obj *) NULL;
        obj->out2 = (msg_spectra_obj *) NULL;

    }

    void mod_sss_enable(mod_sss_obj * obj) {

        obj->enabled = 1;

    }

    void mod_sss_disable(mod_sss_obj * obj) {

        obj->enabled = 0;

    }

    mod_sss_cfg * mod_sss_cfg_construct(void) {

        mod_sss_cfg * cfg;

        cfg = (mod_sss_cfg *) malloc(sizeof(mod_sss_cfg));

        cfg->mode_sep = 0x00;
        cfg->mode_pf = 0x00;

        cfg->nThetas = 0;
        cfg->gainMin = 0.0f;

        cfg->mics = (mics_obj *) NULL;
        cfg->samplerate = (samplerate_obj *) NULL;
        cfg->soundspeed = (soundspeed_obj *) NULL;

        cfg->ssbShiftHz = 0;

        return cfg;

    }

    void mod_sss_cfg_destroy(mod_sss_cfg * cfg) {

        if (cfg->mics != NULL) {
            mics_destroy(cfg->mics);
        }

        if (cfg->samplerate != NULL) {
            samplerate_destroy(cfg->samplerate);
        }

        if (cfg->soundspeed != NULL) {
            soundspeed_destroy(cfg->soundspeed);
        }

        free((void *) cfg);

    }

    void mod_sss_cfg_printf(const mod_sss_cfg * cfg) {
        
        // Empty

    }

    /*
void push_track_to_buffer(mod_sss_obj* obj,
                          msg_tracks_obj* msg_tracks,
                          int debug) {
    tracks_obj* tracks = msg_tracks->tracks;   // unwrap the message

    for (unsigned int iTrack = 0; iTrack < tracks->nTracks; iTrack++) {
        unsigned long long trackID = tracks->ids[iTrack];
        if (trackID == 0) continue;

        // FIFO index (circular buffer)
        unsigned int idx = obj->trackSpectra[iTrack].count % obj->nFramesPerTrack;

        // Copy separated spectrum (already float magnitudes)
        if (obj->out1 && obj->out1->freqs && obj->out1->freqs->array[iTrack]) {
            memcpy(obj->trackSpectra[iTrack].buffer[idx],
                   obj->out1->freqs->array[iTrack],
                   sizeof(float) * obj->halfFrameSize);
        }

        // Update metadata
        obj->trackSpectra[iTrack].count++;
        obj->trackSpectra[iTrack].id = trackID;
        obj->trackSpectra[iTrack].lastFrameSeen = msg_tracks->timeStamp;

        unsigned int count = obj->trackSpectra[iTrack].count;

        // Trigger classification only when buffer is truly full (96 frames), with overlap
        if (count >= obj->nFramesPerTrack &&
            ((count == obj->nFramesPerTrack) || ((count - obj->nFramesPerTrack) % 48 == 0))) {

            if (debug) {
                printf("[SSS DEBUG] Track %llu ready for classification (count=%u)\n",
                       trackID, count);
            }

            // Flatten last nFramesPerTrack frames into contiguous patch
            unsigned int total_bins = obj->nFramesPerTrack * obj->halfFrameSize;
            float *patch = (float *)malloc(sizeof(float) * total_bins);
            if (!patch) {
                printf("[ERROR] Failed to allocate patch buffer for Track %llu\n", trackID);
                continue;
            }

            for (unsigned int i = 0; i < obj->nFramesPerTrack; i++) {
                unsigned int f = (count - obj->nFramesPerTrack + i) % obj->nFramesPerTrack;
                memcpy(&patch[i * obj->halfFrameSize],
                       obj->trackSpectra[iTrack].buffer[f],
                       sizeof(float) * obj->halfFrameSize);
            }

            int class_id;
            const char* class_name;
            float confidence;

            if (yamnet_classify_patch(obj->yamnet, patch, &class_id, &class_name, &confidence)) {
                printf("[SSS Audio Event] Track %llu classified as %s (Confidence: %.2f)\n",
                       trackID, class_name, confidence);

                strcpy(tracks->tags[iTrack], class_name);
                tracks->activity[iTrack] = confidence;
            }

            if (debug) {
                unsigned int nonzero_bins = 0;
                for (unsigned int i = 0; i < total_bins; i++) {
                    if (patch[i] != 0.0f) nonzero_bins++;
                }
                printf("[DEBUG] Patch bins=%u, non-zero=%u\n",
                       total_bins, nonzero_bins);
            }

            free(patch);
        }
    }
}

*/


// Declaration in mod_sss.h:
// void push_track_to_buffer(mod_sss_obj* obj, msg_tracks_obj* msg_tracks, int debug);

void push_track_to_buffer(mod_sss_obj* obj,
                          msg_tracks_obj* msg_tracks,
                          int debug) {
    tracks_obj* tracks = msg_tracks->tracks;   // unwrap the message

    for (unsigned int iTrack = 0; iTrack < tracks->nTracks; iTrack++) {
        unsigned long long trackID = tracks->ids[iTrack];
        if (trackID == 0) continue;

        // Initialize track lazily if needed
        if (obj->trackSpectra[iTrack].id == 0) {
            obj->trackSpectra[iTrack].id = trackID;
            obj->trackSpectra[iTrack].count = 0;
            obj->trackSpectra[iTrack].lastFrameSeen = msg_tracks->timeStamp;
            obj->trackSpectra[iTrack].type = 'S'; // mark as SSS track

            if (obj->trackSpectra[iTrack].buffer == NULL) {
                obj->trackSpectra[iTrack].buffer =
                    (float **)calloc(obj->nFramesPerTrack, sizeof(float *));
                if (obj->trackSpectra[iTrack].buffer == NULL) {
                    if (debug) {
                        printf("[ERROR] Failed to allocate buffer array for Track %llu\n", trackID);
                    }
                    continue;
                }
            }

            if (debug) {
                printf("[ALLOC] New buffer initialized for Track %llu at index %u\n",
                       trackID, iTrack);
            }
        }

        unsigned int count = obj->trackSpectra[iTrack].count;
        unsigned int h = count % obj->nFramesPerTrack;

        // Copy separated spectrum if valid
        if (obj->out1 && obj->out1->freqs && obj->out1->freqs->array[iTrack]) {
            if (obj->trackSpectra[iTrack].buffer[h] == NULL) {
                obj->trackSpectra[iTrack].buffer[h] =
                    (float *)malloc(sizeof(float) * obj->halfFrameSize);
                if (obj->trackSpectra[iTrack].buffer[h] == NULL) {
                    if (debug) {
                        printf("[ERROR] Failed to allocate buffer[%u] for Track %llu\n", h, trackID);
                    }
                    continue;
                }
            }

            memcpy(obj->trackSpectra[iTrack].buffer[h],
                   obj->out1->freqs->array[iTrack],
                   sizeof(float) * obj->halfFrameSize);

            obj->trackSpectra[iTrack].count++;
            obj->trackSpectra[iTrack].lastFrameSeen = msg_tracks->timeStamp;
            count = obj->trackSpectra[iTrack].count;

            // Trigger classification when buffer has 96 frames, then every 48
            if (count >= 96 && ((count == 96) || ((count - 48) % 96 == 0))) {
                if (debug) {
                    printf("[SSS DEBUG] Track %llu frame count=%u\n", trackID, count);
                }

                // Flatten last 96 frames into contiguous patch
                float patch[96 * obj->halfFrameSize];
                for (unsigned int i = 0; i < 96; i++) {
                    unsigned int f = (count - 96 + i) % obj->nFramesPerTrack;
                    memcpy(&patch[i * obj->halfFrameSize],
                           obj->trackSpectra[iTrack].buffer[f],
                           sizeof(float) * obj->halfFrameSize);
                }

                int class_id;
                const char* class_name;
                float confidence;

                // Debug completeness
                unsigned int total_bins = 96 * obj->halfFrameSize;
                unsigned int nonzero_bins = 0;
                for (unsigned int i = 0; i < total_bins; i++) {
                    if (patch[i] != 0.0f) nonzero_bins++;
                }
                if (debug) {
                    printf("[DEBUG] Patch bins=%u, non-zero=%u\n",
                           total_bins, nonzero_bins);
                }

                /* Reverse SSB shift so YAMNet sees original spectrum */
                if (obj->ssbShiftBins > 0) {
                    ssb_unshift_patch(patch, obj->nFramesPerTrack, obj->halfFrameSize, obj->ssbShiftBins);
                }

                if (yamnet_classify_patch(obj->yamnet, patch,
                                          &class_id, &class_name, &confidence)) {
                    printf("[SSS Audio Event] Track %llu classified as %s (Confidence: %.2f)\n",
                           trackID, class_name, confidence);

                    strcpy(tracks->tags[iTrack], class_name);
                    tracks->activity[iTrack] = confidence;
                }
            }
        }
    }
}






