
    
    #include <message/msg_spectra.h>

    msg_spectra_obj * msg_spectra_construct(const msg_spectra_cfg * msg_spectra_config) {

        msg_spectra_obj * obj;

        obj = (msg_spectra_obj *) malloc(sizeof(msg_spectra_obj));

        obj->timeStamp = 0;
        obj->fS = msg_spectra_config->fS;
        obj->freqs = freqs_construct_zero(msg_spectra_config->nChannels, msg_spectra_config->halfFrameSize);

        return obj;

    }

    void msg_spectra_destroy(msg_spectra_obj * obj) {

        freqs_destroy(obj->freqs);
        free((void *) obj);

    }

    void msg_spectra_copy(msg_spectra_obj * dest, const msg_spectra_obj * src) {

        dest->timeStamp = src->timeStamp;
        dest->fS = src->fS;
        freqs_copy(dest->freqs, src->freqs);

    }

    void msg_spectra_zero(msg_spectra_obj * obj) {

        obj->timeStamp = 0;
        obj->fS = 0;
        freqs_zero(obj->freqs);

    }

    unsigned int msg_spectra_isZero(const msg_spectra_obj * obj) {

        unsigned int rtnValue;

        if (obj->timeStamp == 0) {
            rtnValue = 1;
        }
        else {
            rtnValue = 0;
        }

        return rtnValue;

    }

    msg_spectra_cfg * msg_spectra_cfg_construct(void) {

        msg_spectra_cfg * cfg;

        cfg = (msg_spectra_cfg *) malloc(sizeof(msg_spectra_cfg));

        cfg->halfFrameSize = 0;
        cfg->nChannels = 0;
        cfg->fS = 0;

        return cfg;

    }

    void msg_spectra_cfg_destroy(msg_spectra_cfg * msg_spectra_config) {

        free((void *) msg_spectra_config);

    }

    void msg_spectra_cfg_printf(const msg_spectra_cfg * msg_spectra_config) {

        if (msg_spectra_config != NULL) {

            printf("halfFrameSize = %u\n", msg_spectra_config->halfFrameSize);
            printf("nChannels = %u\n", msg_spectra_config->nChannels);
            printf("fS = %u\n", msg_spectra_config->fS);

        }
        else {

            printf("(null)\n");

        }        

    }

    void msg_spectra_printf(const msg_spectra_obj * obj, const char * label) {
    if (!obj) {
        printf("[SPECTRA][%s] obj is NULL\n", label);
        return;
    }

    printf("[SPECTRA][%s] fS: %u\n", label, obj->fS);

    if (!obj->freqs) {
        printf("  freqs: NULL pointer\n");
        fflush(stdout);
        return;
    }

    float * f = (float *) obj->freqs;
    int non_zero_bins = 0;
    float max_val = 0.0f;
    int loud_bins = 0;
    const int N_FREQ_BINS = 256; // Adjust based on your actual size

    for (int i = 0; i < N_FREQ_BINS; i++) {
        float val = fabsf(f[i]);
        if (val > 1e-5) non_zero_bins++;
        if (val > max_val) max_val = val;
        if (10 * log10f(val + 1e-10) > -60.0f) loud_bins++;
    }
    printf("  TimeStamp: %lu", obj->timeStamp," ");
    printf("  Non-zero bins (>|1e-5|): %d / %d", non_zero_bins, N_FREQ_BINS, "  ");
    printf("  ~%d bins above -60 dB", loud_bins, " ");
    printf("  Max bin magnitude: %.6f\n", max_val);

    fflush(stdout);
}


    
