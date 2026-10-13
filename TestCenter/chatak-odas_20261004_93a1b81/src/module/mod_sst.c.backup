
   /**
    * \file     mod_sst.c  
    */
    
    #include <module/mod_sst.h>

    #define NBINS 10

    unsigned long long session_start = 0;
  
    mod_sst_obj * mod_sst_construct(const mod_sst_cfg * mod_sst_config, const mod_ssl_cfg * mod_ssl_config, const msg_pots_cfg * msg_pots_config, const msg_targets_cfg * msg_targets_config, const msg_tracks_cfg * msg_tracks_config,const msg_spectra_cfg * msg_spectra_config) {

        mod_sst_obj * obj;
        
        unsigned int iTrackMax;
        points_obj * points;
        beampatterns_obj * beampatterns_mics;
        beampatterns_obj * beampatterns_spatialfilters;
        spatialgains_obj * spatialgains;
        spatialmasks_obj * spatialmasks;
        spatialindexes_obj * spatialindexes;
        unsigned int iPoint;
        unsigned int nPointsActive;
        float diffuse_cst;
        float deltaT;
        
        obj = (mod_sst_obj *) malloc(sizeof(mod_sst_obj));

        obj->nPots = msg_pots_config->nPots;
        obj->nTracksMax = msg_tracks_config->nTracks;
        obj->nTracks = 0;
        obj->nTargetsMax = msg_targets_config->nTargets;
        obj->fS = mod_sst_config->fS;
     
        
        obj->nBits = mod_sst_config->nBits;
        
        obj->interpRate      = mod_ssl_config->interpRate;

        obj->frameSize     = 2 * (msg_spectra_config->halfFrameSize - 1);
        obj->halfFrameSize = msg_spectra_config->halfFrameSize;
        obj->hopSize = mod_sst_config->hopSize;


        obj->mode = mod_sst_config->mode;
        obj->add = mod_sst_config->add;

        obj->mixtures = (mixture_obj **) malloc(sizeof(mixture_obj *) * (obj->nTracksMax+1));
        obj->coherences = (coherences_obj **) malloc(sizeof(coherences_obj *) * (obj->nTracksMax+1));
        obj->postprobs = (postprobs_obj **) malloc(sizeof(postprobs_obj *) * (obj->nTracksMax+1));
        obj->trackSpectra = (track_spectrum_obj *) malloc(sizeof(track_spectrum_obj) * obj->nTracksMax);
        
        //unsigned int nFramesPerTrack = (2 * obj->nTracksMax <= 50) ? 2 * obj->nTracksMax : 50;
        unsigned int nFramesPerTrack = 96;


        for (iTrackMax = 0; iTrackMax <= obj->nTracksMax; iTrackMax++) {

            obj->mixtures[iTrackMax] = mixture_construct_zero(obj->nPots, iTrackMax);
            obj->coherences[iTrackMax] = coherences_construct_zero(obj->nPots, iTrackMax);
            obj->postprobs[iTrackMax] = postprobs_construct_zero(obj->nPots, iTrackMax); 
            
            if (iTrackMax < obj->nTracksMax) {
                
            obj->trackSpectra[iTrackMax].buffer = malloc(sizeof(float *) * nFramesPerTrack);
            for (unsigned int f = 0; f < nFramesPerTrack; f++) {
                obj->trackSpectra[iTrackMax].buffer[f] = malloc(sizeof(float) * obj->halfFrameSize);
                memset(obj->trackSpectra[iTrackMax].buffer[f], 0x00, sizeof(float) * obj->halfFrameSize);
            }

            obj->trackSpectra[iTrackMax].count = 0;
            obj->trackSpectra[iTrackMax].id    = 0;
            obj->trackSpectra[iTrackMax].type  = 0;
            obj->trackSpectra[iTrackMax].lastFrameSeen  = 0;
            
        }


        }

        obj->ids = (unsigned long long *) malloc(sizeof(unsigned long long) * obj->nTracksMax);
        memset(obj->ids, 0x00, sizeof(unsigned long long) * obj->nTracksMax);
        obj->idsAdded = (unsigned long long *) malloc(sizeof(unsigned long long) * obj->nTracksMax);
        memset(obj->idsAdded, 0x00, sizeof(unsigned long long) * obj->nTracksMax);
        obj->idsRemoved = (unsigned long long *) malloc(sizeof(unsigned long long) * obj->nTracksMax);
        memset(obj->idsRemoved, 0x00, sizeof(unsigned long long) * obj->nTracksMax);
        
        obj->tags = (char **) malloc(sizeof(char *) * obj->nTracksMax);
        
        /* Allocate per-track classification smoothing state */
        obj->last_class_id = (int *) malloc(sizeof(int) * obj->nTracksMax);
        obj->last_class_conf = (float *) malloc(sizeof(float) * obj->nTracksMax);
        obj->last_class_ts = (unsigned long long *) malloc(sizeof(unsigned long long) * obj->nTracksMax);
        
        /* Allocate rolling Top-K history buffers */
        obj->topk_history = (topk_hop_t **) malloc(sizeof(topk_hop_t *) * obj->nTracksMax);
        obj->topk_head = (int *) malloc(sizeof(int) * obj->nTracksMax);
        obj->topk_count = (int *) malloc(sizeof(int) * obj->nTracksMax);
        
        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
            obj->last_class_id[iTrackMax] = -1;
            obj->last_class_conf[iTrackMax] = 0.0f;
            obj->last_class_ts[iTrackMax] = 0;
            
            // Allocate circular buffer for 6 hops of Top-K results
            obj->topk_history[iTrackMax] = (topk_hop_t *) malloc(sizeof(topk_hop_t) * ROLLING_HOPS);
            obj->topk_head[iTrackMax] = 0;
            obj->topk_count[iTrackMax] = 0;
            
            // Initialize each hop entry
            for (int h = 0; h < ROLLING_HOPS; h++) {
                obj->topk_history[iTrackMax][h].timestamp = 0;
                for (int k = 0; k < TOPK; k++) {
                    obj->topk_history[iTrackMax][h].class_ids[k] = -1;
                    obj->topk_history[iTrackMax][h].confidences[k] = 0.0f;
                }
            }
        }

        /* sim_mode / min_event_votes from config */
        obj->sim_mode = mod_sst_config->sim_mode;
        obj->min_event_votes = (mod_sst_config->min_event_votes >= 1 &&
                                mod_sst_config->min_event_votes <= ROLLING_HOPS)
                               ? mod_sst_config->min_event_votes : 4;

        /* Per-track .bin sidecar path — 512-byte buffer per track, no truncation risk */
        obj->last_patch_path = (char **) malloc(sizeof(char *) * obj->nTracksMax);
        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
            obj->last_patch_path[iTrackMax] = (char *) malloc(512);
            obj->last_patch_path[iTrackMax][0] = '\0';
        }

        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

            obj->tags[iTrackMax] = (char *) malloc(sizeof(char) * 256);
            strcpy(obj->tags[iTrackMax],"");

        }

        obj->type = (char *) malloc(sizeof(char) * obj->nTracksMax);
        memset(obj->type, 0x00, sizeof(char) * obj->nTracksMax);
        
        obj->kalmans = (kalman_obj **) malloc(sizeof(kalman_obj *) * obj->nTracksMax); 
        memset(obj->kalmans, 0x00, sizeof(kalman_obj *) * obj->nTracksMax);
        obj->particles = (particles_obj **) malloc(sizeof(particles_obj *) * obj->nTracksMax);
        memset(obj->particles, 0x00, sizeof(particles_obj *) * obj->nTracksMax);
        
        obj->sourceActivities = (float *) malloc(sizeof(float) * obj->nTracksMax);
        memset(obj->sourceActivities, 0x00, sizeof(float) * obj->nTracksMax);

        obj->theta_new = mod_sst_config->theta_new;
        obj->N_prob = mod_sst_config->N_prob;
        obj->theta_prob = mod_sst_config->theta_prob;
        obj->N_inactive = (unsigned int *) malloc(sizeof(unsigned int) * obj->nTracksMax);

        obj->theta_inactive = mod_sst_config->theta_inactive;

        obj->n_prob = (unsigned int *) malloc(sizeof(unsigned int) * obj->nTracksMax);
        memset(obj->n_prob, 0x00, sizeof(unsigned int) * obj->nTracksMax);
        obj->mean_prob = (float *) malloc(sizeof(float) * obj->nTracksMax);
        memset(obj->mean_prob, 0x00, sizeof(float) * obj->nTracksMax);
        obj->n_inactive = (unsigned int *) malloc(sizeof(unsigned int) * obj->nTracksMax);
        memset(obj->n_inactive, 0x00, sizeof(unsigned int) * obj->nTracksMax);

        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

            obj->type[iTrackMax] = 'I';

            switch(obj->mode) {

                case 'k':
                    
                    obj->kalmans[iTrackMax] =  kalman_construct_zero();

                break;

                case 'p':
                    
                    obj->particles[iTrackMax] =  particles_construct_zero(mod_sst_config->nParticles);

                break;

                default:

                    printf("Invalid type of filter\n");
                    exit(EXIT_FAILURE);

                break;

            }
            
            obj->N_inactive[iTrackMax] = mod_sst_config->N_inactive[iTrackMax];

        }       

        points = space_sphere(mod_ssl_config->levels[mod_ssl_config->nLevels-1]);
        beampatterns_mics = directivity_beampattern_mics(mod_ssl_config->mics, mod_ssl_config->nThetas);
        beampatterns_spatialfilters = directivity_beampattern_spatialfilters(mod_ssl_config->spatialfilters, mod_ssl_config->nThetas);
        spatialgains = directivity_spatialgains(mod_ssl_config->mics, beampatterns_mics, mod_ssl_config->spatialfilters, beampatterns_spatialfilters, points);           
        spatialmasks = directivity_spatialmasks(spatialgains, mod_ssl_config->gainMin);    
        spatialindexes = directivity_spatialindexes(spatialmasks);

        nPointsActive = 0;

        for (iPoint = 0; iPoint < spatialindexes->nPoints; iPoint++) {

            if (spatialindexes->count[iPoint] > 0) {
                nPointsActive++;
            }

        }

        diffuse_cst = 1.0f / (4.0f * M_PI * ((float) nPointsActive) / ((float) spatialindexes->nPoints));

        points_destroy(points);
        beampatterns_destroy(beampatterns_mics);
        beampatterns_destroy(beampatterns_spatialfilters);
        spatialgains_destroy(spatialgains);
        spatialmasks_destroy(spatialmasks);
        spatialindexes_destroy(spatialindexes);

        deltaT = ((float) mod_sst_config->hopSize) / ((float) msg_pots_config->fS);
        
        switch(obj->mode) {

            case 'k':

                obj->kalman2kalman_prob = kalman2kalman_construct(deltaT,
                                                                  mod_sst_config->sigmaQ,
                                                                  mod_sst_config->sigmaR_prob,
                                                                  mod_sst_config->epsilon);    

                obj->kalman2kalman_active = kalman2kalman_construct(deltaT,
                                                                    mod_sst_config->sigmaQ,
                                                                    mod_sst_config->sigmaR_active,
                                                                    mod_sst_config->epsilon);   

                obj->kalman2kalman_target = kalman2kalman_construct(deltaT,
                                                                    mod_sst_config->sigmaQ,
                                                                    mod_sst_config->sigmaR_target,
                                                                    mod_sst_config->epsilon);

                obj->kalman2coherence_prob = kalman2coherence_construct(mod_sst_config->epsilon, 
                                                                        mod_sst_config->sigmaR_prob);

                obj->kalman2coherence_active = kalman2coherence_construct(mod_sst_config->epsilon, 
                                                                          mod_sst_config->sigmaR_active);

                obj->kalman2coherence_target = kalman2coherence_construct(mod_sst_config->epsilon,
                                                                          mod_sst_config->sigmaR_target);

                obj->particle2particle_prob = NULL;
                obj->particle2particle_active = NULL;
                obj->particle2particle_target = NULL;
                obj->particle2coherence_prob = NULL;
                obj->particle2coherence_active = NULL;                
                obj->particle2coherence_target = NULL;

            break;

            case 'p':

                obj->kalman2kalman_prob = NULL;
                obj->kalman2kalman_active = NULL;
                obj->kalman2kalman_target = NULL;
                obj->kalman2coherence_prob = NULL;
                obj->kalman2coherence_active = NULL;
                obj->kalman2coherence_target = NULL;

                obj->particle2particle_prob = particle2particle_construct(mod_sst_config->nParticles,
                                                                          deltaT,
                                                                          mod_sst_config->st_alpha,
                                                                          mod_sst_config->st_beta,
                                                                          mod_sst_config->st_ratio,
                                                                          mod_sst_config->ve_alpha,
                                                                          mod_sst_config->ve_beta,
                                                                          mod_sst_config->ve_ratio,
                                                                          mod_sst_config->ac_alpha,
                                                                          mod_sst_config->ac_beta,
                                                                          mod_sst_config->ac_ratio,
                                                                          (double) mod_sst_config->epsilon,
                                                                          mod_sst_config->sigmaR_prob,
                                                                          mod_sst_config->Nmin);

                obj->particle2particle_active = particle2particle_construct(mod_sst_config->nParticles,
                                                                            deltaT,
                                                                            mod_sst_config->st_alpha,
                                                                            mod_sst_config->st_beta,
                                                                            mod_sst_config->st_ratio,
                                                                            mod_sst_config->ve_alpha,
                                                                            mod_sst_config->ve_beta,
                                                                            mod_sst_config->ve_ratio,
                                                                            mod_sst_config->ac_alpha,
                                                                            mod_sst_config->ac_beta,
                                                                            mod_sst_config->ac_ratio,
                                                                            (double) mod_sst_config->epsilon,
                                                                            mod_sst_config->sigmaR_active,
                                                                            mod_sst_config->Nmin);   

                obj->particle2particle_target = particle2particle_construct(mod_sst_config->nParticles,
                                                                            deltaT,
                                                                            mod_sst_config->st_alpha,
                                                                            mod_sst_config->st_beta,
                                                                            mod_sst_config->st_ratio,
                                                                            mod_sst_config->ve_alpha,
                                                                            mod_sst_config->ve_beta,
                                                                            mod_sst_config->ve_ratio,
                                                                            mod_sst_config->ac_alpha,
                                                                            mod_sst_config->ac_beta,
                                                                            mod_sst_config->ac_ratio,
                                                                            (double) mod_sst_config->epsilon,
                                                                            mod_sst_config->sigmaR_target,
                                                                            mod_sst_config->Nmin);  

                obj->particle2coherence_prob = particle2coherence_construct(mod_sst_config->sigmaR_prob);

                obj->particle2coherence_active = particle2coherence_construct(mod_sst_config->sigmaR_active);

                obj->particle2coherence_target = particle2coherence_construct(mod_sst_config->sigmaR_target);

            break;

            default:

            break;

        }

        obj->mixture2mixture = mixture2mixture_construct(mod_sst_config->active_gmm,
                                                         mod_sst_config->inactive_gmm,
                                                         diffuse_cst,
                                                         mod_sst_config->Pfalse,
                                                         mod_sst_config->Pnew,
                                                         mod_sst_config->Ptrack,
                                                         mod_sst_config->epsilon);

        obj->id = 0;

        obj->in1 = (msg_pots_obj *) NULL;
        obj->in2 = (msg_targets_obj *) NULL;
        obj->out = (msg_tracks_obj *) NULL;

        obj->enabled = 0;
        obj->enable_classifier_output = mod_sst_config->enable_classifier_output;
        
        // Copy classifier log directory path, always resolving to absolute so
        // JSON consumers don't need to know the ODAS working directory.
        {
            const char *cfg_dir = mod_sst_config->classifier_log_dir
                                  ? mod_sst_config->classifier_log_dir
                                  : "./ClassifierLogs";
            char cwd[512] = {0};
            if (cfg_dir[0] == '/') {
                /* Already absolute */
                obj->classifier_log_dir = strdup(cfg_dir);
            } else if (getcwd(cwd, sizeof(cwd))) {
                /* Relative path — prepend cwd.
                 * Strip leading "./" or "." from cfg_dir first. */
                const char *rel = cfg_dir;
                if (rel[0] == '.' && rel[1] == '/') rel += 2;
                else if (rel[0] == '.' && rel[1] == '\0') rel = "";
                char abs_log_dir[700];
                if (rel[0] != '\0')
                    snprintf(abs_log_dir, sizeof(abs_log_dir), "%s/%s", cwd, rel);
                else
                    snprintf(abs_log_dir, sizeof(abs_log_dir), "%s", cwd);
                obj->classifier_log_dir = strdup(abs_log_dir);
            } else {
                /* getcwd failed — keep as-is (rare embedded fallback) */
                obj->classifier_log_dir = strdup(cfg_dir);
            }
        }

        // At the end of mod_sst_construct
        obj->yamnet = yamnet_create("/home/azureuser/z_odas_newbeamform/models/yamnet_core.tflite",
                            "/home/azureuser/z_odas_newbeamform/models/yamnet_class_map.csv");

        if (obj->yamnet == NULL) {
            printf("Failed to initialize YAMNet\n");
            exit(EXIT_FAILURE);
        }

        return obj;

    }

void mod_sst_destroy(mod_sst_obj * obj) {

        unsigned int iTrackMax;

        for (iTrackMax = 0; iTrackMax <= obj->nTracksMax; iTrackMax++) {

            mixture_destroy(obj->mixtures[iTrackMax]);
            coherences_destroy(obj->coherences[iTrackMax]);
            postprobs_destroy(obj->postprobs[iTrackMax]);

        }

        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

            if (obj->kalmans[iTrackMax] != NULL) {
                kalman_destroy(obj->kalmans[iTrackMax]);
            }

            if (obj->particles[iTrackMax] != NULL) {
                particles_destroy(obj->particles[iTrackMax]);
            }
        }

        free((void *) obj->mixtures);
        free((void *) obj->coherences);
        free((void *) obj->postprobs);

        free((void *) obj->ids);
        free((void *) obj->idsAdded);
        free((void *) obj->idsRemoved);
        
        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
            free((void *) obj->tags[iTrackMax]);
        }
        free((void *) obj->tags);
        
        free((void *) obj->type);

        free((void *) obj->kalmans);
        free((void *) obj->particles);

        free((void *) obj->sourceActivities);

        free((void *) obj->N_inactive);
        free((void *) obj->n_prob);
        free((void *) obj->mean_prob);
        free((void *) obj->n_inactive);

        /* Free per-track classification state */
        if (obj->last_class_id) free((void *) obj->last_class_id);
        if (obj->last_class_conf) free((void *) obj->last_class_conf);
        if (obj->last_class_ts) free((void *) obj->last_class_ts);
        
        /* Free rolling Top-K history buffers */
        if (obj->topk_history) {
            for (unsigned int i = 0; i < obj->nTracksMax; i++) {
                if (obj->topk_history[i]) free((void *) obj->topk_history[i]);
            }
            free((void *) obj->topk_history);
        }
        if (obj->topk_head) free((void *) obj->topk_head);
        if (obj->topk_count) free((void *) obj->topk_count);

        if (obj->last_patch_path) {
            for (unsigned int i = 0; i < obj->nTracksMax; i++) {
                if (obj->last_patch_path[i]) free(obj->last_patch_path[i]);
            }
            free((void *) obj->last_patch_path);
        }

        if (obj->kalman2kalman_prob != NULL) {
            kalman2kalman_destroy(obj->kalman2kalman_prob);
        }
        if (obj->kalman2kalman_active != NULL) {
            kalman2kalman_destroy(obj->kalman2kalman_active);
        }
        if (obj->kalman2kalman_target != NULL) {
            kalman2kalman_destroy(obj->kalman2kalman_target);
        }
        if (obj->kalman2coherence_prob != NULL) {
            kalman2coherence_destroy(obj->kalman2coherence_prob);
        }
        if (obj->kalman2coherence_active != NULL) {
            kalman2coherence_destroy(obj->kalman2coherence_active);
        }
        if (obj->kalman2coherence_target != NULL) {
            kalman2coherence_destroy(obj->kalman2coherence_target);
        }
        if (obj->particle2particle_prob != NULL) {
            particle2particle_destroy(obj->particle2particle_prob);
        }
        if (obj->particle2particle_active != NULL) {
            particle2particle_destroy(obj->particle2particle_active);
        }
        if (obj->particle2particle_target != NULL) {
            particle2particle_destroy(obj->particle2particle_target);
        }
        if (obj->particle2coherence_prob != NULL)  {
            particle2coherence_destroy(obj->particle2coherence_prob);
        }
        if (obj->particle2coherence_active != NULL) {
            particle2coherence_destroy(obj->particle2coherence_active);
        }
        if (obj->particle2coherence_target != NULL) {
            particle2coherence_destroy(obj->particle2coherence_target);
        }

        if (obj->yamnet) {
            yamnet_destroy(obj->yamnet);
            obj->yamnet = NULL;
        }
        
        if (obj->classifier_log_dir) {
            free(obj->classifier_log_dir);
            obj->classifier_log_dir = NULL;
        }


        mixture2mixture_destroy(obj->mixture2mixture);
        
        //unsigned int nFramesPerTrack = (2 * obj->nTracksMax <= 50) ? 2 * obj->nTracksMax : 50;
        unsigned int nFramesPerTrack = 96;

        // FIXED: use < obj->nTracksMax here, not <=
        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
            if (obj->trackSpectra[iTrackMax].buffer != NULL) {
                for (unsigned int f = 0; f < nFramesPerTrack; f++) {
                    free(obj->trackSpectra[iTrackMax].buffer[f]);  // Free each frame
                }
                free(obj->trackSpectra[iTrackMax].buffer);  // Free the buffer array
            }
        }
        free(obj->trackSpectra);  // Free the trackSpectra array itself


        
        free((void *) obj);
}

    int mod_sst_process(mod_sst_obj * obj) {

        unsigned int iPot;
        unsigned int iTrackMax;
        unsigned int iTrack;
        unsigned int iTargetMax;
        float x,y,z;
        float sourceActivity;
        int rtnValue;
        char targetFound;
        unsigned int nFramesPerTrack = 96 ;
        
        // Safety check
        if (obj == NULL || obj->in1 == NULL || obj->in2 == NULL) {
            return 0;
        }

        if (obj->in1->timeStamp != obj->in2->timeStamp) {

            printf("Time stamp mismatch.\n");
            exit(EXIT_FAILURE);

        }
        

        if (msg_pots_isZero(obj->in1) == 0) {
                
           

            if (obj->enabled == 1) {

                // +----------------------------------------------------------------------+
                // | Update tracked sources from target sources                           |
                // +----------------------------------------------------------------------+

                // Add sources

                for (iTargetMax = 0; iTargetMax < obj->nTargetsMax; iTargetMax++) {

                    targetFound = 0x00;

                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                        if (strcmp(obj->in2->targets->tags[iTargetMax],obj->tags[iTrackMax])==0) {
                            
                            targetFound = 0x01;
                            break;

                        }

                    }

                    if (targetFound == 0x00) {

                        for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                            if (obj->ids[iTrackMax] == 0) {

                                obj->id++;
                                obj->ids[iTrackMax] = obj->id;
                                strcpy(obj->tags[iTrackMax],obj->in2->targets->tags[iTargetMax]);

                                switch(obj->mode) {

                                    case 'k':

                                        kalman2kalman_init_targets(obj->kalman2kalman_prob, 
                                                                   obj->in2->targets,
                                                                   iTargetMax, 
                                                                   obj->kalmans[iTrackMax]); 

                                    break;

                                    case 'p':

                                        particle2particle_init_targets(obj->particle2particle_prob, 
                                                                       obj->in2->targets,
                                                                       iTargetMax, 
                                                                       obj->particles[iTrackMax]);

                                    break;

                                    default:

                                        printf("Invalid filter type.\n");
                                        exit(EXIT_FAILURE);

                                    break;

                                }

                                obj->type[iTrackMax] = 'T';
                                obj->n_prob[iTrackMax] = 0;
                                obj->mean_prob[iTrackMax] = 0.0f;

                                break;

                            }

                        }

                    }

                }

                // Remove sources

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (strcmp(obj->tags[iTrackMax],"dynamic") != 0) {

                        targetFound = 0x00;

                        for (iTargetMax = 0; iTargetMax < obj->nTargetsMax; iTargetMax++) {

                            if (strcmp(obj->tags[iTrackMax],obj->in2->targets->tags[iTargetMax]) == 0) {

                                targetFound = 0x01;
                                break;

                            }

                        }

                        if (targetFound == 0x00) {

                            obj->ids[iTrackMax] = 0;
                            strcpy(obj->tags[iTrackMax],"");
                            obj->type[iTrackMax] = 'I';
                            obj->n_prob[iTrackMax] = 0;
                            obj->mean_prob[iTrackMax] = 0.0f;

                        }

                    }

                }

                // +----------------------------------------------------------------------+
                // | Count tracked sources                                                |
                // +----------------------------------------------------------------------+

                obj->nTracks = 0;

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        obj->nTracks++;

                    }

                }


                // +----------------------------------------------------------------------+
                // | Predict                                                              |
                // +----------------------------------------------------------------------+

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        switch(obj->mode) {

                            case 'k':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':                    

                                        kalman2kalman_predict(obj->kalman2kalman_prob,
                                                              obj->kalmans[iTrackMax]);         

                                    break;

                                    case 'A':

                                        kalman2kalman_predict(obj->kalman2kalman_active,
                                                              obj->kalmans[iTrackMax]);  

                                    break;

                                    case 'T':

                                        kalman2kalman_predict_static(obj->kalman2kalman_target,
                                                                     obj->kalmans[iTrackMax]);

                                    break;

                                    default:

                                        printf("Predict: Unknown state.\n");
                                        printf("%u, %c\n",iTrackMax,obj->type[iTrackMax]);
                                        exit(EXIT_FAILURE);                        

                                    break;

                                }

                            break;

                            case 'p':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':    

                                        particle2particle_predict(obj->particle2particle_prob,
                                                                  obj->particles[iTrackMax]);                        

                                    break;

                                    case 'A':

                                        particle2particle_predict(obj->particle2particle_active,
                                                                  obj->particles[iTrackMax]);                        

                                    break;

                                    case 'T':

                                        particle2particle_predict_static(obj->particle2particle_target,
                                                                         obj->particles[iTrackMax]);

                                    break;

                                    default:

                                        printf("Predict: Unknown state.\n");
                                        exit(EXIT_FAILURE);                        

                                    break;

                                }    

                            break;

                            default:

                                printf("Invalid filter type.\n");
                                exit(EXIT_FAILURE);

                            break;

                        }

                    }

                }

            

                // +----------------------------------------------------------------------+
                // | Coherence                                                            |
                // +----------------------------------------------------------------------+

                iTrack = 0;

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        switch(obj->mode) {

                            case 'k':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':  

                                        kalman2coherence_process(obj->kalman2coherence_prob,
                                                                 obj->kalmans[iTrackMax],
                                                                 obj->in1->pots,
                                                                 iTrack,
                                                                 obj->coherences[obj->nTracks]);

                                    break;

                                    case 'A':

                                        kalman2coherence_process(obj->kalman2coherence_active,
                                                                 obj->kalmans[iTrackMax],
                                                                 obj->in1->pots,
                                                                 iTrack,
                                                                 obj->coherences[obj->nTracks]);

                                    break;

                                    case 'T':

                                        kalman2coherence_process(obj->kalman2coherence_target,
                                                                 obj->kalmans[iTrackMax],
                                                                 obj->in1->pots,
                                                                 iTrack,
                                                                 obj->coherences[obj->nTracks]);

                                    break;

                                    default:

                                        printf("Coherence: Unknown state.\n");
                                        exit(EXIT_FAILURE);                            

                                    break;

                                }

                            break;

                            case 'p':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':                      

                                        particle2coherence_process(obj->particle2coherence_prob,
                                                                   obj->particles[iTrackMax],
                                                                   obj->in1->pots,
                                                                   iTrack,
                                                                   obj->coherences[obj->nTracks]);

                                    break;

                                    case 'A':                      

                                        particle2coherence_process(obj->particle2coherence_active,
                                                                   obj->particles[iTrackMax],
                                                                   obj->in1->pots,
                                                                   iTrack,
                                                                   obj->coherences[obj->nTracks]);

                                    break;

                                    case 'T':

                                        particle2coherence_process(obj->particle2coherence_target,
                                                                   obj->particles[iTrackMax],
                                                                   obj->in1->pots,
                                                                   iTrack,
                                                                   obj->coherences[obj->nTracks]);                                

                                    break;

                                    default:

                                        printf("Coherence: Unknown state.\n");
                                        exit(EXIT_FAILURE);                            

                                    break;                        

                                }

                            break;

                            default:

                                printf("Invalid filter type.\n");
                                exit(EXIT_FAILURE);

                            break;

                        }

                        iTrack++;

                    }

                }
                
              
             
                // +----------------------------------------------------------------------+
                // | Mixture                                                              |
                // +----------------------------------------------------------------------+

                mixture2mixture_process(obj->mixture2mixture, 
                	                    obj->mixtures[obj->nTracks], 
                	                    obj->in1->pots, 
                	                    obj->coherences[obj->nTracks], 
                	                    obj->postprobs[obj->nTracks]);

                unsigned int bestTrack = UINT_MAX;
                float bestScore = 0.0f;
                for (iPot = 0; iPot < obj->nPots; iPot++) {

                    unsigned int bestTrack = UINT_MAX;
                    float bestScore = 0.0f;

                    for (unsigned int iTrack = 0; iTrack < obj->nTracksMax; iTrack++) {
                        if (obj->ids[iTrack] != 0 && (obj->type[iTrack] == 'P' || obj->type[iTrack] == 'A')) {
                            float score = obj->coherences[obj->nTracks]->array[iPot * obj->nTracksMax + iTrack];
                            
                            // ===============================================================
                            // SEMANTIC BOOST: Apply multiplier if Top-K contains animal class
                            // ===============================================================
                            if (obj->topk_count[iTrack] > 0) {
                                // Get most recent Top-K prediction
                                int recent_hop = (obj->topk_head[iTrack] + ROLLING_HOPS - 1) % ROLLING_HOPS;
                                topk_hop_t *recent = &obj->topk_history[iTrack][recent_hop];
                                
                                // Check if any of Top-K contains target animal classes
                                // Example: Dog=74, Cat=76, Bird=397, Elephant=359, etc.
                                const int target_classes[] = {74, 76, 397, 359, 388, 89};  // Configurable
                                const int n_targets = 6;
                                const float confidence_thresh = 0.05f;  // Minimum confidence
                                const float semantic_boost = 1.2f;      // 20% boost
                                
                                for (int k = 0; k < TOPK; k++) {
                                    if (recent->confidences[k] < confidence_thresh) break;
                                    
                                    for (int t = 0; t < n_targets; t++) {
                                        if (recent->class_ids[k] == target_classes[t]) {
                                            score *= semantic_boost;
                                            goto boost_applied;  // Apply boost only once per track
                                        }
                                    }
                                }
                                boost_applied: ;
                            }
                            // ===============================================================
                            
                            if (score > bestScore) {
                                bestScore = score;
                                bestTrack = iTrack;
                            }
                        }
                    }

                    if (bestTrack != UINT_MAX && bestScore > obj->theta_prob) {
                        push_pot_to_track_buffer(obj, iPot, bestTrack, obj->ids[bestTrack], nFramesPerTrack, 1);
                    }
                }
                                            
                                            
                

                // +----------------------------------------------------------------------+
                // | Update                                                               |
                // +----------------------------------------------------------------------+

                iTrack = 0;

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        switch(obj->mode) {

                            case 'k':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':                                                  

                                        kalman2kalman_update(obj->kalman2kalman_prob,
                                                             obj->postprobs[obj->nTracks],
                                                             iTrack,
                                                             obj->in1->pots,
                                                             obj->kalmans[iTrackMax]);

                                    break;

                                    case 'A':                      

                                        kalman2kalman_update(obj->kalman2kalman_active,
                                                             obj->postprobs[obj->nTracks],
                                                             iTrack,
                                                             obj->in1->pots,
                                                             obj->kalmans[iTrackMax]);

                                    break;

                                    case 'T':

                                        kalman2kalman_update_static(obj->kalman2kalman_target,
                                                                    obj->postprobs[obj->nTracks],
                                                                    iTrack,
                                                                    obj->in1->pots,
                                                                    obj->kalmans[iTrackMax]);

                                    break;

                                    default:

                                        printf("Update: Unknown state.\n");
                                        exit(EXIT_FAILURE); 

                                    break;

                                }

                            break;

                            case 'p':

                                switch (obj->type[iTrackMax]) {
                                    
                                    case 'P':                      

                                        particle2particle_update(obj->particle2particle_prob,
                                                                 obj->postprobs[obj->nTracks],
                                                                 iTrack,
                                                                 obj->in1->pots,
                                                                 obj->particles[iTrackMax]);

                                    break;

                                    case 'A':

                                        particle2particle_update(obj->particle2particle_active,
                                                                 obj->postprobs[obj->nTracks],
                                                                 iTrack,
                                                                 obj->in1->pots,
                                                                 obj->particles[iTrackMax]);

                                    break;

                                    case 'T':

                                        particle2particle_update_static(obj->particle2particle_target,
                                                                        obj->postprobs[obj->nTracks],
                                                                        iTrack,
                                                                        obj->in1->pots,
                                                                        obj->particles[iTrackMax]);

                                    break;

                                    default:

                                        printf("Update: Unknown state.\n");
                                        exit(EXIT_FAILURE);                         

                                    break;

                                }

                            break;

                            default:

                                printf("Invalid filter type.\n");
                                exit(EXIT_FAILURE);

                            break;

                        }

                        iTrack++;

                    }

                }

                     
                 // After Update loop — OUTSIDE
                 // Export JSON packets if flag is enabled
                 // Gate output to every ROLLING_HOPS hops (48ms) so each emitted
                 // frame aligns with a YAMNet evaluation window.  Emitting every
                 // 8ms produced 6 identical (stale) classification frames per
                 // YAMNet call, inflating the session JSON 6× unnecessarily.
                if (obj->enable_classifier_output &&
                    (obj->in1->timeStamp % ROLLING_HOPS == 0)) {
                    dump_track_buffers_to_json(obj, "sst_session_live.json", nFramesPerTrack);
                    dump_track_fingerprint_only(obj, "sst_session_live", nFramesPerTrack);
                }

                // +----------------------------------------------------------------------+
                // | Activity                                                             |
                // +----------------------------------------------------------------------+

                iTrack = 0;
                memset(obj->sourceActivities, 0x00, sizeof(float) * obj->nTracksMax);

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        sourceActivity = obj->postprobs[obj->nTracks]->arrayTrackTotal[iTrack];
                        obj->sourceActivities[iTrackMax] = sourceActivity;

                        // Update counters
                        
                        switch (obj->type[iTrackMax]) {
                            
                            case 'P':

                                obj->n_prob[iTrackMax]++;
                                obj->mean_prob[iTrackMax] += sourceActivity;

                            break;

                            case 'A':

                                if (sourceActivity >= obj->theta_inactive) {
                                    obj->n_inactive[iTrackMax] = 0;
                                }
                                else {
                                    obj->n_inactive[iTrackMax]++;   
                                }

                            break;

                            case 'T':

                                // No counter to update

                            break;

                            default:

                                printf("Unknown state.\n");
                                exit(EXIT_FAILURE);

                            break;

                        }

                        iTrack++;

                    }

                }        

                // +----------------------------------------------------------------------+
                // | Transitions                                                          |
                // +----------------------------------------------------------------------+

                iTrack = 0;

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        if ((obj->type[iTrackMax] == 'P') && 
                            (obj->n_prob[iTrackMax] == obj->N_prob) && 
                            (obj->mean_prob[iTrackMax]/((float) obj->N_prob) >= obj->theta_prob)) {

                            obj->type[iTrackMax] = 'A';
                            obj->trackSpectra[iTrackMax].type = 'A';
                            obj->n_inactive[iTrackMax] = 0;

                        }

                        iTrack++;

                    }

                }          

                // +----------------------------------------------------------------------+
                // | Update tracked sources from potential sources                        |
                // +----------------------------------------------------------------------+

                // Only if the tracking is dynamic (i.e. sources may be created automatically)

                if (obj->add == 'd') {

                    // Add sources
                    
                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                        obj->idsAdded[iTrackMax] = 0;

                    }

                    for (iPot = 0; iPot < obj->nPots; iPot++) {

                        if (obj->postprobs[obj->nTracks]->arrayNew[iPot] > obj->theta_new) {                

                            for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                                if (obj->ids[iTrackMax] == 0) {

                                    obj->id++;
                                    obj->idsAdded[iTrackMax] = obj->id;
                                    
                                    switch(obj->mode) {

                                        case 'k':

                                            kalman2kalman_init_pots(obj->kalman2kalman_prob, 
                                                                    obj->in1->pots,
                                                                    iPot, 
                                                                    obj->kalmans[iTrackMax]); 

                                        break;

                                        case 'p':

                                            particle2particle_init_pots(obj->particle2particle_prob, 
                                                                        obj->in1->pots,
                                                                        iPot, 
                                                                        obj->particles[iTrackMax]);

                                        break;

                                        default:

                                            printf("Invalid filter type.\n");
                                            exit(EXIT_FAILURE);

                                        break;

                                    }

                                    obj->type[iTrackMax] = 'P';
                                    obj->n_prob[iTrackMax] = 0;
                                    obj->mean_prob[iTrackMax] = 0.0f;
                                    
                                    /* Seed the newly-created dynamic track with the triggering pot.
                                     * Use idsAdded (new id) because obj->ids[iTrackMax] is still 0 here.
                                     */
                                    push_pot_to_track_buffer(obj,iPot,iTrackMax,obj->idsAdded[iTrackMax],nFramesPerTrack, 1);                  
                                    break;

                                }

                            }

                        }

                    }

                    // Remove source

                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                        obj->idsRemoved[iTrackMax] = 0;
                        
                    }

                    iTrack = 0;

                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
                        obj->idsRemoved[iTrackMax] = 0;
                    }

                    iTrack = 0;

                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {
                        if (obj->ids[iTrackMax] != 0) {

                            if ((obj->type[iTrackMax] == 'P') &&
                                (obj->n_prob[iTrackMax] == obj->N_prob) &&
                                (obj->mean_prob[iTrackMax]/((float)obj->N_prob) < obj->theta_prob)) {
                                obj->idsRemoved[iTrackMax] = obj->ids[iTrackMax];
                            }

                            // ✅ fixed guard
                            unsigned int inactive_thresh = obj->N_inactive[iTrackMax];
                            if ((obj->type[iTrackMax] == 'A') &&
                                (obj->n_inactive[iTrackMax] >= inactive_thresh)) {
                                obj->idsRemoved[iTrackMax] = obj->ids[iTrackMax];
                            }

                            iTrack++;
                        }
                    }


                    // Update IDs

                    for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                        if (obj->idsAdded[iTrackMax] != 0) {

                            obj->ids[iTrackMax] = obj->idsAdded[iTrackMax];
                            strcpy(obj->tags[iTrackMax], "dynamic");

                        }

                        if (obj->idsRemoved[iTrackMax] != 0) {

                            reset_track_slot(obj, iTrackMax, nFramesPerTrack);
                   

                        }

                    }

                }

                // +----------------------------------------------------------------------+
                // | Count tracked sources                                                |
                // +----------------------------------------------------------------------+

                obj->nTracks = 0;

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    if (obj->ids[iTrackMax] != 0) {

                        obj->nTracks++;

                    }

                }

                // +----------------------------------------------------------------------+
                // | Copy in tracking                                                     |
                // +----------------------------------------------------------------------+
           
                memset(obj->out->tracks->array, 0x00, sizeof(float) * obj->out->tracks->nTracks * 3);
                memset(obj->out->tracks->ids, 0x00, sizeof(unsigned long long) * obj->out->tracks->nTracks);
                memset(obj->out->tracks->activity, 0x00, sizeof(float) * obj->out->tracks->nTracks);

                for (iTrackMax = 0; iTrackMax < obj->nTracksMax; iTrackMax++) {

                    strcpy(obj->out->tracks->tags[iTrackMax], "");

                    if (obj->ids[iTrackMax] != 0) {

                        if ((obj->type[iTrackMax] == 'A')|| (obj->type[iTrackMax] == 'P') || (obj->type[iTrackMax] == 'T')) {

                            switch(obj->mode) {

                                case 'k':

                                    kalman2kalman_estimate(obj->kalman2kalman_active, 
                                                           obj->kalmans[iTrackMax], 
                                                           &x, 
                                                           &y, 
                                                           &z);

                                    /* Log class name and confidence for this track */
                                    if (obj->last_class_id[iTrackMax] != -1) {
                                        const char *cname = yamnet_class_name_from_id(obj->yamnet, obj->last_class_id[iTrackMax]);
                                        /*printf("[KALMAN] Track %llu: class=%s, confidence=%.2f, pos=(%.2f, %.2f, %.2f)\n",
                                               obj->ids[iTrackMax],
                                               cname ? cname : "(unknown)",
                                               obj->last_class_conf[iTrackMax],
                                               x, y, z);*/
                                    }

                                break;

                                case 'p':

                                    particle2particle_estimate(obj->particle2particle_active, 
                                                               obj->particles[iTrackMax], 
                                                               &x,
                                                               &y,
                                                               &z);

                                break;

                                default:

                                    printf("Invalid filter type.\n");
                                    exit(EXIT_FAILURE);

                                break;

                            }

                            obj->out->tracks->array[iTrackMax * 3 + 0] = x;
                            obj->out->tracks->array[iTrackMax * 3 + 1] = y;
                            obj->out->tracks->array[iTrackMax * 3 + 2] = z;
                            obj->out->tracks->ids[iTrackMax] = obj->ids[iTrackMax];
                            strcpy(obj->out->tracks->tags[iTrackMax],obj->tags[iTrackMax]);
                         
                        }
                        
        
                    }

                }
        }
            else {

                tracks_zero(obj->out->tracks);  

            }

       
            obj->out->timeStamp = obj->in2->timeStamp;

            rtnValue = 0;

        }
        else {

            msg_tracks_zero(obj->out);

            rtnValue = -1;

        }

        return rtnValue;

    }

    void mod_sst_connect(mod_sst_obj * obj, msg_pots_obj * in1, msg_targets_obj * in2, msg_tracks_obj * out) {

        obj->in1 = in1;
        obj->in2 = in2;
        obj->out = out;

    }

    void mod_sst_disconnect(mod_sst_obj * obj) {

        obj->in1 = (msg_pots_obj *) NULL;
        obj->in2 = (msg_targets_obj *) NULL;
        obj->out = (msg_tracks_obj *) NULL;

    }

    void mod_sst_enable(mod_sst_obj * obj) {

        obj->enabled = 1;

    }

    void mod_sst_disable(mod_sst_obj * obj) {

        obj->enabled = 0;

    }

    mod_sst_cfg * mod_sst_cfg_construct(void) {

        mod_sst_cfg * cfg;

        cfg = (mod_sst_cfg *) malloc(sizeof(mod_sst_cfg));

        cfg->mode = 0x00;

        cfg->frameSize = 0;
        cfg->halfFrameSize =0;


        cfg->hopSize = 0;
        cfg->fS =0;
        cfg->nBits ;
        cfg->sigmaQ = 0.0f;
        
        cfg->mics       = NULL;   // will be constructed later in parameters_mod_sst_config
        cfg->samplerate = NULL;
        cfg->soundspeed = NULL;
        cfg->gainMin    = 0.0f;

        cfg->nParticles = 0;
        cfg->st_alpha = 0.0f;
        cfg->st_beta = 0.0f;
        cfg->st_ratio = 0.0f;
        cfg->ve_alpha = 0.0f;
        cfg->ve_beta = 0.0f;
        cfg->ve_ratio = 0.0f;
        cfg->ac_alpha = 0.0f;
        cfg->ac_beta = 0.0f;
        cfg->ac_ratio = 0.0f;
        cfg->Nmin = 0.0f;

        cfg->epsilon = 0.0f;
        cfg->sigmaR_prob = 0.0f;
        cfg->sigmaR_active = 0.0f;
        cfg->active_gmm = (gaussians_1d_obj *) NULL;
        cfg->inactive_gmm = (gaussians_1d_obj *) NULL;
        cfg->Pfalse = 0.0f;
        cfg->Pnew = 0.0f;
        cfg->Ptrack = 0.0f;

        cfg->theta_new = 0.0f;
        cfg->N_prob = 0;
        cfg->theta_prob = 0.0f;
        cfg->N_inactive = (unsigned int *) NULL;
        cfg->theta_inactive = 0.0f;
        cfg->enable_classifier_output = 0;  // Default: disabled
        cfg->classifier_log_dir = NULL;     // Will be set from config
        cfg->sim_mode = 0;                  /* Default: Pi/edge mode — no .bin sidecars */
        cfg->min_event_votes = 4;           /* Default: 4/6 hops must agree to emit event */

        return cfg;

    }

    void mod_sst_cfg_destroy(mod_sst_cfg * cfg) {

        if (cfg->active_gmm != NULL) {
            gaussians_1d_destroy(cfg->active_gmm);
        }

        if (cfg->inactive_gmm != NULL) {
            gaussians_1d_destroy(cfg->inactive_gmm);
        }

        if (cfg->N_inactive != NULL) {
            free((void *) cfg->N_inactive);
        }
        
        if (cfg->classifier_log_dir != NULL) {
            free((void *) cfg->classifier_log_dir);
        }

        free((void *) cfg);

    }

    void mod_sst_cfg_printf(const mod_sst_cfg * cfg) {

        unsigned int iTrackMax;

        printf("mode = %c\n", cfg->mode);
        printf("nTracksMax = %u\n", cfg->nTracksMax);
        printf("hopSize = %u\n", cfg->hopSize);
        printf("sigmaQ = %f\n", cfg->sigmaQ);
        printf("nParticles = %u\n", cfg->nParticles);
        printf("st_alpha = %f\n", cfg->st_alpha);
        printf("st_beta = %f\n", cfg->st_beta);
        printf("st_ratio = %f\n", cfg->st_ratio);
        printf("ve_alpha = %f\n", cfg->ve_alpha);
        printf("ve_beta = %f\n", cfg->ve_beta);
        printf("ve_ratio = %f\n", cfg->ve_ratio);
        printf("ac_alpha = %f\n", cfg->ac_alpha);
        printf("ac_beta = %f\n", cfg->ac_beta);
        printf("ac_ratio = %f\n", cfg->ac_ratio);
        printf("Nmin = %f\n", cfg->Nmin);
        printf("epsilon = %f\n", cfg->epsilon);
        printf("sigmaR_prob = %f\n", cfg->sigmaR_prob);
        printf("sigmaR_active = %f\n", cfg->sigmaR_active);

        printf("active_gmm:\n");
        gaussians_1d_printf(cfg->active_gmm);

        printf("inactive_gmm:\n");
        gaussians_1d_printf(cfg->inactive_gmm);

        printf("Pfalse = %f\n", cfg->Pfalse);
        printf("Pnew = %f\n", cfg->Pnew);
        printf("Ptrack = %f\n", cfg->Ptrack);
        printf("theta_new = %f\n", cfg->theta_new);
        printf("N_prob = %u\n", cfg->N_prob);
        printf("theta_prob = %f\n", cfg->theta_prob);

        printf("N_inactive = (");
        for (iTrackMax = 0; iTrackMax < cfg->nTracksMax; iTrackMax++) {

            printf("%u",cfg->N_inactive[iTrackMax]);

            if (iTrackMax != (cfg->nTracksMax-1)) {
                printf(", ");
            }

        }
        printf(")\n");  

        printf("theta_inactive = %f\n", cfg->theta_inactive);      

    }


void push_pot_to_track_buffer(mod_sst_obj* obj,
                              unsigned int iPot,
                              unsigned int iTrack,
                              unsigned long long trackID,
                              unsigned int nFramesPerTrack,
                              int debug) {
    if (trackID == 0) return;
    
    // Safety checks
    if (obj == NULL || obj->in1 == NULL || obj->in1->pots == NULL) {
        if (debug) {
            printf("[ERROR] NULL pointer in push_pot_to_track_buffer\n");
        }
        return;
    }
    
    if (iPot >= obj->nPots) {
        if (debug) {
            printf("[ERROR] Invalid iPot=%u (nPots=%u)\n", iPot, obj->nPots);
        }
        return;
    }
    
    if (iTrack >= obj->nTracksMax) {
        if (debug) {
            printf("[ERROR] Invalid iTrack=%u (nTracksMax=%u)\n", iTrack, obj->nTracksMax);
        }
        return;
    }

    // Prevent writing the same pot multiple times in the same frame
    if (obj->trackSpectra[iTrack].lastFrameSeen == obj->in1->timeStamp) {
        if (debug) {
           // printf("[SKIP] track %llu already written for timeStamp %llu\n", trackID, obj->in1->timeStamp);
        }
        return;
    }

    // Initialize track if needed
    if (obj->ids[iTrack] == 0) {
        obj->ids[iTrack] = trackID;
        obj->trackSpectra[iTrack].count = 0;
        obj->trackSpectra[iTrack].lastFrameSeen = obj->in1->timeStamp;
        obj->trackSpectra[iTrack].id = trackID;
        obj->trackSpectra[iTrack].type = 'P';

        if (obj->trackSpectra[iTrack].buffer == NULL) {
            obj->trackSpectra[iTrack].buffer = (float **)calloc(nFramesPerTrack, sizeof(float *));
            if (obj->trackSpectra[iTrack].buffer == NULL) {
                if (debug) {
                    printf("[ERROR] Failed to allocate buffer pointer array for Track ID %llu\n", trackID);
                }
                return;
            }
        }

        if (debug) {
            //printf("[ALLOC] New buffer initialized for Track ID %llu at index %u\n", trackID, iTrack);
        }
    }

    unsigned int count = obj->trackSpectra[iTrack].count;
    unsigned int h = count % nFramesPerTrack;

    if (obj->in1->pots->spec_at_peak[iPot]) {
        if (obj->trackSpectra[iTrack].buffer[h] == NULL) {
            obj->trackSpectra[iTrack].buffer[h] = (float *)malloc(sizeof(float) * obj->halfFrameSize);
            if (obj->trackSpectra[iTrack].buffer[h] == NULL) {
                if (debug) {
                    printf("[ERROR] Failed to allocate buffer[%u] for Track ID %llu\n", h, trackID);
                }
                return;
            }
        }

        memcpy(obj->trackSpectra[iTrack].buffer[h],
               obj->in1->pots->spec_at_peak[iPot],
               sizeof(float) * obj->halfFrameSize);

        obj->trackSpectra[iTrack].count++;
        obj->trackSpectra[iTrack].lastFrameSeen = obj->in1->timeStamp;

        count = obj->trackSpectra[iTrack].count;

        // Classify every 48 frames once we have the first full 96-frame window.
        // This gives true 50% overlap: hops at count=96,144,192,240,288,336...
        // Each hop covers ~0.96s; 6 hops span ~3.36s of unique audio.
        if (count >= 96 && (count - 96) % 48 == 0) {
            // YAMNet expects 96x257 frames
            const unsigned int YAMNET_BINS = 257;
            
            // Verify spectrum size matches YAMNet's requirements
            if (obj->halfFrameSize != YAMNET_BINS) {
                if (debug) {
                    printf("[ERROR] halfFrameSize=%u but YAMNet expects %u bins. Fix frameSize in config to 512.\n",
                           obj->halfFrameSize, YAMNET_BINS);
                }
                return;
            }
            
            // Flatten last 96 frames into contiguous patch
            printf("[DEBUG] Track %llu frame count=%u - Running YAMNet classification\n", trackID, count);
            float patch[96 * YAMNET_BINS];
            for (unsigned int i = 0; i < 96; i++) {
                unsigned int f = (count - 96 + i) % nFramesPerTrack;
                memcpy(&patch[i * YAMNET_BINS],
                       obj->trackSpectra[iTrack].buffer[f],
                       sizeof(float) * YAMNET_BINS);
            }

            // ---------------------------------------------------------------
            // SIM MODE: Write 96×257 float32 .bin sidecar for this hop.
            // Load in Python: np.fromfile(path, dtype=np.float32).reshape(96,257)
            // Stitch 6 of these with AudioReconstructor.reconstruct_multi_frame()
            // to get ~3s of listenable audio. Skipped on Pi (sim_mode==0).
            // ---------------------------------------------------------------
            if (obj->sim_mode == 1) {
                ensure_log_dir_exists(obj->classifier_log_dir);
                snprintf(obj->last_patch_path[iTrack], 512,
                         "%s/patch_%llu_%llu.bin",
                         obj->classifier_log_dir, trackID, obj->in1->timeStamp);
                FILE *bin_fp = fopen(obj->last_patch_path[iTrack], "wb");
                if (bin_fp) {
                    fwrite(patch, sizeof(float), 96 * YAMNET_BINS, bin_fp);
                    fclose(bin_fp);
                } else {
                    perror("[WARN] Failed to write .bin sidecar");
                    obj->last_patch_path[iTrack][0] = '\0';
                }
            } else {
                obj->last_patch_path[iTrack][0] = '\0';
            }

            int class_id;
            const char* class_name;   // plain C string
            float confidence;

            // ===============================================================
            // TOP-K CLASSIFICATION & ROLLING HISTORY BUFFER
            // ===============================================================
            int topk_ids[TOPK];
            float topk_confs[TOPK];
            
            // Call Top-K patch-based C API
            if (yamnet_classify_patch_topk(obj->yamnet, patch, topk_ids, topk_confs, TOPK)) {
                // Store Top-K results in rolling buffer
                int hop_idx = obj->topk_head[iTrack];
                topk_hop_t *hop = &obj->topk_history[iTrack][hop_idx];
                
                hop->timestamp = obj->in1->timeStamp;
                for (int k = 0; k < TOPK; k++) {
                    hop->class_ids[k] = topk_ids[k];
                    hop->confidences[k] = topk_confs[k];
                }
                
                // Advance circular buffer
                obj->topk_head[iTrack] = (hop_idx + 1) % ROLLING_HOPS;
                if (obj->topk_count[iTrack] < ROLLING_HOPS) {
                    obj->topk_count[iTrack]++;
                }
                
                // Update primary classification (top-1 for backward compatibility)
                class_id = topk_ids[0];
                confidence = topk_confs[0];
                class_name = yamnet_class_name_from_id(obj->yamnet, class_id);
                
                printf("[YAMNET Top-K] Track %llu: Top5=[", trackID);
                for (int k = 0; k < TOPK; k++) {
                    const char* cn = yamnet_class_name_from_id(obj->yamnet, topk_ids[k]);
                    printf("%s(%.3f)%s", cn, topk_confs[k], k < TOPK-1 ? ", " : "");
                }
                printf("]\n");
                
                // Update last class tracking
                obj->last_class_id[iTrack] = class_id;
                obj->last_class_conf[iTrack] = confidence;
                obj->last_class_ts[iTrack] = obj->in1->timeStamp;
            } else if (yamnet_classify_patch(obj->yamnet, patch, &class_id, &class_name, &confidence)) {
                // Fallback to single-class — still populate the rolling buffer
                // so topk_count advances and the event gate can be satisfied.
                printf("[YAMNET] Track %llu: class='%s' (id=%d) confidence=%.3f\n",
                       trackID, class_name ? class_name : "unknown", class_id, confidence);

                int hop_idx = obj->topk_head[iTrack];
                topk_hop_t *hop = &obj->topk_history[iTrack][hop_idx];
                hop->timestamp = obj->in1->timeStamp;
                hop->class_ids[0]   = class_id;
                hop->confidences[0] = confidence;
                for (int k = 1; k < TOPK; k++) {
                    hop->class_ids[k]   = -1;
                    hop->confidences[k] = 0.0f;
                }
                obj->topk_head[iTrack] = (hop_idx + 1) % ROLLING_HOPS;
                if (obj->topk_count[iTrack] < ROLLING_HOPS)
                    obj->topk_count[iTrack]++;

                obj->last_class_id[iTrack]   = class_id;
                obj->last_class_conf[iTrack]  = confidence;
                obj->last_class_ts[iTrack]    = obj->in1->timeStamp;
            }
            // ===============================================================
            
            // Simplified classification acceptance (no streaks needed with rolling buffer)
            const float threshold = 0.30f;
            if (confidence > threshold) {
                // Fetch current (Kalman) position for this track
                float tx = 0.0f, ty = 0.0f, tz = 0.0f;
                if (obj->out != NULL && obj->out->tracks != NULL) {
                    unsigned int idx = iTrack * 3;
                    tx = obj->out->tracks->array[idx + 0];
                    ty = obj->out->tracks->array[idx + 1];
                    tz = obj->out->tracks->array[idx + 2];
                }

                printf("[SST Audio Event] pos=(%6.2f, %6.2f, %6.2f) class=%s conf=%.2f\n",
                       tx, ty, tz,
                       class_name ? class_name : "(unknown)",
                       confidence);

                // Upgrade P type to A if confidence is strong
                if (obj->type[iTrack] == 'P' && confidence > threshold) {
                    obj->type[iTrack] = 'A';
                    printf("[SST UPGRADE] Track %llu: P -> A (confidence %.2f)\n",
                           trackID, confidence);
                }
            } else if (debug) {
                printf("[SST DEBUG] Track %llu: %s (%.2f) below threshold %.2f\n",
                       trackID, class_name ? class_name : "unknown", confidence, threshold);
            }
        }

        // Optional: keep your bucket analysis/debug print here
        if (debug) {
            // … existing bucket averaging code …
        }
    }
}


void reset_track_slot(mod_sst_obj *obj, unsigned int iTrackMax, unsigned int nFramesPerTrack) {
    obj->ids[iTrackMax] = 0;
    obj->trackSpectra[iTrackMax].id = 0;
    obj->trackSpectra[iTrackMax].count = 0;
    obj->trackSpectra[iTrackMax].type = 0;
    obj->trackSpectra[iTrackMax].lastFrameSeen = 0;
    strcpy(obj->tags[iTrackMax], "");

    // Reset classification state
    obj->last_class_id[iTrackMax] = -1;
    obj->last_class_conf[iTrackMax] = 0.0f;
    obj->last_class_ts[iTrackMax] = 0;
    
    // Clear rolling Top-K history buffer
    obj->topk_head[iTrackMax] = 0;
    obj->topk_count[iTrackMax] = 0;
    for (int h = 0; h < ROLLING_HOPS; h++) {
        obj->topk_history[iTrackMax][h].timestamp = 0;
        for (int k = 0; k < TOPK; k++) {
            obj->topk_history[iTrackMax][h].class_ids[k] = -1;
            obj->topk_history[iTrackMax][h].confidences[k] = 0.0f;
        }
    }
    if (obj->last_patch_path && obj->last_patch_path[iTrackMax]) {
        obj->last_patch_path[iTrackMax][0] = '\0';
    }

    for (unsigned int f = 0; f < nFramesPerTrack; f++) {
        if (obj->trackSpectra[iTrackMax].buffer[f]) {
            memset(obj->trackSpectra[iTrackMax].buffer[f], 0x00, sizeof(float) * obj->halfFrameSize);
        }
    }
}

void ensure_log_dir_exists(const char* log_dir) {
    struct stat st = {0};
    if (stat(log_dir, &st) == -1) {
        mkdir(log_dir, 0755);
        printf("[LOG] Created missing log directory: %s\n", log_dir);
    }
}


/* ---------------------------------------------------------------------------
 * compute_event — Summarise the ROLLING_HOPS Top-1 predictions for iTrack.
 *
 * Reads the circular topk_history buffer (up to ROLLING_HOPS entries), finds
 * the mode (most-frequent top-1 class_id), counts how many hops agreed, and
 * averages the confidence of those agreeing hops only.
 *
 * Returns sst_event_t{class_id=-1, votes=0, avg_conf=0} when no hops filled.
 * ---------------------------------------------------------------------------*/
static sst_event_t compute_event(const mod_sst_obj *obj, unsigned int iTrack) {
    sst_event_t ev = {-1, 0, 0.0f, 0.0f};
    int n = obj->topk_count[iTrack];
    if (n == 0) return ev;

    /* -----------------------------------------------------------------------
     * Expand the vote pool to all top-K entries across every hop.
     * pool_ids[i] / pool_confs[i] / pool_hop[i] — one entry per (hop, rank).
     * pool_hop lets us count distinct hops later (for event_votes semantics).
     * ----------------------------------------------------------------------- */
    const int MAX_POOL = ROLLING_HOPS * TOPK;
    int   pool_ids[ROLLING_HOPS * TOPK];
    float pool_confs[ROLLING_HOPS * TOPK];
    int   pool_hop[ROLLING_HOPS * TOPK];   /* which hop index each entry came from */
    int   pool_size = 0;

    int start = (obj->topk_head[iTrack] + ROLLING_HOPS - n) % ROLLING_HOPS;
    for (int h = 0; h < n; h++) {
        int hop_idx = (start + h) % ROLLING_HOPS;
        const topk_hop_t *hop = &obj->topk_history[iTrack][hop_idx];
        for (int k = 0; k < TOPK; k++) {
            if (hop->class_ids[k] < 0) break;   /* -1 sentinel = no more entries */
            pool_ids[pool_size]   = hop->class_ids[k];
            pool_confs[pool_size] = hop->confidences[k];
            pool_hop[pool_size]   = h;
            pool_size++;
        }
    }

    if (pool_size == 0) return ev;

    /* -----------------------------------------------------------------------
     * For each candidate class: count distinct hops it appeared in (hop_votes)
     * and average its confidence across all (hop, rank) appearances.
     * Winner = highest hop_votes; tie-break = highest avg_confidence.
     * hop_votes keeps the 0–ROLLING_HOPS range so min_event_votes stays sane.
     * ----------------------------------------------------------------------- */
    int   best_id        = -1;
    int   best_hop_votes = 0;
    float best_avg_conf  = -1.0f;
    float best_max_conf  = 0.0f;  /* peak single-hop top-1 conf for winner */

    for (int i = 0; i < pool_size; i++) {
        int cid = pool_ids[i];
        if (cid < 0) continue;

        /* Count distinct hops and accumulate confidence for this class */
        int   seen_hops[ROLLING_HOPS];
        int   n_seen     = 0;
        int   hop_votes  = 0;
        float conf_sum   = 0.0f;
        int   conf_count = 0;
        float max_c      = 0.0f;

        for (int j = 0; j < pool_size; j++) {
            if (pool_ids[j] != cid) continue;
            conf_sum += pool_confs[j];
            conf_count++;
            if (pool_confs[j] > max_c) max_c = pool_confs[j];

            /* Track distinct hops */
            int already = 0;
            for (int s = 0; s < n_seen; s++) if (seen_hops[s] == pool_hop[j]) { already = 1; break; }
            if (!already) { seen_hops[n_seen++] = pool_hop[j]; hop_votes++; }
        }

        float avg_conf = (conf_count > 0) ? conf_sum / (float)conf_count : 0.0f;

        if (hop_votes > best_hop_votes ||
            (hop_votes == best_hop_votes && avg_conf > best_avg_conf)) {
            best_hop_votes = hop_votes;
            best_id        = cid;
            best_avg_conf  = avg_conf;
            best_max_conf  = max_c;
        }
    }

    ev.class_id = best_id;
    ev.votes    = best_hop_votes;
    ev.avg_conf = best_avg_conf;
    ev.max_conf = best_max_conf;
    return ev;
}


void dump_track_buffers_to_json(mod_sst_obj *obj, const char *basename, unsigned int nFramesPerTrack) {
    ensure_log_dir_exists(obj->classifier_log_dir);
    int DEBUG_LOGS_ENABLED = 0;  // Disable verbose debug logs

    // Initialize session timestamp once
    if (session_start == 0) {
        session_start = (unsigned long long)time(NULL);
    }

    json_object *root = json_object_new_object();
    json_object_object_add(root, "timeStamp", json_object_new_int64(obj->in1->timeStamp));

    json_object *src_array = json_object_new_array();
    int nDumped = 0;

    for (unsigned int i = 0; i < obj->nTracksMax; i++) {
        if (obj->ids[i] == 0 || obj->trackSpectra[i].buffer == NULL) continue;

        /* Always emit position/activity so the simulator can draw tracks.
         * Event fields are added only when the rolling buffer is full AND
         * votes >= min_event_votes.  Frames without an event still appear
         * in the JSON so tracking visualisation works normally. */

        json_object *track_obj = json_object_new_object();
        json_object_object_add(track_obj, "id",  json_object_new_int64(obj->ids[i]));
        json_object_object_add(track_obj, "tag", json_object_new_string(obj->tags[i]));

        // Direction (x, y, z)
        json_object_object_add(track_obj, "x", json_object_new_double(obj->out->tracks->array[i * 3 + 0]));
        json_object_object_add(track_obj, "y", json_object_new_double(obj->out->tracks->array[i * 3 + 1]));
        json_object_object_add(track_obj, "z", json_object_new_double(obj->out->tracks->array[i * 3 + 2]));

        json_object_object_add(track_obj, "activity",    json_object_new_double(obj->sourceActivities[i]));
        json_object_object_add(track_obj, "type",        json_object_new_string_len(&obj->type[i], 1));
        json_object_object_add(track_obj, "frame_count", json_object_new_int(obj->trackSpectra[i].count));

        /* Event gate — emit event fields once the track has at least one
         * classification (topk_count >= 1).  We do NOT require a full buffer
         * of ROLLING_HOPS because the base YAMNet model may not agree across
         * hops for unseen classes; that is exactly why we are collecting data.
         * min_event_votes is still checked so the JSON label reflects the
         * most-agreed class, but with min_event_votes=1 every hop qualifies.
         * The Python curator assigns the correct label from ground truth. */
        int has_event = 0;
        sst_event_t ev = {-1, 0, 0.0f, 0.0f};
        if (obj->topk_count[i] >= 1) {
            ev = compute_event(obj, i);
            if (ev.votes >= obj->min_event_votes) has_event = 1;
        }

        if (has_event) {
        // ---------------------------------------------------------------
        // EVENT FIELDS — top-K × N-hop voting (compute_event)
        // event_class_id / event_class_name: winner (most distinct-hop votes,
        //   tie-broken by highest avg_confidence across all top-K appearances)
        // event_votes: distinct hops the winner appeared in (0–ROLLING_HOPS)
        // event_avg_confidence: mean confidence across all (hop,rank) entries
        //   for the winning class
        // event_candidates: full ranked list so the visualizer can show the
        //   complete vote distribution
        // ---------------------------------------------------------------
        {
            const char *ev_name = yamnet_class_name_from_id(obj->yamnet, ev.class_id);
            json_object_object_add(track_obj, "event_class_id",
                json_object_new_int(ev.class_id));
            json_object_object_add(track_obj, "event_class_name",
                json_object_new_string(ev_name ? ev_name : "unknown"));
            json_object_object_add(track_obj, "event_votes",
                json_object_new_int(ev.votes));
            json_object_object_add(track_obj, "event_avg_confidence",
                json_object_new_double(ev.avg_conf));
            /* Peak single-hop confidence — less diluted than avg when only a few
             * hops had strong signal.  Use this as the primary display metric. */
            json_object_object_add(track_obj, "event_max_confidence",
                json_object_new_double(ev.max_conf));
        }

        // ---------------------------------------------------------------
        // EVENT CANDIDATES — ranked list of all classes that appeared in
        // the top-K pool, sorted descending by hop_votes then avg_conf.
        // Each entry: { class_id, class_name, hop_votes, avg_confidence }
        // ---------------------------------------------------------------
        {
            /* Re-build pool from circular buffer */
            int   pool_ids[ROLLING_HOPS * TOPK];
            float pool_confs[ROLLING_HOPS * TOPK];
            int   pool_hop[ROLLING_HOPS * TOPK];
            int   pool_size = 0;
            int   n_hops = obj->topk_count[i];
            int   ps = (obj->topk_head[i] + ROLLING_HOPS - n_hops) % ROLLING_HOPS;
            for (int h = 0; h < n_hops; h++) {
                int hi = (ps + h) % ROLLING_HOPS;
                const topk_hop_t *hp = &obj->topk_history[i][hi];
                for (int k = 0; k < TOPK; k++) {
                    if (hp->class_ids[k] < 0) break;
                    pool_ids[pool_size]   = hp->class_ids[k];
                    pool_confs[pool_size] = hp->confidences[k];
                    pool_hop[pool_size]   = h;
                    pool_size++;
                }
            }

            /* Collect unique class ids */
            int   uniq_ids[ROLLING_HOPS * TOPK];
            int   uniq_votes[ROLLING_HOPS * TOPK];
            float uniq_conf_sum[ROLLING_HOPS * TOPK];
            int   uniq_conf_cnt[ROLLING_HOPS * TOPK];
            int   n_uniq = 0;

            for (int p = 0; p < pool_size; p++) {
                int cid = pool_ids[p];
                int found = -1;
                for (int u = 0; u < n_uniq; u++) if (uniq_ids[u] == cid) { found = u; break; }
                if (found < 0) {
                    found = n_uniq++;
                    uniq_ids[found]       = cid;
                    uniq_votes[found]     = 0;
                    uniq_conf_sum[found]  = 0.0f;
                    uniq_conf_cnt[found]  = 0;
                    /* Count distinct hops for this class */
                    int seen[ROLLING_HOPS]; int ns = 0;
                    for (int q = 0; q < pool_size; q++) {
                        if (pool_ids[q] != cid) continue;
                        uniq_conf_sum[found] += pool_confs[q];
                        uniq_conf_cnt[found]++;
                        int dup = 0;
                        for (int s = 0; s < ns; s++) if (seen[s] == pool_hop[q]) { dup = 1; break; }
                        if (!dup) { seen[ns++] = pool_hop[q]; uniq_votes[found]++; }
                    }
                }
            }

            /* Simple insertion sort: descending hop_votes, tie-break avg_conf */
            for (int a = 1; a < n_uniq; a++) {
                int   tid_v   = uniq_ids[a];
                int   tvotes  = uniq_votes[a];
                float tsum    = uniq_conf_sum[a];
                int   tcnt    = uniq_conf_cnt[a];
                int b = a - 1;
                float avg_a = (tcnt > 0) ? tsum / (float)tcnt : 0.0f;
                while (b >= 0) {
                    float avg_b = (uniq_conf_cnt[b] > 0) ?
                                  uniq_conf_sum[b] / (float)uniq_conf_cnt[b] : 0.0f;
                    if (uniq_votes[b] > tvotes ||
                        (uniq_votes[b] == tvotes && avg_b >= avg_a)) break;
                    uniq_ids[b+1]      = uniq_ids[b];
                    uniq_votes[b+1]    = uniq_votes[b];
                    uniq_conf_sum[b+1] = uniq_conf_sum[b];
                    uniq_conf_cnt[b+1] = uniq_conf_cnt[b];
                    b--;
                }
                uniq_ids[b+1]      = tid_v;
                uniq_votes[b+1]    = tvotes;
                uniq_conf_sum[b+1] = tsum;
                uniq_conf_cnt[b+1] = tcnt;
            }

            json_object *candidates_arr = json_object_new_array();
            for (int u = 0; u < n_uniq; u++) {
                float avg_c = (uniq_conf_cnt[u] > 0) ?
                              uniq_conf_sum[u] / (float)uniq_conf_cnt[u] : 0.0f;
                const char *cn = yamnet_class_name_from_id(obj->yamnet, uniq_ids[u]);
                json_object *cand = json_object_new_object();
                json_object_object_add(cand, "class_id",
                    json_object_new_int(uniq_ids[u]));
                json_object_object_add(cand, "class_name",
                    json_object_new_string(cn ? cn : "unknown"));
                json_object_object_add(cand, "hop_votes",
                    json_object_new_int(uniq_votes[u]));
                json_object_object_add(cand, "avg_confidence",
                    json_object_new_double(avg_c));
                json_object_array_add(candidates_arr, cand);
            }
            json_object_object_add(track_obj, "event_candidates", candidates_arr);
        }

        // ---------------------------------------------------------------
        // EXPORT 6-HOP ROLLING TOP-K HISTORY
        // ---------------------------------------------------------------
        json_object *topk_history_array = json_object_new_array();
        int valid_hops = obj->topk_count[i];  /* always == ROLLING_HOPS here */
        {
            int start_idx = (obj->topk_head[i] + ROLLING_HOPS - valid_hops) % ROLLING_HOPS;
            for (int h = 0; h < valid_hops; h++) {
                int hop_idx = (start_idx + h) % ROLLING_HOPS;
                topk_hop_t *hop = &obj->topk_history[i][hop_idx];

                json_object *hop_obj = json_object_new_object();
                json_object_object_add(hop_obj, "timestamp",
                    json_object_new_int64(hop->timestamp));

                json_object *class_ids_arr   = json_object_new_array();
                json_object *class_names_arr = json_object_new_array();
                json_object *confidences_arr = json_object_new_array();
                for (int k = 0; k < TOPK; k++) {
                    json_object_array_add(class_ids_arr,
                        json_object_new_int(hop->class_ids[k]));
                    const char *cn = yamnet_class_name_from_id(obj->yamnet, hop->class_ids[k]);
                    json_object_array_add(class_names_arr,
                        json_object_new_string(cn ? cn : "unknown"));
                    json_object_array_add(confidences_arr,
                        json_object_new_double(hop->confidences[k]));
                }
                json_object_object_add(hop_obj, "class_ids",   class_ids_arr);
                json_object_object_add(hop_obj, "class_names", class_names_arr);
                json_object_object_add(hop_obj, "confidences", confidences_arr);
                json_object_array_add(topk_history_array, hop_obj);
            }
        }
        json_object_object_add(track_obj, "topk_history", topk_history_array);

        // ---------------------------------------------------------------
        // SPECTRA FILE — path to 96×257 float32 .bin sidecar (sim_mode=1 only)
        // Python: np.fromfile(path, dtype=np.float32).reshape(96, 257)
        // Empty string on Pi (sim_mode=0) — load from topk_history instead.
        // ---------------------------------------------------------------
        json_object_object_add(track_obj, "spectra_file",
            json_object_new_string(
                (obj->last_patch_path &&
                 obj->last_patch_path[i] &&
                 obj->last_patch_path[i][0] != '\0')
                ? obj->last_patch_path[i] : ""));

        } /* end if (has_event) */

        json_object_array_add(src_array, track_obj);
        nDumped++;
    }

    json_object_object_add(root, "src", src_array);

    // Human-readable filename
    time_t ts = (time_t)session_start;
    struct tm *tm_info = localtime(&ts);
    char ts_str[64];
    strftime(ts_str, sizeof(ts_str), "%Y%m%d_%H%M%S", tm_info);

    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s/%s_%s.json", obj->classifier_log_dir, basename, ts_str);

    FILE *fp = fopen(fullpath, "a");
    if (!fp) {
        perror("[ERROR] Failed to open session JSON file");
    } else {
        fprintf(fp, "%s\n", json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
        fclose(fp);
    }

    json_object_put(root);
}


void log_classification_event(mod_sst_obj *obj, unsigned int iTrack, unsigned long long trackID) {
    ensure_log_dir_exists(obj->classifier_log_dir);
    int DEBUG_LOGS_ENABLED = 1;

    // Initialize session timestamp once
    if (session_start == 0) {
        session_start = (unsigned long long)time(NULL);
    }



    // Human-readable filename
    time_t ts = (time_t)session_start;
    struct tm *tm_info = localtime(&ts);
    char ts_str[64];
    strftime(ts_str, sizeof(ts_str), "%Y%m%d_%H%M%S", tm_info);

    char fullpath[512];
    //snprintf(fullpath, sizeof(fullpath), "%s/sst_classify_events_%s.json", LOG_DIR, ts_str);

    FILE *fp = fopen(fullpath, "a");
    if (!fp) {
        //perror("[ERROR] Failed to open classification events file");
        return;
    }

    // Build event JSON
    json_object *event = json_object_new_object();
    json_object_object_add(event, "timeStamp", json_object_new_int64(obj->in1->timeStamp));
    json_object_object_add(event, "track_id", json_object_new_int64(trackID));
    json_object_object_add(event, "frame_count", json_object_new_int(obj->trackSpectra[iTrack].count));
    json_object_object_add(event, "type", json_object_new_string_len(&obj->type[iTrack], 1));
    json_object_object_add(event, "tag", json_object_new_string(obj->tags[iTrack]));
    json_object_object_add(event, "activity", json_object_new_double(obj->sourceActivities[iTrack]));

    fprintf(fp, "%s\n", json_object_to_json_string_ext(event, JSON_C_TO_STRING_PLAIN));
    fclose(fp);
    json_object_put(event);

}



void dump_track_fingerprint_only(mod_sst_obj *obj,
                                 const char *basename,
                                 unsigned int nFramesPerTrack) {
    ensure_log_dir_exists(obj->classifier_log_dir);

    if (session_start == 0) {
        session_start = (unsigned long long)time(NULL);
    }

    // Human-readable filename
    time_t ts = (time_t)session_start;
    struct tm *tm_info = localtime(&ts);
    char ts_str[64];
    strftime(ts_str, sizeof(ts_str), "%Y%m%d_%H%M%S", tm_info);

    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s/%s_fingerprint_%s.json",
             obj->classifier_log_dir, basename, ts_str);

    FILE *fp = fopen(fullpath, "a");
    if (!fp) {
        perror("[ERROR] Failed to open fingerprint JSON file");
        return;
    }

    for (unsigned int i = 0; i < obj->nTracksMax; i++) {
        if (obj->ids[i] == 0 || obj->trackSpectra[i].buffer == NULL) continue;

        unsigned int latestFrame = (obj->trackSpectra[i].count > 0)
                                   ? (obj->trackSpectra[i].count - 1) % nFramesPerTrack
                                   : 0;
        float *bins = obj->trackSpectra[i].buffer[latestFrame];

        // Match xcorr2true_spectrum_at_peak: search only up to hopSize/4
        unsigned int nBins = obj->halfFrameSize;   // full FFT bins (257 for frameSize=512)


        // Find top 3 bins
        typedef struct { unsigned int bin; float val; } binval;
        binval top[3] = {{0, -1e9}, {0, -1e9}, {0, -1e9}};
        for (unsigned int k = 0; k < nBins; k++) {
            float v = bins[k];
            if (v > top[0].val) {
                top[2] = top[1];
                top[1] = top[0];
                top[0] = (binval){k, v};
            } else if (v > top[1].val) {
                top[2] = top[1];
                top[1] = (binval){k, v};
            } else if (v > top[2].val) {
                top[2] = (binval){k, v};
            }
        }

        
        // Build JSON object
        json_object *track_obj = json_object_new_object();
        json_object_object_add(track_obj, "timeStamp", json_object_new_int64(obj->in1->timeStamp));
        json_object_object_add(track_obj, "id", json_object_new_int64(obj->ids[i]));
        json_object_object_add(track_obj, "tag", json_object_new_string(obj->tags[i]));
        json_object_object_add(track_obj, "x", json_object_new_double(obj->out->tracks->array[i * 3 + 0]));
        json_object_object_add(track_obj, "y", json_object_new_double(obj->out->tracks->array[i * 3 + 1]));
        json_object_object_add(track_obj, "z", json_object_new_double(obj->out->tracks->array[i * 3 + 2]));

        for (int t = 0; t < 3; t++) {
            char bin_key[32], freq_key[32], val_key[32];
            snprintf(bin_key, sizeof(bin_key), "top_bin%d", t + 1);
            snprintf(freq_key, sizeof(freq_key), "freq%d", t + 1);
            snprintf(val_key, sizeof(val_key), "val%d", t + 1);

            // Match xcorr2true_spectrum_at_peak: f = bin * fS / (hopSize/2)
            //double freq = ((double)top[t].bin * obj->fS) / ((double)obj->hopSize / 2.0);
            double freq = ((double)top[t].bin * obj->fS) / (double)obj->frameSize;
            json_object_object_add(track_obj, bin_key, json_object_new_int(top[t].bin));
            json_object_object_add(track_obj, freq_key, json_object_new_double(freq));
            json_object_object_add(track_obj, val_key, json_object_new_double(top[t].val));
        }

        // One line per track
        fprintf(fp, "%s\n", json_object_to_json_string_ext(track_obj, JSON_C_TO_STRING_PLAIN));
        json_object_put(track_obj);
    }

    fclose(fp);
}

