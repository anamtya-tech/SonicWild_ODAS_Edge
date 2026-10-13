#ifndef YAMNET_C_API_H
#define YAMNET_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Opaque handle type (for future multi-instance support)
// ---------------------------------------------------------------------------
typedef struct yamnet_handle_t yamnet_handle_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Construct a YAMNet classifier.
 * @param tflite_model_path Path to the .tflite model file
 * @param class_map_csv_path Path to the class map CSV file
 * @return Pointer to a new classifier handle, or NULL on failure
 */
yamnet_handle_t* yamnet_create(const char* tflite_model_path,
                               const char* class_map_csv_path);

/**
 * Destroy a YAMNet classifier and free resources.
 */
void yamnet_destroy(yamnet_handle_t* handle);

// ---------------------------------------------------------------------------
// Frame-by-frame API
// ---------------------------------------------------------------------------

/**
 * Add one 257-bin spectrum frame and return classification when ready.
 * @param handle Classifier handle
 * @param spectrum Pointer to 257 floats (magnitude spectrum)
 * @param out_class_id Output: predicted class ID
 * @param out_class_name Output: pointer to class name string
 * @param out_confidence Output: confidence score
 * @return 1 if classification is ready, 0 if still buffering, negative on error
 */
int yamnet_add_frame(yamnet_handle_t* handle,
                     const float* spectrum,
                     int* out_class_id,
                     const char** out_class_name,
                     float* out_confidence);

// ---------------------------------------------------------------------------
// Patch-based API
// ---------------------------------------------------------------------------

/**
 * Classify a full patch (96 frames × 257 bins) immediately.
 * @param handle Classifier handle
 * @param patch Pointer to contiguous array of 96*257 floats
 * @param out_class_id Output: predicted class ID
 * @param out_class_name Output: pointer to class name string
 * @param out_confidence Output: confidence score
 * @return 1 on success, 0 on failure
 */
int yamnet_classify_patch(yamnet_handle_t* handle,
                          const float* patch,
                          int* out_class_id,
                          const char** out_class_name,
                          float* out_confidence);

/**
 * Classify a full patch and return Top-K predictions.
 * @param handle Classifier handle
 * @param patch Pointer to contiguous array of 96*257 floats
 * @param out_class_ids Pre-allocated array of size k for class IDs
 * @param out_confidences Pre-allocated array of size k for confidence scores
 * @param k Number of top predictions to return (typically 5)
 * @return 1 on success, 0 on failure
 */
int yamnet_classify_patch_topk(yamnet_handle_t* handle,
                               const float* patch,
                               int* out_class_ids,
                               float* out_confidences,
                               int k);

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

/**
 * Reset classifier state (clear buffered frames).
 */
void yamnet_reset(yamnet_handle_t* handle);

/**
 * Get number of classes (should be 521).
 */
int yamnet_num_classes(yamnet_handle_t* handle);

/**
 * Get class name string from class ID.
 */
const char* yamnet_class_name_from_id(yamnet_handle_t* handle, int class_id);

#ifdef __cplusplus
}
#endif

#endif // YAMNET_C_API_H
