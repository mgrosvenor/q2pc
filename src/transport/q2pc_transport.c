/*
 * q2pc_transport.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */


#include "q2pc_transport.h"
#include "q2pc_trans_server_tcp.h"


q2pc_trans_server* server_factory(const transport_s* transport, i64 client_count)
{
    switch(transport->type){
        case tcp_ln: return q2pc_sever_tcp_construct(transport, client_count);
        default: ch_log_fatal("Not implemented\n");
    }

    return NULL;
}

