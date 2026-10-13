
    
    #include <aconnector/acon_chatak_id.h>

    acon_chatak_id_obj * acon_chatak_id_construct(const unsigned int nOuts, const unsigned int nMessages, const msg_chatak_id_cfg * msg_chatak_id_config) {

        acon_chatak_id_obj * obj;
        unsigned int iOut;

        obj = (acon_chatak_id_obj *) malloc(sizeof(acon_chatak_id_obj));

        obj->in = amsg_chatak_id_construct(nMessages, msg_chatak_id_config);

        obj->outs = (amsg_chatak_id_obj **) malloc(sizeof(amsg_chatak_id_obj *) * nOuts);
        for (iOut = 0; iOut < nOuts; iOut++) {
            obj->outs[iOut] = amsg_chatak_id_construct(nMessages, msg_chatak_id_config);
        }

        obj->con_chatak_id = con_chatak_id_construct(nOuts, msg_chatak_id_config);

        obj->thread = thread_construct(&acon_chatak_id_thread, (void *) obj);

        return obj;

    }

    void acon_chatak_id_destroy(acon_chatak_id_obj * obj) {

        unsigned int iOut;
        unsigned int nOuts;

        nOuts = obj->con_chatak_id->nOuts;

        thread_destroy(obj->thread);

        con_chatak_id_destroy(obj->con_chatak_id);

        for (iOut = 0; iOut < nOuts; iOut++) {
            amsg_chatak_id_destroy(obj->outs[iOut]);
        }
        free((void *) obj->outs);

        amsg_chatak_id_destroy(obj->in);

        free((void *) obj);


    }

    void * acon_chatak_id_thread(void * ptr) {

        msg_chatak_id_obj * msg_chatak_id_in;
        msg_chatak_id_obj * msg_chatak_id_out;

        acon_chatak_id_obj * obj;
        unsigned int iOut;
        unsigned int nOuts;
        int debug = 1;
        
        int rtnValue;

        obj = (acon_chatak_id_obj *) ptr;

        nOuts = obj->con_chatak_id->nOuts;

        while(1) {

            msg_chatak_id_in = amsg_chatak_id_filled_pop(obj->in);
            msg_chatak_id_copy(obj->con_chatak_id->in, msg_chatak_id_in);
            amsg_chatak_id_empty_push(obj->in, msg_chatak_id_in);

            rtnValue = con_chatak_id_process(obj->con_chatak_id);

            for (iOut = 0; iOut < nOuts; iOut++) {
                
                msg_chatak_id_out = amsg_chatak_id_empty_pop(obj->outs[iOut]);
                msg_chatak_id_copy(msg_chatak_id_out, obj->con_chatak_id->outs[iOut]);
                amsg_chatak_id_filled_push(obj->outs[iOut], msg_chatak_id_out);
            }
            
           
            if (rtnValue == -1) {
                break;
            }

        }

    }
