/*
 * q2pc_trans_rudp.c
 *
 *  Created on: Apr 12, 2014
 *      Author: mgrosvenor
 */


/*
 * q2pc_trans_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "q2pc_trans_rudp.h"
#include "q2pc_trans_udp.h"
#include "conn_vector.h"
#include "../errors/errors.h"
#include "../protocol/q2pc_protocol.h"

typedef struct {
    q2pc_trans_conn base;
    bool is_server;
} q2pc_rudp_conn_priv;



static int conn_beg_read(struct q2pc_trans_conn_s* this, char** data_o, i64* len_o)
{
    q2pc_rudp_conn_priv* priv = (q2pc_rudp_conn_priv*)this->priv;
    return priv->base.beg_read(&priv->base,data_o, len_o);
}

static int conn_end_read(struct q2pc_trans_conn_s* this)
{
    q2pc_rudp_conn_priv* priv = (q2pc_rudp_conn_priv*)this->priv;
    return priv->base.end_read(&priv->base);
}


static int conn_beg_write(struct q2pc_trans_conn_s* this, char** data_o, i64* len_o)
{
    q2pc_rudp_conn_priv* priv = (q2pc_rudp_conn_priv*)this->priv;
    return priv->base.beg_write(&priv->base, data_o, len_o);
}


static int conn_end_write(struct q2pc_trans_conn_s* this, i64 len)
{
    q2pc_rudp_conn_priv* priv = (q2pc_rudp_conn_priv*)this->priv;
    return priv->base.end_write(&priv->base, len);
}


static void conn_delete(struct q2pc_trans_conn_s* this)
{
    if(this){
        if(this->priv){
            q2pc_rudp_conn_priv* priv = (q2pc_rudp_conn_priv*)this->priv;
            priv->base.delete(&priv->base);
            free(this->priv);
        }

        //XXX HACK!
        //free(this);
    }
}



/***************************************************************************************************************************/

typedef struct {

    transport_s transport;
    q2pc_trans* base;
} q2pc_rudp_priv;



#define BUFF_SIZE (4096 * 1024) //A 4MB buffer. Just because it feels right
static q2pc_rudp_conn_priv* init_new_conn(q2pc_trans_conn* conn)
{
    q2pc_rudp_conn_priv* new_priv = calloc(1,sizeof(q2pc_rudp_conn_priv));
    if(!new_priv){
        ch_log_fatal("Malloc failed!\n");
    }

    conn->priv      = new_priv;
    conn->beg_read  = conn_beg_read;
    conn->end_read  = conn_end_read;
    conn->beg_write = conn_beg_write;
    conn->end_write = conn_end_write;
    conn->delete    = conn_delete;

    return new_priv;
}



//Wait for all clients to connect
static int doconnect(struct q2pc_trans_s* this, q2pc_trans_conn* conn)
{
    q2pc_rudp_priv* trans_priv = (q2pc_rudp_priv*)this->priv;
    q2pc_rudp_conn_priv* conn_priv = (q2pc_rudp_conn_priv*)conn->priv;

    if(!conn_priv){

        q2pc_rudp_conn_priv* new_priv = init_new_conn(conn);
        if(trans_priv->base->connect(trans_priv->base,&conn_priv->base)){
            ch_log_fatal("Could not create UDP base for RUDP\n");
        }
        conn_priv = new_priv;

        conn_priv->is_server = trans_priv->transport.server;

    }

    return Q2PC_ENONE;
}


static void serv_delete(struct q2pc_trans_s* this)
{
    if(this){

        if(this->priv){
            q2pc_rudp_priv* priv = (q2pc_rudp_priv*)this->priv;
            priv->base->delete(priv->base);
            free(this->priv);
        }

        free(this);
    }

}


static void init(q2pc_rudp_priv* priv)
{

    ch_log_debug1("Constructing RUDP transport\n");
    priv->base = q2pc_udp_construct(&priv->transport);
    ch_log_debug1("Done constructing RUDP transport\n");

}


q2pc_trans* q2pc_rudp_construct(const transport_s* transport)
{
    q2pc_trans* result = (q2pc_trans*)calloc(1,sizeof(q2pc_trans));
    if(!result){
        ch_log_fatal("Could not allocate RUDP server structure\n");
    }

    q2pc_rudp_priv* priv = (q2pc_rudp_priv*)calloc(1,sizeof(q2pc_rudp_priv));
    if(!priv){
        ch_log_fatal("Could not allocate RUDP server private structure\n");
    }

    result->priv          = priv;
    result->connect       = doconnect;
    result->delete        = serv_delete;
    memcpy(&priv->transport,transport, sizeof(transport_s));
    init(priv);


    return result;
}
