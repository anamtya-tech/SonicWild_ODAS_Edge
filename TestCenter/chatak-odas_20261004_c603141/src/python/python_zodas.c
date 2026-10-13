#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>  // for pid_t

void call_sync_zodas(int fS, int hopSize, int nBits, int nChannels, const char *audioRecordPath, pid_t parentPID) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "python3 /home/chatak/z_odas/src/python/sync_zodas.py %d %d %d %d \"%s\" %d &",
        fS, hopSize, nBits, nChannels, audioRecordPath, parentPID);

    system(cmd);
}
