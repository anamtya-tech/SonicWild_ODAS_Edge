#ifndef PYTHON_ZODAS_H
#define PYTHON_ZODAS_H

#ifdef __cplusplus
extern "C" {
#endif

// Call sync_zodas.py with config parameters
void call_sync_zodas(int fS,
					 int hopSize,
					 int nBits,
					 int nChannels,
					 const char *audioRecordPath,
					 const char *sessionPrefix,
					 const char *configFilePath,
					 pid_t parentPID);

#ifdef __cplusplus
}
#endif

#endif // PYTHON_ZODAS_H
