#ifndef __ODAS_ACONNECTOR_CHATAK_ID
#define __ODAS_ACONNECTOR_CHATAK_ID

    #include "../connector/con_chatak_id.h"
    #include "../amessage/amsg_chatak_id.h"
    #include "../message/msg_chatak_id.h"
    #include "../general/thread.h"

    #include <stdlib.h>
    #include <stdio.h>

    typedef struct acon_chatak_id_obj {

        amsg_chatak_id_obj * in;
        amsg_chatak_id_obj ** outs;
        con_chatak_id_obj * con_chatak_id;
        thread_obj * thread;

    } acon_chatak_id_obj;

    acon_chatak_id_obj * acon_chatak_id_construct(const unsigned int nOuts, const unsigned int nMessages, const msg_chatak_id_cfg * msg_chatak_id_config);

    void acon_chatak_id_destroy(acon_chatak_id_obj * obj);

    void * acon_chatak_id_thread(void * ptr);

#endif
