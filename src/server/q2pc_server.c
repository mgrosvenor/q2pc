/*
 * q2pc_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

 //#LINKFLAGS=-lpthread

#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#include "q2pc_server.h"
#include "../transport/q2pc_transport.h"
#include "../errors/errors.h"



static CH_ARRAY(TRANS_CONN)* cons = NULL;

typedef struct{
    i64 lo;
    i64 hi;
} thread_params_t;

void* run_thread( void* p);


static bool stop_signal;
void term(int signo)
{
    ch_log_info("Terminating...\n");
    (void)signo;
    stop_signal = true;
    sleep(1);
    exit(0);
}

void run_server(const i64 thread_count, const i64 client_count , const transport_s* transport)
{

    //Signal handling for the main thread
    signal(SIGHUP,  term);
    signal(SIGKILL, term);
    signal(SIGTERM, term);
    signal(SIGINT, term);

    //Set up all the connections
    ch_log_debug1("Waiting for clients to connect...\n");
    q2pc_trans_server* trans = server_factory(transport,client_count);
    cons = trans->connectall(trans, client_count);
    trans->delete(trans);
    ch_log_debug1("Waiting for clients to connect... Done.\n");


    //Calculate the connection to thread mappins
    i64 cons_per_thread = MIN(client_count / thread_count, 1);
    i64 real_thread_count         = MIN(thread_count, client_count);
    i64 lo = 0;
    i64 hi = lo + cons_per_thread;

    //Fire up the threads
    pthread_t* threads = (pthread_t*)calloc(real_thread_count, sizeof(pthread_t));
    for(int i = 0; i < real_thread_count; i++){
        ch_log_debug2("Starting thread %i with connections %li to %li\n", i, lo, hi);

        //Do this to avoid synchronisation errors
        thread_params_t* params = (thread_params_t*)calloc(1,sizeof(thread_params_t));
        if(!params){
            ch_log_fatal("Cannot allocate thread paramters\n");
        }
        params->lo = lo;
        params->hi = hi;

        pthread_create(threads + i, NULL, run_thread, (void*)params);

        lo++;
        hi = lo + cons_per_thread;
        hi = MIN(cons->size,hi); //Clip so we don't go over the bounds
    }

    //Wait for them to finish
    for(int i = 0; i < real_thread_count; i++){
        pthread_join(threads[i],NULL);
    }

    return;
}



void* run_thread( void* p)
{
    thread_params_t* params = (thread_params_t*)p;
    i64 lo = params->lo;
    i64 hi = params->hi;
    free(params);

    while(!stop_signal){
        for(int i = lo; i < hi; i++){
            q2pc_trans_conn* con = cons->off(cons,i);
            char* data = NULL;
            i64 len = 0;
            i64 result = con->beg_read(con,&data, &len);
            if(result != Q2PC_ENONE){
                con->end_read(con);
                continue;
            }

            ch_log_debug2("Got %lu bytes in read: %.*s\n", len, len, data);
            con->end_read(con);
        }
    }


    for(int i = lo; i < hi; i++){
        q2pc_trans_conn* con = cons->off(cons,i);
        con->delete(con);
    }

    return NULL;
}
