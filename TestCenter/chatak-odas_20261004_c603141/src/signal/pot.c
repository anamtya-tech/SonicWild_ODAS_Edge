   
   /**
    * \file     pot.c

    */
    
    #include <signal/pot.h>

         pots_obj * pots_construct_zero(const unsigned int nPots) {
            pots_obj * obj = (pots_obj *) malloc(sizeof(pots_obj));
            obj->nPots = nPots;

            obj->array = (float *) malloc(sizeof(float) * 4 * nPots);
            memset(obj->array, 0x00, sizeof(float) * 4 * nPots);

            obj->spec_at_peak = malloc(sizeof(float *) * nPots);
            for (unsigned int i = 0; i < nPots; i++) {
                obj->spec_at_peak[i] = calloc(N_BINS, sizeof(float));
            }

            return obj;
        }


        pots_obj * pots_clone(const pots_obj * obj) {

          pots_obj * clone;

          clone = (pots_obj *) malloc(sizeof(pots_obj));

          clone->nPots = obj->nPots;
          clone->array = (float *) malloc(sizeof(float) * 4 * obj->nPots);
          memcpy(clone->array, obj->array, sizeof(float) * 4 * obj->nPots);
          clone->spec_at_peak = malloc(sizeof(float *) * obj->nPots);
            for (unsigned int i = 0; i < obj->nPots; i++) {
                clone->spec_at_peak[i] = malloc(sizeof(float) * N_BINS);
                memcpy(clone->spec_at_peak[i], obj->spec_at_peak[i], sizeof(float) * N_BINS);
            }


          return clone;

        }


    void pots_copy(pots_obj * dest, const pots_obj * src) {

      dest->nPots = src->nPots;

      memcpy(dest->array, src->array, sizeof(float) * 4 * src->nPots);
        for (unsigned int i = 0; i < src->nPots; i++) {
        memcpy(dest->spec_at_peak[i], src->spec_at_peak[i], sizeof(float) * N_BINS);
    }


    }

    void pots_zero(pots_obj * obj) {

      obj->nPots = 0;
      memset(obj->array, 0x00, sizeof(float) * 4 * obj->nPots);

        for (unsigned int i = 0; i < 4; i++) {
            for (unsigned int b = 0; b < N_BINS; b++) {
                obj->spec_at_peak[i][b] = 0.0f;
            }
        }

    }

    void pots_destroy(pots_obj * obj) {
        if (obj->spec_at_peak != NULL) {
            for (unsigned int i = 0; i < obj->nPots; i++) {
                free(obj->spec_at_peak[i]);
            }
            free(obj->spec_at_peak);
        }

        free(obj->array);
        free(obj);
    }


    void pots_printf(const pots_obj * obj) {

    unsigned int iPot;

    for (iPot = 0; iPot < obj->nPots; iPot++) {

        printf("(%02u): %+1.3f %+1.3f %+1.3f conf=%+1.3f\n",
               iPot,
               obj->array[iPot * 4 + 0],
               obj->array[iPot * 4 + 1],
               obj->array[iPot * 4 + 2],
               obj->array[iPot * 4 + 3]);

        // Find top-3 bins in spec_at_peak[iPot]
        float topVals[3] = { -1.0f, -1.0f, -1.0f };
        unsigned int topBins[3] = { 0, 0, 0 };

        for (unsigned int b = 0; b < N_BINS; b++) {
            float val = obj->spec_at_peak[iPot][b];

            if (val > topVals[0]) {
                topVals[2] = topVals[1]; topBins[2] = topBins[1];
                topVals[1] = topVals[0]; topBins[1] = topBins[0];
                topVals[0] = val;         topBins[0] = b;
            }
            else if (val > topVals[1]) {
                topVals[2] = topVals[1]; topBins[2] = topBins[1];
                topVals[1] = val;        topBins[1] = b;
            }
            else if (val > topVals[2]) {
                topVals[2] = val;        topBins[2] = b;
            }
        }

        printf(" Top bins: #%u (%.2f), #%u (%.2f), #%u (%.2f)\n",
               topBins[0], topVals[0],
               topBins[1], topVals[1],
               topBins[2], topVals[2]);
        }
    }
    
    void pots_log_to_file(const pots_obj *obj, const char *filepath, unsigned long long timestamp) {
    static int first_write_done = 0;

    FILE *fp;
    if (!first_write_done) {
        fp = fopen(filepath, "w");  // Overwrite once at start of session
        first_write_done = 1;
    } else {
        fp = fopen(filepath, "a");  // Append from here on
    }

    if (!fp) return;
    
    

    for (unsigned int iPot = 0; iPot < obj->nPots; iPot++) {
        // Write timestamp, pot ID (1-indexed), and XYZ/conf
        fprintf(fp, "%06llu,%u,%.2f,%.2f,%.2f,%.2f",
                timestamp,
                iPot + 1,
                obj->array[iPot * 4 + 0],
                obj->array[iPot * 4 + 1],
                obj->array[iPot * 4 + 2],
                obj->array[iPot * 4 + 3]);

        // Dump every bin from 0 to N_BINS - 1 (no grouping)
        for (unsigned int b = 0; b < N_BINS; b++) {
            fprintf(fp, ",%.2f", obj->spec_at_peak[iPot][b]);
        }

        fprintf(fp, "\n");  // End of pot row
    }

    fclose(fp);
    }



 
