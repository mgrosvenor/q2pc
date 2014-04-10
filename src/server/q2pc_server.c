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
#include "../protocol/q2pc_protocol.h"


static CH_ARRAY(TRANS_CONN)* cons = NULL;

typedef struct{
    i64 lo;
    i64 hi;
    i64 count;
} thread_params_t;

void* run_thread( void* p);

//Server wide global
static volatile bool stop_signal = false;
static volatile bool pause_signal = false;
static pthread_t* threads = NULL;
static i64 real_thread_count = 0;
static i64* votes_scoreboard = NULL;
static q2pc_trans_server* trans;

//Signal handler to terminate early
void term(int signo)
{
    ch_log_info("Terminating...\n");
    (void)signo;

    stop_signal = true;
    __sync_synchronize(); //Full fence

    if(threads){
        for(int i = 0; i < real_thread_count; i++){
            pthread_join(threads[i],NULL);
        }
    }

    if(trans){
        trans->delete(trans);
    }

    ch_log_info("Terminating... Done.\n");
    exit(0);
}


//Pause/unpause worker threads
void dopause_all(){ pause_signal = true;  __sync_synchronize(); }
void unpause_all(){ pause_signal = false; __sync_synchronize(); }


void server_init(const i64 thread_count, const i64 client_count , const transport_s* transport)
{

    //Signal handling for the main thread
    signal(SIGHUP,  term);
    signal(SIGKILL, term);
    signal(SIGTERM, term);
    signal(SIGINT, term);

    //Set up and init the voting scoreboard
    votes_scoreboard = aligned_alloc(sizeof(i64), sizeof(i64) * client_count);
    if(!votes_scoreboard){
        ch_log_fatal("Could not allocate memory for votes scoreboard\n");
    }

    bzero(votes_scoreboard,sizeof(i64) * client_count);

    //Set up all the connections
    ch_log_debug1("Waiting for clients to connect...\n");
    trans = server_factory(transport,client_count);
    cons = trans->connectall(trans, client_count);
    ch_log_debug1("Waiting for clients to connect... Done.\n");


    //Calculate the connection to thread mappins
    i64 cons_per_thread = MIN(client_count / thread_count, 1);
    real_thread_count  = MIN(thread_count, client_count);
    i64 lo = 0;
    i64 hi = lo + cons_per_thread;

    //Fire up the threads
    threads = (pthread_t*)calloc(real_thread_count, sizeof(pthread_t));
    for(int i = 0; i < real_thread_count; i++){
        ch_log_debug2("Starting thread %i with connections %li to %li\n", i, lo, hi);

        //Do this to avoid synchronisation errors
        thread_params_t* params = (thread_params_t*)calloc(1,sizeof(thread_params_t));
        if(!params){
            ch_log_fatal("Cannot allocate thread parameters\n");
        }
        params->lo      = lo;
        params->hi      = hi;
        params->count   = client_count;

        pthread_create(threads + i, NULL, run_thread, (void*)params);

        lo++;
        hi = lo + cons_per_thread;
        hi = MIN(cons->size,hi); //Clip so we don't go over the bounds
    }

}


void* run_thread( void* p)
{
    thread_params_t* params = (thread_params_t*)p;
    i64 lo      = params->lo;
    i64 hi      = params->hi;
    i64 count   = params->count;
    free(params);

    ch_log_debug3("Starting worker thread\n");
    while(!stop_signal){

        //Busy loop if we're told to stop processing for a moment
        if(pause_signal){ __asm__("pause"); continue; }

        for(int i = lo; i < hi; i++){
            q2pc_trans_conn* con = cons->off(cons,i);
            char* data = NULL;
            i64 len = 0;
            i64 result = con->beg_read(con,&data, &len);
            if(result != Q2PC_ENONE){
                con->end_read(con);
                continue;
            }

            q2pc_msg* msg = (q2pc_msg*)data;
            //Bounds check the answer

            if(msg->src_hostid < 0 || msg->src_hostid > count){
                ch_log_warn("Client ID (%li) is out of the expected range [%i,%i]. Ignoring vote\n", msg->src_hostid, 0, count);
                con->end_read(con);
                continue;
            }

            votes_scoreboard[msg->src_hostid] = msg->vote;
            ch_log_debug3("[%i] voted %s\n", msg->src_hostid, msg->vote ? "yes" : "no");

            con->end_read(con);
        }
    }

    ch_log_debug3("Cleaning up connections...\n");
    //We're done with the connections now, clean them up
    for(int i = lo; i < hi; i++){
        q2pc_trans_conn* con = cons->off(cons,i);
        con->delete(con);
    }

    ch_log_debug3("Exiting worker thread\n");
    return NULL;
}


void send_request()
{
    char* data;
    i64 len;
    if(trans->beg_write_all(trans,&data,&len)){
        ch_log_fatal("Could not complete broadcast message request\n");
    }

    if(len < (i64)sizeof(q2pc_msg)){
        ch_log_fatal("Not enough space to send a Q2PC message. Needed %li, but found %li\n", sizeof(q2pc_msg), len);
    }

    q2pc_msg* msg = (q2pc_msg*)data;
    msg->src_hostid = ~0LL;
    msg->vote       = ~0ULL;

    //Commit it
    trans->end_write_all(trans, sizeof(q2pc_msg));

}


void run_server(const i64 thread_count, const i64 client_count , const transport_s* transport)
{
    //Set up all the threads, scoreboard, transport connections etc.
    server_init(thread_count, client_count, transport);

    while(1){
        //Init the scoreboard
        for(int i = 0; i < client_count; i++){
            __builtin_prefetch(votes_scoreboard + i + 1);
            votes_scoreboard[i] = -1;
        }

        //send out a broadcast message to all servers
        send_request();

        //wait for all the responses
        usleep(200000);

        //Stop all the receiver threads
        dopause_all();

        //evaluate
        bool failed = false;
        for(int i = 0; i < client_count; i++){
            __builtin_prefetch(votes_scoreboard + i + 1);
            if(votes_scoreboard[i] == -1){
                ch_log_debug1("client %li did not vote.\n",i);
                failed = true;
            }
            if(votes_scoreboard[i] == 0){
                ch_log_debug1("client %li voted no.\n",i);
                failed = true;
            }
        }

        //Restart
        unpause_all();
    }

}

