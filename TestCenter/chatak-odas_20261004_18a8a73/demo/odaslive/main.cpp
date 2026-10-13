
    #include <odas/odas.h>

extern "C" {
    #include "parameters.h"
    #include "configs.h"
    #include "objects.h"
    #include "threads.h"
    #include "profiler.h"
    #include "odas/python/python_zodas.h"
}


    #include <getopt.h>
    #include <time.h>
    #include <signal.h>
    #include <unistd.h>

    #include <tensorflow/lite/c/c_api.h>

    #include <string>
    #include "yamnet_classifier.h"

    static YAMNetClassifier g_yamnet;   // global instance


    // +----------------------------------------------------------+
    // | Variables                                                |
    // +----------------------------------------------------------+  

        // +------------------------------------------------------+
        // | Type                                                 |
        // +------------------------------------------------------+   

            enum processing {
                processing_singlethread,
                processing_multithread
            } type;
            

        // +------------------------------------------------------+
        // | GetPID                                               |
        // +------------------------------------------------------+   

            pid_t zodas_pid ;

        // +------------------------------------------------------+
        // | Getopt                                               |
        // +------------------------------------------------------+   

            int c;
            int iArg;
            char * file_config;
            char *typeStr;
            int record_enabled =0;
            enum { record_mode_none = 0, record_mode_live = 1, record_mode_passive = 2 } record_mode = record_mode_none;

        // +------------------------------------------------------+
        // | Objects                                              |
        // +------------------------------------------------------+   

            objects * objs;
            aobjects * aobjs;

        // +------------------------------------------------------+
        // | Configurations                                       |
        // +------------------------------------------------------+        

            configs * cfgs;   

        // +------------------------------------------------------+
        // | Profiler                                             |
        // +------------------------------------------------------+   

            profiler * prf;

        // +------------------------------------------------------+
        // | Flag                                                 |
        // +------------------------------------------------------+                   

            char stopProcess;

    // +----------------------------------------------------------+
    // | Signal handler                                           |
    // +----------------------------------------------------------+  

    void sighandler(int signum) {
        
        if (type == processing_singlethread) {
            stopProcess = 1;
        }
        if (type == processing_multithread) {
            threads_multiple_stop(aobjs);
        }        

    }

    // +----------------------------------------------------------+
    // | Main routine                                             |
    // +----------------------------------------------------------+  

    int main(int argc, char * argv[]) {

        // +------------------------------------------------------+
        // | Arguments                                            |
        // +------------------------------------------------------+  

            file_config = (char *) NULL;
            char verbose = 0x00;

            type = processing_multithread;
            
            zodas_pid = getpid();
            

            /* Accept aliases -rl and -rp by rewriting them to -r before getopt parsing. */
            for (iArg = 1; iArg < argc; iArg++) {
                if (strcmp(argv[iArg], "-rl") == 0) {
                    record_mode = record_mode_live;
                    argv[iArg] = (char *) "-r";
                }
                else if (strcmp(argv[iArg], "-rp") == 0) {
                    record_mode = record_mode_passive;
                    argv[iArg] = (char *) "-r";
                }
            }

            while ((c = getopt(argc,argv, "c:hsvr")) != -1) {

                switch(c) {

                    case 'c':

                        file_config = (char *) malloc(sizeof(char) * (strlen(optarg)+1));
                        strcpy(file_config, optarg);                        

                    break;

                    case 'h':

                        printf("+----------------------------------------------------+\n");
                        printf("|        ODAS (Open embeddeD Audition System)        |\n");
                        printf("+----------------------------------------------------+\n");
                        printf("| Author:      Francois Grondin                      |\n");
                        printf("| Email:       francois.grondin2@usherbrooke.ca      |\n");
                        printf("| Website:     introlab.3it.usherbrooke.ca           |\n");
                        printf("| Repository:  github.com/introlab/odas              |\n");
                        printf("| Version:     1.0                                   |\n");
                        printf("+----------------------------------------------------+\n");        
                        printf("| -c       Configuration file (.cfg)                 |\n");
                        printf("| -h       Help                                      |\n");
                        printf("| -s       Process sequentially (no multithread)     |\n");
                        printf("| -v       Verbose                                   |\n");
                        printf("| -rl      Record to raw.liveRecordPath              |\n");
                        printf("| -rp      Record to raw.passiveRecordPath           |\n");
                        printf("+----------------------------------------------------+\n");                

                        exit(EXIT_SUCCESS);

                    break;

                    case 's':

                        type = processing_singlethread;

                    break;

                    case 'v':

                        verbose = 0x01;

                    break;
                    
                    case 'r':

                        record_enabled = 1;
                        if (record_mode == record_mode_none) {
                            record_mode = record_mode_live;
                        }

                    break;

                }

            }

            if (file_config == NULL) {
                printf("Missing configuration file.\n");
                exit(EXIT_FAILURE);
            }

            if (record_mode == record_mode_passive) {
                setenv("ODAS_RECORD_MODE", "passive", 1);
            }
            else {
                setenv("ODAS_RECORD_MODE", "live", 1);
            }

        // +------------------------------------------------------+
        // | Copyright                                            |
        // +------------------------------------------------------+ 

            if (verbose == 0x01) {

            printf("+--------------------------------------------+\n");
            printf("|    ODAS (Open embeddeD Audition System)    |\n");
            printf("+--------------------------------------------+\n");
            printf("| Author:  Francois Grondin                  |\n");
            printf("| Email:   francois.grondin2@usherbrooke.ca  |\n");
            printf("| Website: introlab.3it.usherbrooke.ca       |\n");
            printf("| Version: 1.0                               |\n");
            printf("+--------------------------------------------+\n");

            }

        // +------------------------------------------------------+
        // | Single thread                                        |
        // +------------------------------------------------------+  

            if (type == processing_singlethread) {

            // +--------------------------------------------------+
            // | Profiler                                         |
            // +--------------------------------------------------+ 

                prf = profiler_construct();

            // +--------------------------------------------------+
            // | Configure                                        |
            // +--------------------------------------------------+ 

                if (verbose == 0x01) printf("| + Initializing configurations...... "); fflush(stdout); 

                cfgs = configs_construct(file_config);

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Construct                                        |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Initializing objects............. "); fflush(stdout); 

                objs = objects_construct(cfgs);    
                
                if (verbose == 0x01) printf("[Done] |\n");   

            // +--------------------------------------------------+
            // | Processing                                       |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Processing....................... "); fflush(stdout);

                threads_single_open(objs);
                stopProcess = 0;
                while((threads_single_process(objs, prf) == 0) && (stopProcess == 0));
                threads_single_close(objs);

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Free memory                                      |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Free memory...................... "); fflush(stdout);

                objects_destroy(objs); 
                configs_destroy(cfgs);
                free((void *) file_config);

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Results                                          |
            // +--------------------------------------------------+  

                if (verbose == 0x01) profiler_printf(prf);

                profiler_destroy(prf);

            }

        // +------------------------------------------------------+
        // | Multiple threads                                     |
        // +------------------------------------------------------+  

            if (type == processing_multithread) {

            // +--------------------------------------------------+
            // | Configure                                        |
            // +--------------------------------------------------+ 

                if (verbose == 0x01) printf("| + Initializing configurations...... "); fflush(stdout); 

                cfgs = configs_construct(file_config);

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Construct                                        |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Initializing objects............. "); fflush(stdout); 
                
                aobjs = aobjects_construct(cfgs);    

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Launch threads                                   |
            // +--------------------------------------------------+  

                signal(SIGINT, sighandler);

                if (verbose == 0x01) printf("| + Launch threads................... "); fflush(stdout); 

                threads_multiple_start(aobjs);

                if (verbose == 0x01) printf("[Done] |\n");

                // +--------------------------------------------------+
                // | Load Classifier                                  |
                // +--------------------------------------------------+  

          
                const char* modelDir = parameters_lookup_string(file_config, "raw.model_path");
                if (modelDir != NULL) {
                    std::string model_path   = std::string(modelDir) + "/yamnet_core.tflite";
                    std::string labels_path  = std::string(modelDir) + "/yamnet_class_map.csv";

                    if (g_yamnet.LoadModel(model_path.c_str()) &&
                        g_yamnet.LoadClassNames(labels_path.c_str())) {
                        printf("| + Loaded Classifier................. [Done] |\n"); fflush(stdout);
                    } else {
                        if (verbose == 0x01){printf("| + Classifier load failed............ [Done] |\n"); fflush(stdout);}
                    }
                } else {
                    if (verbose == 0x01){printf("| + No model_path in config........... [Skipped] |\n"); fflush(stdout);}
                }

            // +--------------------------------------------------+
            // | Start recorder                                   |
            // +--------------------------------------------------+  
    
                typeStr = parameters_lookup_string(file_config, "raw.interface.type");
                if (record_enabled) {
                    if (strcmp(typeStr, "file") == 0) {
                        printf("| + Skipping record for file read.... [Done] |\n"); fflush(stdout);
                    } else {
                        const char *recordPath = (record_mode == record_mode_passive)
                                                 ? cfgs->mod_resample_mics_config->passiveRecordPath
                                                 : cfgs->mod_resample_mics_config->liveRecordPath;
                        const char *sessionPrefix = (record_mode == record_mode_passive)
                                                    ? "passiveSession"
                                                    : "liveSession";

                        call_sync_zodas(
                            cfgs->msg_hops_mics_raw_config->fS,
                            cfgs->msg_hops_mics_raw_config->hopSize,
                            cfgs->mod_resample_mics_config->nBits,
                            cfgs->msg_hops_mics_raw_config->nChannels,
                            recordPath,
                            sessionPrefix,
                            file_config,
                            zodas_pid
                        );
                        if (verbose == 0x01){printf("| + Record Audio Started............. [Done] |\n"); fflush(stdout);}
                    }
                } else {
                   if (verbose == 0x01) { printf("| + Starting Without Record.......... [Done] |\n"); fflush(stdout);}
                }


            // +--------------------------------------------------+
            // | Wait                                             |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Threads running..................\n\n "); fflush(stdout); 
                
                threads_multiple_join(aobjs);

                if (verbose == 0x01) printf("[Done] |\n");

            // +--------------------------------------------------+
            // | Free memory                                      |
            // +--------------------------------------------------+  

                if (verbose == 0x01) printf("| + Free memory...................... "); fflush(stdout);

                aobjects_destroy(aobjs);
                configs_destroy(cfgs);
                free((void *) file_config);
                free((void *) typeStr);

                if (verbose == 0x01) printf("[Done] |\n");

                if (verbose == 0x01) printf("+--------------------------------------------+\n");

            }

        return 0;

    }
