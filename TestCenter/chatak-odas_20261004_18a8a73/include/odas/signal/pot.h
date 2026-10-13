#ifndef __ODAS_SIGNAL_POT
#define __ODAS_SIGNAL_POT

   /**
    * \file     pot.h

    */

    #include <stdlib.h>
    #include <string.h>
    #include <stdio.h>
    
    #define N_BINS 1024  // Adjust to match your interpolated frame resolution

    typedef struct pots_obj {

        unsigned int nPots;          // number of detected peaks (usually 4)
        float *array;                // [nPots * 4] ? x, y, z, confidence
        float **spec_at_peak;  // NEW: freq-bin magnitudes per peak

    } pots_obj;

    pots_obj * pots_construct_zero(const unsigned int nPots);

    pots_obj * pots_clone(const pots_obj * obj);

    void pots_copy(pots_obj * dest, const pots_obj * src);

    void pots_zero(pots_obj * obj);

    void pots_destroy(pots_obj * obj);

    void pots_printf(const pots_obj * obj);
    
    void pots_log_to_file(const pots_obj * obj, const char * base_dir, unsigned long long timestamp);

#endif
