#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>  // for pid_t

void call_sync_zodas(int fS,
                     int hopSize,
                     int nBits,
                     int nChannels,
                     const char *audioRecordPath,
                     const char *sessionPrefix,
                     const char *configFilePath,
                     pid_t parentPID) {
    char cmd[1024];
    const char *recorder = getenv("ODAS_TIMING_DIR") != NULL
                           ? ODAS_TIMING_RECORDER_PATH
                           : "/home/chatak/chatak-odas/src/python/sync_zodas.py";
    snprintf(cmd, sizeof(cmd),
        "python3 \"%s\" %d %d %d %d \"%s\" \"%s\" \"%s\" %d &",
        recorder, fS, hopSize, nBits, nChannels, audioRecordPath, sessionPrefix, configFilePath, parentPID);

    system(cmd);
}
