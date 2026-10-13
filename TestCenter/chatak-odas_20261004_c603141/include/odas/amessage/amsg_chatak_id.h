#ifndef __ODAS_AMESSAGE_CHATAK_ID
#define __ODAS_AMESSAGE_CHATAK_ID

    #include <stdlib.h>

    #include "../message/msg_chatak_id.h"
    #include "../utils/fifo.h"

    typedef struct amsg_chatak_id_obj {

        unsigned int nMessages;
        fifo_obj * filled;
        fifo_obj * empty;

    } amsg_chatak_id_obj;

    amsg_chatak_id_obj * amsg_chatak_id_construct(const unsigned int nMessages, const msg_chatak_id_cfg * msg_chatak_id_config);

    void amsg_chatak_id_destroy(amsg_chatak_id_obj * obj);

    msg_chatak_id_obj * amsg_chatak_id_filled_pop(amsg_chatak_id_obj * obj);

    void amsg_chatak_id_filled_push(amsg_chatak_id_obj * obj, msg_chatak_id_obj * msg_chatak_id);

    msg_chatak_id_obj * amsg_chatak_id_empty_pop(amsg_chatak_id_obj * obj);

    void amsg_chatak_id_empty_push(amsg_chatak_id_obj * obj, msg_chatak_id_obj * msg_chatak_id);

#endif
