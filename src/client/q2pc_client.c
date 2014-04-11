/*
 * q2pc_client.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */
#include <signal.h>
#include <stdlib.h>

#include "q2pc_client.h"
#include "../../deps/chaste/chaste.h"
#include "../transport/q2pc_transport.h"
#include "../errors/errors.h"
#include "../protocol/q2pc_protocol.h"

//Local globals
static q2pc_trans* trans    = NULL;
static q2pc_trans_conn conn = {0};


static void term(int signo)
{
    ch_log_info("Terminating...\n");
    (void)signo;

    if(trans){ trans->delete(trans); }
    if(conn.priv) { conn.delete(&conn); }

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
    trans->connect(trans,&conn);
    ch_log_debug1("Connecting to server...Done.\n");
}

typedef enum { q2pc_pahse1, q2pc_phase2 } q2pc_phase_t;


static q2pc_msg* get_messge()
{
    char* data = NULL;
    i64 len = 0;
    i64 result = Q2PC_EAGAIN;

    while(result != Q2PC_ENONE){
        i64 result = conn.beg_read(&conn,&data, &len);
        if(result != Q2PC_ENONE){
            conn.end_read(&conn);
            continue;
        }

        q2pc_msg* msg = (q2pc_msg*)data;
        conn.end_read(&conn);
        return msg;
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
    msg->src_hostid = ~0LL;

    //Commit it
    conn.end_write(&conn, sizeof(q2pc_msg));

}


u64 vote_count = 0;
static int do_phase1()
{
    q2pc_msg* msg = get_messge();

    //XXX HACK: 1 in 5 votes will fail
    u64 vote_yes = (vote_count % 5);


    switch(msg->type){
        case q2pc_request_msg:


            if(vote_yes){
                send_response(q2pc_vote_yes_msg);
                break;
            }
            else{
                send_response(q2pc_vote_no_msg);
                break;
            }
        default:
            ch_log_fatal("Protocol failure, unexpected message type\n");
    }

    vote_count++;
    return !vote_yes;
}

static int do_phase2()
{
    int result = 0;
    q2pc_msg* msg = get_messge();
    switch(msg->type){
        case q2pc_commit_msg:
            ch_log_info("Message committed\n");
            send_response(q2pc_ack_msg);
            result = 0;
            break;
        case q2pc_cancel_msg: ; break;
            ch_log_info("Message canceled\n");
            send_response(q2pc_ack_msg);
            result = 1;
            break;
        default:
            ch_log_fatal("Protocol failure, unexpected message type\n");
    }

    return result;
}


void run_client(const transport_s* transport)
{
    init(transport);


    while(1){
        if(do_phase1()){
            continue;
        }

        do_phase2();
    }

}
