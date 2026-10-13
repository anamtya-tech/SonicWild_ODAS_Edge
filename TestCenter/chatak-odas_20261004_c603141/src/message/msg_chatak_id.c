
    
    #include <message/msg_chatak_id.h>
    #include <stdio.h>

    msg_chatak_id_obj * msg_chatak_id_construct(const msg_chatak_id_cfg * msg_chatak_id_config) {

    msg_chatak_id_obj * obj;

    obj = (msg_chatak_id_obj *) malloc(sizeof(msg_chatak_id_obj));
    if (!obj) return NULL;

    obj->timeStamp = 0;
    obj->numSounds = 0;

    for (int i = 0; i < MAX_SOUND_ROWS; i++) {
        obj->sounds[i].idNum = 0;
        obj->sounds[i].confidence = 0.0f;
        obj->sounds[i].soundName[0] = '\0';
    }

    return obj;
}


    void msg_chatak_id_destroy(msg_chatak_id_obj * obj) {
        free((void *) obj);
    }
        
    void msg_chatak_id_copy(msg_chatak_id_obj * dest, const msg_chatak_id_obj * src) {

        for (int i = 0; i < MAX_SOUND_ROWS; i++) {
            dest->sounds[i].idNum = 0;
            dest->sounds[i].confidence = 0.0f;
            dest->sounds[i].soundName[0] = '\0';
        }

        dest->timeStamp = src->timeStamp;
        dest->numSounds = src->numSounds;

        for (int i = 0; i < src->numSounds; i++) {
            dest->sounds[i] = src->sounds[i];
            }
        }

    void msg_chatak_id_zero(msg_chatak_id_obj * obj) {
        obj->timeStamp = 0;
        obj->numSounds = 0;

        for (int i = 0; i < MAX_SOUND_ROWS; i++) {
            obj->sounds[i].idNum = 0;
            obj->sounds[i].confidence = 0.0f;
            obj->sounds[i].soundName[0] = '\0';
        }
    }

    unsigned int msg_chatak_id_isZero(const msg_chatak_id_obj * obj) {
        unsigned int rtnValue;

        if (obj->timeStamp == 0) {
            rtnValue = 1;
        }
        else {
            rtnValue = 0;
        }

        return rtnValue;
    }

    msg_chatak_id_cfg * msg_chatak_id_cfg_construct(void) {
        msg_chatak_id_cfg * cfg;
        cfg = (msg_chatak_id_cfg *) malloc(sizeof(msg_chatak_id_cfg));
        cfg->halfFrameSize = 0;
        cfg->nChannels = 0;
        cfg->fS = 0;
        return cfg;
    }

    void msg_chatak_id_cfg_destroy(msg_chatak_id_cfg * msg_chatak_id_config) {
        free((void *) msg_chatak_id_config);
    }

    void msg_chatak_id_cfg_printf(const msg_chatak_id_cfg * msg_chatak_id_config) {
        if (msg_chatak_id_config != NULL) {
            printf("halfFrameSize = %u\n", msg_chatak_id_config->halfFrameSize);
            printf("nChannels = %u\n", msg_chatak_id_config->nChannels);
            printf("fS = %u\n", msg_chatak_id_config->fS);
        }
        else {
            printf("(null)\n");
        }
    }


    void msg_chatak_id_printf(const msg_chatak_id_obj * obj, const char * label) {
    if (!obj) {
        printf("[CHATAK_ID][%s] obj is NULL\n", label);
        return;
    }

    printf("[CHATAK_ID][%s] TimeStamp: %llu  NumSounds: %d\n", label, obj->timeStamp, obj->numSounds);

    for (int i = 0; i < obj->numSounds; i++) {
        printf("  [%d] idNum: %u  name: \"%s\"  conf: %.2f%%\n",
               i, obj->sounds[i].idNum,
               obj->sounds[i].soundName,
               obj->sounds[i].confidence);
    }

    fflush(stdout);
    }
