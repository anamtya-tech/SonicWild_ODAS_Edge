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
    snprintf(cmd, sizeof(cmd),
        "python3 /home/chatak/chatak-odas/src/python/sync_zodas.py %d %d %d %d \"%s\" \"%s\" \"%s\" %d &",
        fS, hopSize, nBits, nChannels, audioRecordPath, sessionPrefix, configFilePath, parentPID);

    system(cmd);
}
