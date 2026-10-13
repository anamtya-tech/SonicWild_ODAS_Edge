#ifndef __ODAS_MESSAGE_CHATAK_ID
#define __ODAS_MESSAGE_CHATAK_ID


    #include <stdlib.h>


    #define MAX_SOUND_ROWS 10
    #define MAX_SOUND_NAME_LEN 64

    typedef struct {
        unsigned int idNum;
        char soundName[MAX_SOUND_NAME_LEN];
        float confidence;
        } sound_id;
    
    
    typedef struct msg_chatak_id_obj {
        unsigned long long timeStamp;
        sound_id sounds[MAX_SOUND_ROWS];
        int numSounds; 
        } msg_chatak_id_obj;


    typedef struct msg_chatak_id_cfg {

        unsigned int halfFrameSize;
        unsigned int nChannels;
        unsigned int fS;

    } msg_chatak_id_cfg;

    msg_chatak_id_obj * msg_chatak_id_construct(const msg_chatak_id_cfg * msg_chatak_id_config);

    void msg_chatak_id_destroy(msg_chatak_id_obj * obj);

    void msg_chatak_id_copy(msg_chatak_id_obj * dest, const msg_chatak_id_obj * src);

    void msg_chatak_id_zero(msg_chatak_id_obj * obj);

    unsigned int msg_chatak_id_isZero(const msg_chatak_id_obj * obj);

    msg_chatak_id_cfg * msg_chatak_id_cfg_construct(void);

    void msg_chatak_id_cfg_destroy(msg_chatak_id_cfg * msg_chatak_id_config);

    void msg_chatak_id_cfg_printf(const msg_chatak_id_cfg * msg_chatak_id_config);
    
    void msg_chatak_id_printf(const msg_chatak_id_obj * obj, const char * label);

#endif
