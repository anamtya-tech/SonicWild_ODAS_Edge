#ifndef __ODAS_CONNECTOR_CHATAK_ID
#define __ODAS_CONNECTOR_CHATAK_ID


    #include "../message/msg_chatak_id.h"

    #include <stdlib.h>
    #include <stdio.h>

    typedef struct con_chatak_id_obj {

        msg_chatak_id_obj * in;
        msg_chatak_id_obj ** outs;

        unsigned int nOuts;

    } con_chatak_id_obj;

    con_chatak_id_obj * con_chatak_id_construct(const unsigned int nOuts, const msg_chatak_id_cfg * msg_chatak_id_config);

    void con_chatak_id_destroy(con_chatak_id_obj * obj);

    int con_chatak_id_process(con_chatak_id_obj * obj);

#endif

