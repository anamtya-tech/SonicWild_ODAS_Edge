
    
    #include <amessage/amsg_chatak_id.h>

    amsg_chatak_id_obj * amsg_chatak_id_construct(const unsigned int nMessages, const msg_chatak_id_cfg * msg_chatak_id_config) {

        amsg_chatak_id_obj * obj;
        unsigned int iMessage;

        obj = (amsg_chatak_id_obj *) malloc(sizeof(amsg_chatak_id_obj));

        obj->nMessages = nMessages;
        obj->filled = fifo_construct_zero(nMessages);
        obj->empty = fifo_construct_zero(nMessages);

        for (iMessage = 0; iMessage < nMessages; iMessage++) {

            fifo_push(obj->empty, (void *) msg_chatak_id_construct(msg_chatak_id_config));

        }

        return obj;

    }

    void amsg_chatak_id_destroy(amsg_chatak_id_obj * obj) {

        while(fifo_nElements(obj->filled) > 0) {
            msg_chatak_id_destroy((void *) fifo_pop(obj->filled));
        }

        while(fifo_nElements(obj->empty) > 0) {
            msg_chatak_id_destroy((void *) fifo_pop(obj->empty));
        }

        fifo_destroy(obj->filled);
        fifo_destroy(obj->empty);

        free((void *) obj);

    }

    msg_chatak_id_obj * amsg_chatak_id_filled_pop(amsg_chatak_id_obj * obj) {

        return ((msg_chatak_id_obj *) fifo_pop(obj->filled));

    }

    void amsg_chatak_id_filled_push(amsg_chatak_id_obj * obj, msg_chatak_id_obj * msg_chatak_id) {

        fifo_push(obj->filled, (void *) msg_chatak_id);

    }

    msg_chatak_id_obj * amsg_chatak_id_empty_pop(amsg_chatak_id_obj * obj) {

        return ((msg_chatak_id_obj *) fifo_pop(obj->empty));

    }

    void amsg_chatak_id_empty_push(amsg_chatak_id_obj * obj, msg_chatak_id_obj * msg_chatak_id) {

        fifo_push(obj->empty, (void *) msg_chatak_id);

    }
