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
#include <sys/time.h>

#include "q2pc_server.h"
#include "../transport/q2pc_transport.h"
#include "../errors/errors.h"
#include "../protocol/q2pc_protocol.h"


typedef struct{
    i64 lo;
    i64 hi;
    i64 count;
    i64 thread_id;
} thread_params_t;

void* run_thread( void* p);

//Server wide global
static CH_ARRAY(TRANS_CONN)* cons       = NULL;
static volatile bool stop_signal        = false;
static volatile bool pause_signal       = false;
static pthread_t* threads               = NULL;
static i64 real_thread_count            = 0;
static volatile i64* votes_scoreboard   = NULL;
static volatile i64* votes_count        = NULL;
static q2pc_trans* trans;


void cleanup()
{
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

}


//Signal handler to terminate early
void term(int signo)
{
    ch_log_info("Terminating...\n");
    (void)signo;
    cleanup();

    ch_log_info("Terminating... Done.\n");
    exit(0);
}


//Pause/unpause worker threads
void dopause_all(){ pause_signal = true;  __sync_synchronize(); }
void unpause_all(){ pause_signal = false; __sync_synchronize(); }

//Wait for all clients to connect
void do_connectall(i64 client_count)
{
    cons = CH_ARRAY_NEW(TRANS_CONN,client_count,NULL);
    if(!cons){ ch_log_fatal("Cannot allocate connections array\n"); }

    for(int i = 0; i < client_count; i++){
        trans->connect(trans, cons->off(cons,i));
    }

}



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
    bzero((void*)votes_scoreboard,sizeof(i64) * client_count);


    //Set up all the connections
    ch_log_info("Waiting for clients to connect...\n\r");
    trans = trans_factory(transport);
    do_connectall(client_count);
    ch_log_info("Waiting for clients to connect... Done.\n");


    //Calculate the connection to thread mappings
    i64 cons_per_thread = MAX(client_count / thread_count, 1);
    real_thread_count  = MIN(thread_count, client_count);
    i64 lo = 0;
    i64 hi = lo + cons_per_thread;

    votes_count = aligned_alloc(sizeof(i64), sizeof(i64) * real_thread_count);
    if(!votes_count){
        ch_log_fatal("Could not allocate memory for votes counter\n");
    }
    bzero((void*)votes_count,sizeof(i64) * real_thread_count);


    //Fire up the threads
    threads = (pthread_t*)calloc(real_thread_count, sizeof(pthread_t));
    for(int i = 0; i < real_thread_count; i++){
        ch_log_debug2("Starting thread %i with connections %li\n", i, lo, hi -1);

        //Do this to avoid synchronisation errors
        thread_params_t* params = (thread_params_t*)calloc(1,sizeof(thread_params_t));
        if(!params){
            ch_log_fatal("Cannot allocate thread parameters\n");
        }
        params->lo          = lo;
        params->hi          = hi;
        params->count       = client_count;
        params->thread_id   = i;

        pthread_create(threads + i, NULL, run_thread, (void*)params);

        lo++;
        hi = lo + cons_per_thread;
        hi = MIN(cons->size,hi); //Clip so we don't go over the bounds
    }

}


void* run_thread( void* p)
{
    thread_params_t* params = (thread_params_t*)p;
    i64 lo          = params->lo;
    i64 hi          = params->hi;
    i64 count       = params->count;
    i64 thread_id   = params->thread_id;
    free(params);

    ch_log_debug3("Running worker thread\n");
    while(!stop_signal){

        //Busy loop if we're told to stop processing for a moment
        if(pause_signal){
            votes_count[thread_id] = 0;
            __asm__("pause");
            continue;
        }

        //Otherwise, busy loop looking for data
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


            votes_count[thread_id]++;
            ch_log_debug2("Q2PC Server: [%li] Vote count=%li\n", thread_id,votes_count[thread_id]);

            votes_scoreboard[msg->src_hostid] = msg->type;
            switch(msg->type){
                case q2pc_vote_yes_msg: ch_log_debug2("Q2PC Server: [%i]<-- vote yes from (%li)\n", thread_id, msg->src_hostid); break;
                case q2pc_vote_no_msg:  ch_log_debug2("Q2PC Server: [%i]<-- vote no  from (%li)\n", thread_id, msg->src_hostid); break;
                case q2pc_ack_msg:      ch_log_debug2("Q2PC Server: [%i]<-- ack      from (%li)\n", thread_id, msg->src_hostid); break;
                default:
                    ch_log_warn("Q2PC Server: [%i] <-- Unknown message (%i)   from (%li)\n",thread_id, msg->type, msg->src_hostid );
            }
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


void send_request(q2pc_msg_type_t msg_type)
{
    ch_log_debug3("Sending request of type %i...\n", msg_type);
    char* data;
    i64 len;
    if(trans->beg_write_all(trans,&data,&len)){
        ch_log_fatal("Could not complete broadcast message request\n");
    }

    if(len < (i64)sizeof(q2pc_msg)){
        ch_log_fatal("Not enough space to send a Q2PC message. Needed %li, but found %li\n", sizeof(q2pc_msg), len);
    }

    q2pc_msg* msg = (q2pc_msg*)data;
    msg->type       = msg_type;
    msg->src_hostid = ~0LL;

    //Commit it
    trans->end_write_all(trans, sizeof(q2pc_msg));
    ch_log_debug3("Sending request of type %i...done\n", msg_type);
}


typedef enum {  q2pc_request_success, q2pc_request_fail, q2pc_commit_success, q2pc_commit_fail, q2pc_cluster_fail } q2pc_commit_status_t;

void wait_for_votes(i64 client_count, i64 timeout_us)
{
    struct timeval ts_start = {0};
    struct timeval ts_now   = {0};
    i64 ts_start_us         = 0;
    i64 ts_now_us           = 0;

    gettimeofday(&ts_start, NULL);
    ts_start_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_usec;


    //Wait to either timeout or for all votes to be counted
    ch_log_debug2("Q2PC Server: [M] Waiting for votes\n");
    while(1){
        if(timeout_us >= 0){
             gettimeofday(&ts_now, NULL);
             ts_now_us = ts_now.tv_sec * 1000 * 1000 + ts_now.tv_usec;
             if(ts_now_us > ts_start_us + timeout_us){
                 ch_log_warn("Timed out waiting for client response(s)\n");
                 break;
             }
        }

        i64 total_votes = 0;
        for(int i = 0; i < real_thread_count; i++){
            total_votes+= votes_count[i];
        }
        if(total_votes >= client_count){
            ch_log_debug2("Q2PC Server: [M] Done, collected %li votes\n", total_votes);
            break;
        }
    }

}


q2pc_commit_status_t do_phase1(i64 client_count, i64 timeout_us)
{

    q2pc_commit_status_t result = q2pc_request_success;

    //Init the scoreboard
    for(int i = 0; i < client_count; i++){
        __builtin_prefetch((char*)votes_scoreboard + i + 1);
        votes_scoreboard[i] = q2pc_lost_msg;
    }

    //send out a broadcast message to all servers
    ch_log_debug2("Q2PC Server: [M]--> request\n");
    send_request(q2pc_request_msg);

    //wait for all the responses
    wait_for_votes(client_count, timeout_us);

    //Stop all the receiver threads
    dopause_all();
    for(int i = 0; i < client_count; i++){
        __builtin_prefetch((char*)votes_scoreboard + i + 1);
        switch(votes_scoreboard[i]){
            case q2pc_vote_yes_msg:
                ch_log_debug1("client %li voted yes.\n",i);
                continue;

            case q2pc_vote_no_msg:
                ch_log_debug1("client %li voted no.\n",i);
                result = q2pc_request_fail;
                break;

            case q2pc_lost_msg:
                ch_log_warn("Q2PC: phase 1 - client %li message lost, cluster failed\n",i);
                result = q2pc_cluster_fail;
                break;

            default:
                ch_log_debug1("Q2PC: Server [M] phase 1 - client %li sent an unexpected message type %i\n",i,votes_scoreboard[i]);
                ch_log_fatal("Protocol violation\n");
        }
    }

    for(int i = 0; i < real_thread_count; i++){
        votes_count[i] = 0;
    }

    unpause_all();

    return result;
}


q2pc_commit_status_t do_phase2(q2pc_commit_status_t phase1_status, i64 client_count, i64 timeout_us)
{

    //Init the scoreboard
    for(int i = 0; i < client_count; i++){
        __builtin_prefetch((char*)votes_scoreboard + i + 1);
        votes_scoreboard[i] = q2pc_lost_msg;
    }

    switch(phase1_status){
        case q2pc_request_success:
            ch_log_debug2("Q2PC Server: [M]--> commit\n");
            send_request(q2pc_commit_msg);
            break;
        case q2pc_request_fail:
            ch_log_debug2("Q2PC Server: [M]--> cancel\n");
            send_request(q2pc_cancel_msg);
            break;
        case q2pc_cluster_fail:
            return q2pc_cluster_fail;
        default:
            ch_log_fatal("Internal error: unexpected result from phase 1\n");
    }

    //wait for all the responses
    wait_for_votes(client_count, timeout_us);

    //Stop all the receiver threads
    dopause_all();
    q2pc_commit_status_t result = q2pc_commit_success;
    for(int i = 0; i < client_count; i++){
        __builtin_prefetch((char*)votes_scoreboard + i + 1);

        switch(votes_scoreboard[i]){
            case q2pc_ack_msg:
                continue;

            case q2pc_lost_msg:
                ch_log_warn("Q2PC: Server [M] phase 2 - client %li message lost, cluster failed\n",i);
                result = q2pc_cluster_fail;
                break;
            default:
                ch_log_debug1("Q2PC: Server [M] phase 2 - client %li sent an unexpected message type %i\n",i,votes_scoreboard[i]);
                result = q2pc_cluster_fail;
        }
    }

    for(int i = 0; i < real_thread_count; i++){
        votes_count[i] = 0;
    }

    unpause_all();

    if(result == q2pc_cluster_fail){
        return q2pc_cluster_fail;
    }

    switch(phase1_status){
        case q2pc_request_success:  return q2pc_commit_success;
        case q2pc_request_fail:     return q2pc_commit_fail;
        default:
            ch_log_fatal("Internal error: unexpected result from phase 1\n");
    }

    //Unreachable
    return -1;

}

void run_server(const i64 thread_count, const i64 client_count , const transport_s* transport)
{
    //Set up all the threads, scoreboard, transport connections etc.
    server_init(thread_count, client_count, transport);

    //Statistics keeping
    struct timeval ts_start = {0};
    struct timeval ts_now   = {0};
    i64 ts_start_us         = 0;
    i64 ts_now_us           = 0;

    gettimeofday(&ts_start, NULL);
    ts_start_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_usec;

    const i64 report_int = 100;
    ch_log_info("Running...\n");
    for(i64 requests = 0; ; requests++){

        if(requests && (requests % report_int == 0) ){
            gettimeofday(&ts_now, NULL);
            ts_now_us = ts_now.tv_sec * 1000 * 1000 + ts_now.tv_usec;

            const i64 time_taken_us = ts_now_us - ts_start_us;
            double reqs_per_sec = (double)report_int / (double)(time_taken_us) * 1000 * 1000;

            ch_log_info("Running at %0.2lf req/s (%li)\n", reqs_per_sec, time_taken_us);

            gettimeofday(&ts_start, NULL);
            ts_start_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_usec;
        }


        q2pc_commit_status_t status;
        status = do_phase1(client_count, 2000*1000);
        status = do_phase2(status,client_count, 2000*1000);

        switch(status){
            case q2pc_cluster_fail:     cleanup(); ch_log_fatal("Cluster failed\n"); break;
            case q2pc_commit_success:   ch_log_debug1("Commit success!\n"); break;
            case q2pc_commit_fail:      ch_log_debug1("Commit fail!\n"); break;
            default:
                ch_log_fatal("Internal error: unexpected result from phase 2\n");
        }
    }

}

