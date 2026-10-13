   
   /**
    * \file     track.c
    */
    
    #include <signal/track.h>

  tracks_obj * tracks_construct_zero(const unsigned int nTracks) {

    tracks_obj * obj;
    unsigned int iTrack;

    obj = (tracks_obj *) malloc(sizeof(tracks_obj));

    obj->nTracks = nTracks;

    // Allocate position array: [x, y, z] per track
    obj->array = (float *) malloc(sizeof(float) * 3 * nTracks);
    memset(obj->array, 0x00, sizeof(float) * 3 * nTracks);

    // Allocate velocity array: [vx, vy, vz] per track
    obj->velocity = (float *) malloc(sizeof(float) * 3 * nTracks);
    memset(obj->velocity, 0x00, sizeof(float) * 3 * nTracks);

    // Allocate track IDs
    obj->ids = (unsigned long long *) malloc(sizeof(unsigned long long) * nTracks);
    memset(obj->ids, 0x00, sizeof(unsigned long long) * nTracks);

    // Allocate tags
    obj->tags = (char **) malloc(sizeof(char *) * nTracks);
    for (iTrack = 0; iTrack < nTracks; iTrack++) {
        obj->tags[iTrack] = (char *) malloc(sizeof(char) * 256);
        strcpy(obj->tags[iTrack], "");
    }

    // Allocate activity scores
    obj->activity = (float *) malloc(sizeof(float) * nTracks);
    memset(obj->activity, 0x00, sizeof(float) * nTracks);

    // Allocate YAMNet top-1 classification
    obj->class_name = (char **) malloc(sizeof(char *) * nTracks);
    obj->class_conf  = (float *) malloc(sizeof(float) * nTracks);
    for (iTrack = 0; iTrack < nTracks; iTrack++) {
        obj->class_name[iTrack] = (char *) malloc(sizeof(char) * 256);
        strcpy(obj->class_name[iTrack], "");
        obj->class_conf[iTrack] = 0.0f;
    }

    return obj;
}


    void tracks_destroy(tracks_obj * obj) {

        unsigned int iTrack;

        free((void *) obj->array);
        free((void *) obj->velocity);
        free((void *) obj->ids);

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {
            free((void *) obj->tags[iTrack]);
        }
        free((void *) obj->tags);

        free((void *) obj->activity);

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {
            free((void *) obj->class_name[iTrack]);
        }
        free((void *) obj->class_name);
        free((void *) obj->class_conf);

        free((void *) obj);

    }

    tracks_obj * tracks_clone(const tracks_obj * obj) {

        tracks_obj * clone;
        unsigned int iTrack;

        clone = (tracks_obj *) malloc(sizeof(tracks_obj));

        clone->nTracks = obj->nTracks;
        clone->array = (float *) malloc(sizeof(float) * 3 * obj->nTracks);
        memcpy(clone->array, obj->array, sizeof(float) * 3 * obj->nTracks);
        clone->ids = (unsigned long long *) malloc(sizeof(unsigned long long) * obj->nTracks);
        memcpy(clone->ids, obj->ids, sizeof(unsigned long long) * obj->nTracks);

        clone->tags = (char **) malloc(sizeof(char *) * obj->nTracks);

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {

            clone->tags[iTrack] = (char *) malloc(sizeof(char) * 256);
            strcpy(clone->tags[iTrack], "");
            
        }

        clone->activity = (float *) malloc(sizeof(float) * obj->nTracks);
        memcpy(clone->activity, obj->activity, sizeof(float) * obj->nTracks);

        clone->class_name = (char **) malloc(sizeof(char *) * obj->nTracks);
        clone->class_conf  = (float *) malloc(sizeof(float) * obj->nTracks);
        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {
            clone->class_name[iTrack] = (char *) malloc(sizeof(char) * 256);
            strcpy(clone->class_name[iTrack], obj->class_name[iTrack]);
            clone->class_conf[iTrack] = obj->class_conf[iTrack];
        }

        return clone;

    }

    void tracks_copy(tracks_obj * dest, const tracks_obj * src) {

        unsigned int iTrack;

        dest->nTracks = src->nTracks;
        memcpy(dest->array, src->array, sizeof(float) * 3 * src->nTracks);
        memcpy(dest->ids, src->ids, sizeof(unsigned long long) * src->nTracks);
        
        for (iTrack = 0; iTrack < src->nTracks; iTrack++) {

            strcpy(dest->tags[iTrack], src->tags[iTrack]);

        }

        memcpy(dest->activity, src->activity, sizeof(float) * src->nTracks);

        for (iTrack = 0; iTrack < src->nTracks; iTrack++) {
            strcpy(dest->class_name[iTrack], src->class_name[iTrack]);
            dest->class_conf[iTrack] = src->class_conf[iTrack];
        }

    }

    void tracks_zero(tracks_obj * obj) {

        unsigned int iTrack;

        memset(obj->array, 0x00, sizeof(float) * 3 * obj->nTracks);
        memset(obj->ids, 0x00, sizeof(unsigned long long) * obj->nTracks);       

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {
            
            strcpy(obj->tags[iTrack], "");

        }

        memset(obj->activity, 0x00, sizeof(float) * obj->nTracks);

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {
            strcpy(obj->class_name[iTrack], "");
            obj->class_conf[iTrack] = 0.0f;
        }

    }

    void tracks_printf(const tracks_obj * obj) {

        unsigned int iTrack;

        for (iTrack = 0; iTrack < obj->nTracks; iTrack++) {

            printf("(%04llu)-[%s]: %+1.3f %+1.3f %+1.3f - %1.3f | %s %.2f\n",
                   obj->ids[iTrack],
                   obj->tags[iTrack],
                   obj->array[iTrack * 3 + 0],
                   obj->array[iTrack * 3 + 1],
                   obj->array[iTrack * 3 + 2],
                   obj->activity[iTrack],
                   obj->class_name[iTrack][0] ? obj->class_name[iTrack] : "(none)",
                   obj->class_conf[iTrack]);
            
        }

    }
