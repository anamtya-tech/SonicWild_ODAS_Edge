
    #include <module/mod_chatak.h>
    #define MAX_SOUND_NAME_LEN 64       // Adjust based on your soundName array size
    #define N_FREQ_BINS 256             // Number of frequency bins to serialize (adjust to match halfFrameSize * 2)

    int persistent_sock = -1;


    mod_chatak_obj * mod_chatak_construct(const mod_chatak_cfg * mod_chatak_config) {

        mod_chatak_obj * obj;

        obj = (mod_chatak_obj *) malloc(sizeof(mod_chatak_obj));
        
        obj->in = (msg_spectra_obj *) NULL;
        obj->out = (msg_chatak_id_obj *) NULL;

        obj->enabled = 0;

        return obj;

    }


    void mod_chatak_destroy(mod_chatak_obj * obj) {

        free((void *) obj);

    }

        int mod_chatak_process(mod_chatak_obj * obj) {

    int rtnValue;
    unsigned long long ts = obj->in->timeStamp;
    unsigned int phase = (ts / 300) % 4;

    if (msg_spectra_isZero(obj->in) == 0) {
        
        send_spectra_to_python(obj->in);

        obj->out->timeStamp = ts;
        obj->out->numSounds = 0;

        if (phase >= 1) {
            obj->out->sounds[0].idNum = 1;
            snprintf(obj->out->sounds[0].soundName, MAX_SOUND_NAME_LEN, "crow");
            obj->out->sounds[0].confidence = 85.0f;
            obj->out->numSounds = 1;
        }

        if (phase >= 2) {
            obj->out->sounds[1].idNum = 2;
            snprintf(obj->out->sounds[1].soundName, MAX_SOUND_NAME_LEN, "cow");
            obj->out->sounds[1].confidence = 90.0f;
            obj->out->numSounds = 2;
        }

        if (phase == 3) {
            obj->out->sounds[2].idNum = 3;
            snprintf(obj->out->sounds[2].soundName, MAX_SOUND_NAME_LEN, "car");
            obj->out->sounds[2].confidence = 60.0f;
            obj->out->numSounds = 3;
        }

        rtnValue = 0;

    } else {

        msg_chatak_id_zero(obj->out);
        rtnValue = -1;

    }

    return rtnValue;
    }  


    

    void mod_chatak_connect(mod_chatak_obj * obj, msg_spectra_obj * in, msg_chatak_id_obj * out) {

        obj->in = in;
        obj->out = out;

    }

    void mod_chatak_disconnect(mod_chatak_obj * obj) {

        obj->in = (msg_spectra_obj *) NULL;
        obj->out = (msg_chatak_id_obj *) NULL;

    }

    void mod_chatak_enable(mod_chatak_obj * obj) {

        obj->enabled = 1;

    }

    void mod_chatak_disable(mod_chatak_obj * obj) {

        obj->enabled = 0;

    }

    mod_chatak_cfg * mod_chatak_cfg_construct(void) {

        mod_chatak_cfg * cfg;

        cfg = (mod_chatak_cfg *) malloc(sizeof(mod_chatak_cfg));

        return cfg;

    }

    void mod_chatak_cfg_destroy(mod_chatak_cfg * cfg) {

        free((void *) cfg);

    }


    void mod_chatak_cfg_printf(const mod_chatak_cfg * cfg) {

    }

        int send_spectra_to_python(const msg_spectra_obj *in) {
            if (!in || !in->freqs || !in->freqs->array) return -1;

            unsigned int nSignals = in->freqs->nSignals;
            unsigned int nBins = in->freqs->halfFrameSize * 2;

            spectra_copy *copy = malloc(sizeof(spectra_copy));
            if (!copy) return -1;

            copy->timeStamp = in->timeStamp;
            copy->fS = in->fS;
            copy->nSignals = nSignals;
            copy->nBins = nBins;

            copy->array = malloc(nSignals * sizeof(float *));
            if (!copy->array) {
                free(copy);
                return -1;
            }

            for (unsigned int ch = 0; ch < nSignals; ch++) {
                copy->array[ch] = malloc(nBins * sizeof(float));
                if (!copy->array[ch]) {
                    for (unsigned int j = 0; j < ch; j++) free(copy->array[j]);
                    free(copy->array);
                    free(copy);
                    return -1;
                }
                memcpy(copy->array[ch], in->freqs->array[ch], nBins * sizeof(float));
            }

            pthread_t tid;
            if (pthread_create(&tid, NULL, send_binary_worker, copy) != 0) {
                for (unsigned int ch = 0; ch < nSignals; ch++) free(copy->array[ch]);
                free(copy->array);
                free(copy);
                return -1;
            }

            pthread_detach(tid);  // Fire and forget
            
            return 0;
            
        }


        void *send_binary_worker(void *arg) {
            spectra_copy *copy = (spectra_copy *)arg;

            // Ignore SIGPIPE to avoid crashing if receiver closes
            signal(SIGPIPE, SIG_IGN);

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) goto cleanup;

            struct sockaddr_in addr = {
                .sin_family = AF_INET,
                .sin_port = htons(SERVER_PORT),
            };
            inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                close(sock);
                goto cleanup;
            }

            // Flatten [nSignals][nBins] ? [nSignals * nBins]
            unsigned int total = copy->nSignals * copy->nBins;
            float *flat = malloc(sizeof(float) * total);
            if (!flat) goto cleanup;

            for (unsigned int ch = 0; ch < copy->nSignals; ch++) {
                memcpy(flat + ch * copy->nBins, copy->array[ch], sizeof(float) * copy->nBins);
            }

            // ?? Step 1: Prefix total size (in network byte order)
            uint32_t packet_size = 4 + 8 + 4 + 2 + 2 + sizeof(float) * total;
            uint32_t net_packet_size = htonl(packet_size);
            if (write(sock, &net_packet_size, 4) <= 0) goto cleanup;

            // Prepare header with proper byte order
            uint32_t magic     = htonl(PACKET_MAGIC);
            uint64_t ts        = htobe64(copy->timeStamp);
            uint32_t fS        = htonl(copy->fS);
            uint16_t nSig      = htons(copy->nSignals);
            uint16_t nBins     = htons(copy->nBins);

            if (write(sock, &magic, 4)       <= 0 ||
                write(sock, &ts, 8)          <= 0 ||
                write(sock, &fS, 4)          <= 0 ||
                write(sock, &nSig, 2)        <= 0 ||
                write(sock, &nBins, 2)       <= 0 ||
                write(sock, flat, sizeof(float) * total) != sizeof(float) * total) {
                goto cleanup;
            }

            close(sock);
            free(flat);

        cleanup:
            for (unsigned int ch = 0; ch < copy->nSignals; ch++) free(copy->array[ch]);
            free(copy->array);
            free(copy);
            return NULL;
        }
