#include <module/mod_sst.h>

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void) {
    const char *names[ROLLING_HOPS] = {
        "Elephant", "Elephant", "Frog", "Elephant", "Elephant", "Elephant"
    };
    float scores[ROLLING_HOPS] = {0.91f, 0.88f, 0.96f, 0.83f, 0.85f, 0.89f};
    sst_target_threshold_t thresholds[2] = {
        {.class_name = "Elephant", .min_confidence = 0.80f},
        {.class_name = "Frog", .min_confidence = 0.90f}
    };
    sst_alarm_result_t result;

    CHECK(sst_evaluate_target_alarm(names, scores, ROLLING_HOPS,
                                    thresholds, 2, 5, &result) == 1);
    CHECK(result.triggered == 1);
    CHECK(strcmp(result.class_name, "Elephant") == 0);
    CHECK(result.votes == 5);
    CHECK(result.avg_confidence > 0.87f && result.avg_confidence < 0.88f);

    scores[1] = 0.79f;
    CHECK(sst_evaluate_target_alarm(names, scores, ROLLING_HOPS,
                                    thresholds, 2, 5, &result) == 0);
    CHECK(result.triggered == 0);

    scores[1] = 0.88f;
    CHECK(sst_evaluate_target_alarm(names, scores, ROLLING_HOPS,
                                    thresholds, 2, 6, &result) == 0);

    names[2] = "Elephant";
    CHECK(sst_evaluate_target_alarm(names, scores, ROLLING_HOPS,
                                    thresholds, 2, 6, &result) == 1);
    CHECK(result.votes == 6);

    CHECK(sst_evaluate_target_alarm(names, scores, ROLLING_HOPS,
                                    thresholds, 2, 0, &result) == -1);
    puts("SST target alarm tests passed");
    return EXIT_SUCCESS;
}
