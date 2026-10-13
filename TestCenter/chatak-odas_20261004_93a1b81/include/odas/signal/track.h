#ifndef __ODAS_SIGNAL_TRACK
#define __ODAS_SIGNAL_TRACK

   /**
    * \file     track.h

    */
    
    #include <stdlib.h>
    #include <string.h>
    #include <stdio.h>

    typedef struct tracks_obj {

        unsigned int nTracks;
        unsigned long long * ids;
        char ** tags;
        float * array;
        float * velocity; // [vx, vy, vz] per track
        float * activity;

        /* YAMNet top-1 classification per track.
         * class_name[i] is a heap-allocated string (e.g. "Speech").
         * Empty string "" means no classification yet.
         * class_conf[i] is the confidence in [0,1]. */
        char ** class_name;
        float * class_conf;
        
    } tracks_obj;

    tracks_obj * tracks_construct_zero(const unsigned int nTracks);

    void tracks_destroy(tracks_obj * obj);

    tracks_obj * tracks_clone(const tracks_obj * obj);

    void tracks_copy(tracks_obj * dest, const tracks_obj * src);

    void tracks_zero(tracks_obj * obj);

    void tracks_printf(const tracks_obj * obj);

#endif
