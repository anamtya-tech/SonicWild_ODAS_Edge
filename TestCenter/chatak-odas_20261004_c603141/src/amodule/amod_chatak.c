

    #include <amodule/amod_chatak.h>
    

    amod_chatak_obj * amod_chatak_construct(const mod_chatak_cfg * mod_chatak_config) {

        amod_chatak_obj * obj;

        obj = (amod_chatak_obj *) malloc(sizeof(amod_chatak_obj));

        obj->mod_chatak = mod_chatak_construct(mod_chatak_config);
        
        obj->in = (amsg_spectra_obj *) NULL;
        obj->out = (amsg_chatak_id_obj *) NULL;

        obj->thread = thread_construct(&amod_chatak_thread, (void *) obj);

        mod_chatak_disable(obj->mod_chatak);

        return obj;

    }

    void amod_chatak_destroy(amod_chatak_obj * obj) {

        mod_chatak_destroy(obj->mod_chatak);
        thread_destroy(obj->thread);

        free((void *) obj);           

    }

    void amod_chatak_connect(amod_chatak_obj * obj, amsg_spectra_obj * in, amsg_chatak_id_obj * out) {

        obj->in = in;
        obj->out = out;

    }

    void amod_chatak_disconnect(amod_chatak_obj * obj) {

        obj->in = (amsg_spectra_obj *) NULL;
        obj->out = (amsg_chatak_id_obj *) NULL;

    }

    void amod_chatak_enable(amod_chatak_obj * obj) {

        mod_chatak_enable(obj->mod_chatak);

    }

    void amod_chatak_disable(amod_chatak_obj * obj) {

        mod_chatak_disable(obj->mod_chatak);

    }

    void * amod_chatak_thread(void * ptr) {

        amod_chatak_obj * obj;
        msg_spectra_obj * msg_spectra_in;
        msg_chatak_id_obj * msg_chatak_id_out;
        int rtnValue;
        int debug = 0;
        obj = (amod_chatak_obj *) ptr;

        while(1) {

            // Pop a message, process, and push back
            msg_spectra_in = amsg_spectra_filled_pop(obj->in);
            if (debug) {
            //printf("[CHATAK_THREAD] Popped Filled SPECTRA message chatak:\n");
            //msg_spectra_printf(msg_spectra_in, "POP");
            printf("[CHATAK_THREAD] Obtained EMPTY chatak ID msg:\n");
            msg_chatak_id_printf(msg_chatak_id_out, "PRE-PROCESS");
                }
                
            msg_chatak_id_out = amsg_chatak_id_empty_pop(obj->out);
            mod_chatak_connect(obj->mod_chatak, msg_spectra_in, msg_chatak_id_out);
            rtnValue = mod_chatak_process(obj->mod_chatak);
            mod_chatak_disconnect(obj->mod_chatak);
            
            if (debug) {
            printf("[CHATAK_THREAD] Copied data:\n");
            msg_chatak_id_printf(msg_chatak_id_out, "POST-PROCESS");
                }
            
            amsg_spectra_empty_push(obj->in, msg_spectra_in);
            amsg_chatak_id_filled_push(obj->out, msg_chatak_id_out);

            // If this is the last frame, rtnValue = -1
            if (rtnValue == -1) {
                break;
            }

        }

    }
