#include "yamnet_classifier.h"
#include "yamnet_c_api.h"
#include <string>

// Define the opaque handle struct
struct yamnet_handle_t {
    YAMNetClassifier classifier;
    std::string last_class_name;
};

extern "C" {

// ---------------------------------------------------------------------------
// Construct a new classifier instance
// ---------------------------------------------------------------------------
yamnet_handle_t* yamnet_create(const char* model_path,
                               const char* labels_path) {
    yamnet_handle_t* h = new yamnet_handle_t();
    if (!h->classifier.LoadModel(model_path)) {
        delete h;
        return nullptr;
    }
    if (!h->classifier.LoadClassNames(labels_path)) {
        delete h;
        return nullptr;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Destroy classifier instance
// ---------------------------------------------------------------------------
void yamnet_destroy(yamnet_handle_t* handle) {
    delete handle;
}

// ---------------------------------------------------------------------------
// Add one frame (257 bins) and return classification when ready
// ---------------------------------------------------------------------------
int yamnet_add_frame(yamnet_handle_t* handle,
                     const float* spectrum,
                     int* class_id,
                     const char** class_name,
                     float* confidence) {
    std::string cname;
    bool ready = handle->classifier.AddFrame(spectrum, *class_id, cname, *confidence);
    if (ready) {
        handle->last_class_name = cname;
        *class_name = handle->last_class_name.c_str();
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Classify a full patch (96 frames × 257 bins) immediately
// ---------------------------------------------------------------------------
// yamnet_wrapper.cpp
int yamnet_classify_patch(yamnet_handle_t* handle,
                          const float* patch_96x257,
                          int* class_id,
                          const char** class_name,
                          float* confidence) {
    //fprintf(stderr, "[DEBUG] yamnet_classify_patch invoked\n");
    if (!handle || !patch_96x257 || !class_id || !class_name || !confidence) {
        return 0;
    }
    // Optional: if Impl exposes readiness
    if (!handle->classifier.IsReady()) {
        return 0;
    }

    // Extra: verify expected dims if you can
    // Assume classifier enforces 96*257 internally.

    std::string cname;
    bool ok = handle->classifier.ClassifyPatch(patch_96x257, *class_id, cname, *confidence);
    if (ok) {
        handle->last_class_name = cname;
        *class_name = handle->last_class_name.c_str();
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Classify a full patch and return Top-K predictions
// ---------------------------------------------------------------------------
int yamnet_classify_patch_topk(yamnet_handle_t* handle,
                               const float* patch_96x257,
                               int* class_ids,
                               float* confidences,
                               int k) {
    if (!handle || !patch_96x257 || !class_ids || !confidences || k <= 0) {
        return 0;
    }
    
    if (!handle->classifier.IsReady()) {
        return 0;
    }

    bool ok = handle->classifier.ClassifyPatchTopK(patch_96x257, class_ids, confidences, k);
    return ok ? 1 : 0;
}


// ---------------------------------------------------------------------------
// Reset classifier state (clear buffered frames)
// ---------------------------------------------------------------------------
void yamnet_reset(yamnet_handle_t* handle) {
    handle->classifier.Reset();
}

// ---------------------------------------------------------------------------
// Utility: get number of classes
// ---------------------------------------------------------------------------
int yamnet_num_classes(yamnet_handle_t* handle) {
    return handle->classifier.GetNumClasses();
}

// ---------------------------------------------------------------------------
// Utility: get class name by ID
// ---------------------------------------------------------------------------
const char* yamnet_class_name_from_id(yamnet_handle_t* handle, int class_id) {
    handle->last_class_name = handle->classifier.GetClassName(class_id);
    return handle->last_class_name.c_str();
}

} // extern "C"
