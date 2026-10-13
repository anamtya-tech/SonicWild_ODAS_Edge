
    
    #include <connector/con_chatak_id.h>

    con_chatak_id_obj * con_chatak_id_construct(const unsigned int nOuts, const msg_chatak_id_cfg * msg_chatak_id_config) {

        con_chatak_id_obj * obj;
        unsigned int iOut;

        obj = (con_chatak_id_obj *) malloc(sizeof(con_chatak_id_obj));

        obj->nOuts = nOuts;

        obj->in = msg_chatak_id_construct(msg_chatak_id_config);

        obj->outs = (msg_chatak_id_obj **) malloc(sizeof(msg_chatak_id_obj *) * nOuts);
        for (iOut = 0; iOut < obj->nOuts; iOut++) {
            obj->outs[iOut] = msg_chatak_id_construct(msg_chatak_id_config);
        }

        return obj;

    }

    void con_chatak_id_destroy(con_chatak_id_obj * obj) {

        unsigned int iOut;

        for (iOut = 0; iOut < obj->nOuts; iOut++) {
            msg_chatak_id_destroy(obj->outs[iOut]);
        }
        free((void *) obj->outs);

        msg_chatak_id_destroy(obj->in);

        free((void *) obj);

    }

    int con_chatak_id_process(con_chatak_id_obj * obj) {

        unsigned int iOut;
        int rtnValue;

        for (iOut = 0; iOut < obj->nOuts; iOut++) {
            msg_chatak_id_copy(obj->outs[iOut], obj->in);
        }

        if (msg_chatak_id_isZero(obj->in) == 1) {
            rtnValue = -1;
        }
        else {
            rtnValue = 0;
        }

        return rtnValue;

    }

