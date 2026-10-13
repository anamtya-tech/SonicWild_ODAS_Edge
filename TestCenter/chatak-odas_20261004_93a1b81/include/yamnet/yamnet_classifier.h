/**
 * @file yamnet_classifier.h
 * @brief YAMNet audio classification for ZODAS integration
 */

#ifndef YAMNET_CLASSIFIER_H
#define YAMNET_CLASSIFIER_H

#include <string>
#include <vector>
#include <memory>

class YAMNetClassifier {
public:
    YAMNetClassifier();
    ~YAMNetClassifier();

    // Initialization
    bool LoadModel(const char* tflite_model_path);
    bool LoadClassNames(const char* csv_path);

    // Frame-by-frame API
    bool AddFrame(const float* magnitude_spectrum_257bins,
                  int& class_id_out,
                  std::string& class_name_out,
                  float& confidence_out);

    void Reset();

    // Utilities
    std::string GetClassName(int class_id) const;
    int GetNumClasses() const;
    bool IsReady() const;
    void PrintModelInfo() const;

    // 🔹 Patch-based API
    bool ClassifyPatch(const float* patch_96x257,
                       int& class_id_out,
                       std::string& class_name_out,
                       float& confidence_out);

    // 🔹 Top-K Patch-based API
    bool ClassifyPatchTopK(const float* patch_96x257,
                           int* class_ids_out,      // Pre-allocated array of size TOP_K
                           float* confidences_out,  // Pre-allocated array of size TOP_K
                           int k = 5);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // Disable copy
    YAMNetClassifier(const YAMNetClassifier&) = delete;
    YAMNetClassifier& operator=(const YAMNetClassifier&) = delete;
};

// ============================================================================
// Constants
// ============================================================================
namespace YAMNet {
    namespace Params {
        constexpr int SAMPLE_RATE = 16000;
        constexpr int FRAME_LENGTH = 400;
        constexpr int FRAME_STEP = 160;
        constexpr int FFT_SIZE = 512;
        constexpr int SPECTRUM_BINS = 257;
        constexpr int MEL_BINS = 64;
        constexpr int PATCH_FRAMES = 96;
        constexpr int PATCH_HOP = 48;
        constexpr int NUM_CLASSES = 521;
        constexpr int TOP_K = 5;  // Number of top predictions to return
        constexpr float MEL_MIN_HZ = 125.0f;
        constexpr float MEL_MAX_HZ = 7500.0f;
        constexpr float LOG_OFFSET = 0.001f;
    }

    namespace ClassID {
        constexpr int SPEECH = 0;
        constexpr int MUSIC = 137;
        constexpr int DOG = 74;
        // … add others as needed …
    }
}

#endif // YAMNET_CLASSIFIER_H
