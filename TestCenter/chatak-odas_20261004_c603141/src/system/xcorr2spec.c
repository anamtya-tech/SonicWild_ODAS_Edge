#include <math.h>
#include <stdio.h>
#include <float.h>
#include <complex.h>


int debug = 2;

static inline int is_finite2(float x) { return isfinite(x); }

void xcorr2true_spectrum_at_peak(
    unsigned int fS,
    float **channel_spectra,   // [nChannels][2*(frame_size/2 + 1)] interleaved complex re,im
    int *tdoa_array,           // [nPoints * nPairs] signed delays in samples (base grid)
    unsigned int n_pairs,
    unsigned int n_channels,
    unsigned int frame_size,   // Nfft (even)
    unsigned int peak_index,
    float *spec_output,        // [frame_size/2 + 1] magnitude spectrum (positive freqs)
    float x, float y, float z
) {
    unsigned int half = frame_size / 2; // Nyquist index

    // Zero positive-frequency output
    for (unsigned int k = 0; k <= half; k++) {
        spec_output[k] = 0.0f;
    }

    // --- Step 1: reconstruct per-channel delays tau_channels[c] from per-pair TDOAs ---
    float tau_channels[n_channels];
    for (unsigned int c = 0; c < n_channels; c++) tau_channels[c] = 0.0f;

    unsigned int count = 0;
    for (unsigned int i = 0; i < n_channels - 1; i++) {
        for (unsigned int j = i + 1; j < n_channels; j++, count++) {
            int tdoa = tdoa_array[peak_index * n_pairs + count];
            if (i == 0) {
                // pair (0,j): tau_j = TDOA(0,j)
                tau_channels[j] = (float)tdoa;
            } else if (j == 0) {
                // pair (i,0): tau_i = -TDOA(i,0)
                tau_channels[i] = (float)(-tdoa);
            }
        }
    }
    tau_channels[0] = 0.0f; // reference mic

    // --- Step 2: beamform across all channels ---
    for (unsigned int k = 0; k <= half; k++) {
        float complex sumAligned = 0.0f + I*0.0f;
        float magSum = 0.0f;

        for (unsigned int c = 0; c < n_channels; c++) {
            float tau = tau_channels[c];
            float angle = -2.0f * M_PI * (float)k * tau / (float)frame_size;
            float ca = cosf(angle), sa = sinf(angle);

            float re = channel_spectra[c][2*k];
            float im = channel_spectra[c][2*k + 1];
            if (!is_finite2(re) || !is_finite2(im)) { re = 0.0f; im = 0.0f; }

            float complex X   = re + I*im;
            float complex H   = ca + I*sa;      // e^{-j angle}
            float complex Xal = X * H;          // aligned channel

            sumAligned += Xal;
            magSum     += cabsf(X);
        }

        float beamMag   = cabsf(sumAligned);
        float coherence = (magSum > 1e-9f) ? (beamMag / magSum) : 0.0f;

        // Use coherence mask to suppress leakage
        spec_output[k] = beamMag * coherence;
    }

    if (debug == 2) {
        // Find top two bins
        unsigned int top1 = 0, top2 = 0;
        float val1 = -FLT_MAX, val2 = -FLT_MAX;
        for (unsigned int k = 0; k <= half; k++) {
            float v = spec_output[k];
            if (v > val1) { val2 = val1; top2 = top1; val1 = v; top1 = k; }
            else if (v > val2) { val2 = v; top2 = k; }
        }
        float hz_per_bin = (float)fS / (float)frame_size;
        float f1 = (float)top1 * hz_per_bin;
        float f2 = (float)top2 * hz_per_bin;

        /*
        printf("{\"pot\":%u,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
               "\"top_bin1\":%u,\"freq1\":%.2f,\"val1\":%.6f,"
               "\"top_bin2\":%u,\"freq2\":%.2f,\"val2\":%.6f}\n",
               peak_index, x, y, z,
               top1, f1, val1,
               top2, f2, val2);
        */
    }
}
