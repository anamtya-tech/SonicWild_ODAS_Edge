#ifndef __ODAS_SYSTEM_XCORR_SPEC
#define __ODAS_SYSTEM_XCORR_SPEC

   /**
    * \file     xcorr2spec.h
*/

    #include <stdlib.h>
    #include <string.h>
    #include <time.h>
    #include <math.h>

    #include <signal/delta.h>
    #include <signal/xcorr.h>
    #include <signal/tdoa.h>
    #include <signal/pair.h>

    void xcorr2freq_at_peak(float **interp_products, unsigned int *tdoa_array, unsigned int n_pairs, unsigned int frame_size, unsigned int peak_index, float *spec_output);
    //void xcorr2freq_at_peak(float **interp_products, float **precomputed_tdoas, unsigned int dir_index, unsigned int n_pairs, unsigned int frame_size, float *spec_output);


#endif
