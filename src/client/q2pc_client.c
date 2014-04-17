/*
 * q2pc_client.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>

#include "q2pc_client.h"
#include "../../deps/chaste/chaste.h"
#include "../transport/q2pc_transport.h"
#include "../errors/errors.h"
#include "../protocol/q2pc_protocol.h"

//Local globals
static q2pc_trans* trans    = NULL;
static q2pc_trans_conn conn = {0};
static i64 client_num       = -1;
static u64 vote_count       = 0;

static void term(int signo)
{
    ch_log_info("Terminating...\n");
    (void)signo;

    if(trans){ trans->delete(trans); }
    //if(conn.priv) { conn.delete(&conn); }

    ch_log_info("Terminating... Done.\n");
    exit(0);
}

static void init(const transport_s* transport)
{
    //Signal handling for the main thread
    signal(SIGHUP,  term);
    signal(SIGKILL, term);
    signal(SIGTERM, term);
    signal(SIGINT,  term);

    //Set up all the connections
    ch_log_debug1("Connecting to server...\n");
    trans = trans_factory(transport);

    bool connected = false;
    while(!connected){
                          //Connections are non-blocking
        if(trans->connect(trans, &conn)){
            continue;
        }

        char* data;
        i64 len;
        if(conn.beg_write(&conn,&data, &len)){
            continue;
        }

        if(len < (i64)sizeof(q2pc_msg)){
            ch_log_fatal("Message buffer is smaller than Q2PC message needs be. (%li<%li)\n", len, sizeof(q2pc_msg));
        }

        q2pc_msg* msg   = (q2pc_msg*)data;
        msg->type       = q2pc_con_msg;
        msg->src_hostid = transport->client_id;

        conn.end_write(&conn, sizeof(q2pc_msg));
        connected = true;

    }

    ch_log_debug1("Connecting to server...Done.\n");
}

typedef enum { q2pc_pahse1, q2pc_phase2 } q2pc_phase_t;


static q2pc_msg* get_messge(i64 wait_usecs)
{
    char* data = NULL;
    i64 len = 0;
    i64 result = Q2PC_EAGAIN;

    struct timeval ts_start = {0};
    struct timeval ts_now = {0};
    i64 ts_start_us = 0;
    i64 ts_now_us = 0;

    gettimeofday(&ts_start, NULL);
    ts_start_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_usec;


    ch_log_debug3("Waiting for new requests...\n");
    while(result == Q2PC_EAGAIN){
        result = conn.beg_read(&conn,&data, &len);

        if(result == Q2PC_ENONE){
            q2pc_msg* msg = (q2pc_msg*)data;
            conn.end_read(&conn);
            return msg;
        }

        if(result == Q2PC_EFIN){
            ch_log_warn("Server quit\n");
            conn.end_read(&conn);
            return NULL;
        }

        if(wait_usecs >= 0){
            gettimeofday(&ts_now, NULL);
            ts_now_us = ts_now.tv_sec * 1000 * 1000 + ts_now.tv_usec;
            if(ts_now_us > ts_start_us + wait_usecs){
                ch_log_warn("Timed out waiting for server response\n");
                return NULL;
            }
        }
    }

    //Unreachable
    return NULL;
}


static void send_response(q2pc_msg_type_t msg_type)
{
    char* data;
    i64 len;
    if(conn.beg_write(&conn,&data,&len)){
        ch_log_fatal("Could not complete message request\n");
    }

    if(len < (i64)sizeof(q2pc_msg)){
        ch_log_fatal("Not enough space to send a Q2PC message. Needed %li, but found %li\n", sizeof(q2pc_msg), len);
    }

    q2pc_msg* msg = (q2pc_msg*)data;
    msg->type       = msg_type;
    msg->src_hostid = client_num;

    //Commit it
    conn.end_write(&conn, sizeof(q2pc_msg));

}


static int do_phase1(i64 timeout)
{
    q2pc_msg* msg = get_messge(timeout);
    if(!msg){
        ch_log_error("Server has terminated. Cannot continue\n");
        term(0);
    }

    //XXX HACK: 1 in 5 votes will fail
    u64 vote_yes = (vote_count % 5);


    switch(msg->type){
    case q2pc_request_msg:
        ch_log_debug2("Q2PC Client: [M]<-- request\n");

        if(vote_yes){
            ch_log_debug2("Q2PC Client: [M]--> vote yes\n");
            send_response(q2pc_vote_yes_msg);
            break;
        }
        else{
            ch_log_debug2("Q2PC Client: [M]--> vote no\n");
            send_response(q2pc_vote_no_msg);
            break;
        }
    default:
        ch_log_debug2("Q2PC Client: [M]<-- Unknown message (%i)\n", msg->type);
        ch_log_fatal("Protocol failure, in phase 1 unexpected message type %i\n", msg->type);
    }

    vote_count++;
    return !vote_yes;
}

static int do_phase2(i64 timeout)
{
    int result = 0;
    q2pc_msg* msg = get_messge(timeout);
    if(!msg){
        ch_log_error("Server has terminated. Cannot continue\n");
        term(0);
    }


    switch(msg->type){
    case q2pc_commit_msg:
        ch_log_debug2("Q2PC Client: [M]<-- commit\n");
        send_response(q2pc_ack_msg);
        ch_log_debug2("Q2PC Client: [M]--> ack\n");
        result = 0;
        break;
    case q2pc_cancel_msg:
        ch_log_debug2("Q2PC Client: [M]<-- cancel\n");
        send_response(q2pc_ack_msg);
        ch_log_debug2("Q2PC Client: [M]--> ack\n");
        result = 1;
        break;
    default:
        ch_log_fatal("Protocol failure, in phase 2 unexpected message type %i\n", msg->type);
    }


    return result;
}


void run_client(const transport_s* transport, i64 client_id, i64 wait_time)
{
    client_num = client_id;
    init(transport);
    vote_count = client_id; //XXX HACK

    while(1){
        do_phase1(-1);
        if(do_phase2(wait_time)){
            ch_log_debug1("Commit aborted\n");
        }
        else{
            ch_log_debug1("Commit succeed\n");
        }
    }

}
