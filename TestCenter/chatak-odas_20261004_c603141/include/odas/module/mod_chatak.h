#ifndef __ODAS_MODULE_CHATAK
#define __ODAS_MODULE_CHATAK

    #include "../message/msg_chatak_id.h"
    #include "../message/msg_spectra.h"

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>
    #include <stdint.h>
    #include <errno.h>
    #include <math.h>
    #include <pthread.h>
    #include <signal.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>

    #define SERVER_IP "127.0.0.1"
    #define SERVER_PORT 9090
    #define PACKET_MAGIC 0xFACEFEED



    typedef struct {
        unsigned long long timeStamp;
        unsigned int fS;
        unsigned int nSignals;
        unsigned int nBins;
        float **array;  // shape [nSignals][nBins]
    } spectra_copy;


    typedef struct mod_chatak_obj {

        msg_spectra_obj * in;
        msg_chatak_id_obj * out;        

        char enabled;

    } mod_chatak_obj;

    typedef struct mod_chatak_cfg {
        
    } mod_chatak_cfg;

    mod_chatak_obj * mod_chatak_construct(const mod_chatak_cfg * mod_chatak_config);

    void mod_chatak_destroy(mod_chatak_obj * obj);

    int mod_chatak_process(mod_chatak_obj * obj);

    void mod_chatak_connect(mod_chatak_obj * obj, msg_spectra_obj * in, msg_chatak_id_obj * out);

    void mod_chatak_disconnect(mod_chatak_obj * obj);

    void mod_chatak_enable(mod_chatak_obj * obj);

    void mod_chatak_disable(mod_chatak_obj * obj);

    mod_chatak_cfg * mod_chatak_cfg_construct(void);

    void mod_chatak_cfg_destroy(mod_chatak_cfg * cfg);

    void mod_chatak_cfg_printf(const mod_chatak_cfg * cfg);
    
    int send_spectra_to_python(const msg_spectra_obj *in);
    
    void *send_json_worker(void *arg);
    
    void *send_binary_worker(void *arg);

#endif
