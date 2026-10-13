
#ifndef __ODAS_AMODULE_CHATAK
#define __ODAS_AMODULE_CHATAK


    #include "../module/mod_chatak.h"
    #include "../amessage/amsg_spectra.h"
    #include "../amessage/amsg_chatak_id.h"
    #include "../general/thread.h"

    typedef struct amod_chatak_obj {

        mod_chatak_obj * mod_chatak;
        amsg_spectra_obj * in;
        amsg_chatak_id_obj * out;
        thread_obj * thread;    

    } amod_chatak_obj;

    amod_chatak_obj * amod_chatak_construct(const mod_chatak_cfg * mod_chatak_config);

    void amod_chatak_destroy(amod_chatak_obj * obj);

    void amod_chatak_connect(amod_chatak_obj * obj, amsg_spectra_obj * in, amsg_chatak_id_obj * out);

    void amod_chatak_disconnect(amod_chatak_obj * obj);

    void amod_chatak_enable(amod_chatak_obj * obj);

    void amod_chatak_disable(amod_chatak_obj * obj);

    void * amod_chatak_thread(void * ptr);    

#endif












