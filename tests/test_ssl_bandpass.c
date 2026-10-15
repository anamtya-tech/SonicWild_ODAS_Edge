#include <module/mod_ssl.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void write_band(const char * path, const char * contents, long nanoseconds) {
    struct timespec times[2] = {{1700000000, 0}, {1700000000, nanoseconds}};
    FILE * file = fopen(path, "w");
    CHECK(file != NULL);
    CHECK(fputs(contents, file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(utimensat(AT_FDCWD, path, times, 0) == 0);
}

static void check_frame(mod_ssl_obj * obj, unsigned int low, unsigned int high) {
    unsigned int k;
    unsigned int channel;
    CHECK(mod_ssl_process(obj) == 0);
    CHECK(obj->freq2freq_bandpass->lowCut == low);
    CHECK(obj->freq2freq_bandpass->highCut == high);
    CHECK(obj->out->timeStamp == obj->in->timeStamp);
    for (k = 0; k < obj->halfFrameSize; k++) {
        float real = obj->products->array[0][2 * k];
        float imag = obj->products->array[0][2 * k + 1];
        if ((k >= low) && (k <= high)) {
            CHECK(real > 0.99f);
        } else {
            CHECK(real == 0.0f);
        }
        CHECK(imag == 0.0f);
        CHECK(obj->productsInterp->array[0][2 * k] == real);
        for (channel = 0; channel < 2; channel++) {
            CHECK(obj->in->freqs->array[channel][2 * k] == 1.0f);
            CHECK(obj->in->freqs->array[channel][2 * k + 1] == 0.0f);
        }
    }
}

int main(void) {
    char directory[] = "/tmp/odas-ssl-bandpass-XXXXXX";
    char replacement[256];
    mod_ssl_obj obj = {0};
    scans_obj scans = {0};
    msg_spectra_cfg spectra_cfg = {.fS = 16000, .halfFrameSize = 257, .nChannels = 2};
    msg_pots_cfg pots_cfg = {.fS = 16000, .nPots = 1};
    const char * invalid[] = {
        "lowHz=1000\n",
        "lowHz=2000\nhighHz=1000\n",
        "lowHz=1000\nhighHz=1000\n",
        "lowHz=-1\nhighHz=2000\n",
        "lowHz=1000\nhighHz=8001\n",
        "lowHz=1000.5\nhighHz=2000\n",
        "lowHz=oops\nhighHz=2000\n",
        "lowHz=1000\nhighHz=2000\nextra=4\n",
        "lowHz=1000\nlowHz=1500\nhighHz=2000\n",
        "lowHz=184467440737095516160\nhighHz=2000\n",
        "lowHz=1001\nhighHz=1002\n",
        ""
    };
    unsigned int i;

    CHECK(mkdtemp(directory) != NULL);
    snprintf(obj.bandpassPath, sizeof(obj.bandpassPath), "%s/bandpass.cfg", directory);
    snprintf(replacement, sizeof(replacement), "%s/bandpass.tmp", directory);
    obj.fS = 16000;
    obj.frameSize = 512;
    obj.halfFrameSize = 257;
    obj.freq2freq_bandpass = freq2freq_bandpass_construct_zero(257, 0, 256);

    /* Load an existing file without requiring a post-startup modification. */
    write_band(obj.bandpassPath, "lowHz=1000\nhighHz=2000\n", 1);
    CHECK(mod_ssl_reload_bandpass(&obj) == 0);
    CHECK(obj.freq2freq_bandpass->lowCut == 32);
    CHECK(obj.freq2freq_bandpass->highCut == 64);

    /* Exercise the actual SSL frame path without spatial peak extraction. */
    scans.pairs = pairs_construct_zero(1);
    scans.pairs->array[0] = 1;
    obj.scans = &scans;
    obj.enabled = 1;
    obj.phasors = freqs_construct_zero(2, 257);
    obj.products = freqs_construct_zero(1, 257);
    obj.productsInterp = freqs_construct_zero(1, 257);
    obj.freq2freq_phasor = freq2freq_phasor_construct_zero(257, 1e-20f);
    obj.freq2freq_product = freq2freq_product_construct_zero(257);
    obj.freq2freq_interpolate = freq2freq_interpolate_construct_zero(257, 257);
    obj.freq2xcorr = freq2xcorr_construct_zero(512, 257);
    obj.xcorrs = xcorrs_construct_zero(1, 512);
    obj.pots = pots_construct_zero(1);
    obj.in = msg_spectra_construct(&spectra_cfg);
    obj.out = msg_pots_construct(&pots_cfg);
    obj.in->timeStamp = 1;
    for (i = 0; i < 257; i++) {
        obj.in->freqs->array[0][2 * i] = 1.0f;
        obj.in->freqs->array[1][2 * i] = 1.0f;
    }
    check_frame(&obj, 32, 64);

    /* Same inode, size and second: nanosecond changes must reload. */
    write_band(obj.bandpassPath, "lowHz=2000\nhighHz=3000\n", 2);
    check_frame(&obj, 64, 96);
    write_band(obj.bandpassPath, "lowHz=1000\nhighHz=2000\n", 1);
    check_frame(&obj, 32, 64);

    /* Atomic replacement, even with identical timestamps and size. */
    write_band(replacement, "lowHz=3000\nhighHz=4000\n", 1);
    CHECK(rename(replacement, obj.bandpassPath) == 0);
    check_frame(&obj, 96, 128);

    write_band(obj.bandpassPath, "lowHz=1001\nhighHz=1999\n", 3);
    check_frame(&obj, 33, 63);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        write_band(obj.bandpassPath, invalid[i], 10 + i);
        CHECK(mod_ssl_reload_bandpass(&obj) == -1);
        check_frame(&obj, 33, 63);
    }

    CHECK(unlink(obj.bandpassPath) == 0);
    CHECK(mod_ssl_reload_bandpass(&obj) == -1);
    check_frame(&obj, 33, 63);
    write_band(obj.bandpassPath, "  highHz = 8000 \n\n lowHz = 0 \n", 50);
    check_frame(&obj, 32, 256);

    write_band(obj.bandpassPath, "lowHz=0\nhighHz=2000\n", 51);
    check_frame(&obj, 32, 64);
    write_band(obj.bandpassPath, "lowHz=999\nhighHz=2000\n", 52);
    check_frame(&obj, 32, 64);

    /* An unusable band clears previous localization results, not just bins. */
    obj.nPots = 1;
    obj.pots->array[3] = obj.out->pots->array[3] = 5.0f;
    obj.pots->spec_at_peak[0][40] = obj.out->pots->spec_at_peak[0][40] = 5.0f;
    write_band(obj.bandpassPath, "lowHz=0\nhighHz=999\n", 53);
    check_frame(&obj, 32, 31);
    CHECK(obj.out->pots->array[3] == 0.0f);
    CHECK(obj.out->pots->spec_at_peak[0][40] == 0.0f);
    CHECK(obj.out->pots->nPots == 1);
    write_band(obj.bandpassPath, "lowHz=0\nhighHz=1000\n", 54);
    check_frame(&obj, 33, 32);
    obj.nPots = 0;
    write_band(obj.bandpassPath, "lowHz=0\nhighHz=2000\n", 55);
    check_frame(&obj, 32, 64);

    obj.fS = 48000;
    write_band(obj.bandpassPath, "lowHz=0\nhighHz=2000\n", 56);
    check_frame(&obj, 11, 21);

    msg_spectra_destroy(obj.in);
    msg_pots_destroy(obj.out);
    pots_destroy(obj.pots);
    xcorrs_destroy(obj.xcorrs);
    freq2xcorr_destroy(obj.freq2xcorr);
    freq2freq_interpolate_destroy(obj.freq2freq_interpolate);
    freq2freq_product_destroy(obj.freq2freq_product);
    freq2freq_phasor_destroy(obj.freq2freq_phasor);
    freq2freq_bandpass_destroy(obj.freq2freq_bandpass);
    freqs_destroy(obj.productsInterp);
    freqs_destroy(obj.products);
    freqs_destroy(obj.phasors);
    pairs_destroy(scans.pairs);
    CHECK(unlink(obj.bandpassPath) == 0);
    CHECK(rmdir(directory) == 0);
    puts("SSL dynamic bandpass tests passed");
    return EXIT_SUCCESS;
}
