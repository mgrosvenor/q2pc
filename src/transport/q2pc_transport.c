/*
 * q2pc_transport.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */


#include "q2pc_transport.h"
#include "q2pc_trans_tcp.h"


q2pc_trans* trans_factory(const transport_s* transport)
{
    switch(transport->type){
        case tcp_ln: return q2pc_tcp_construct(transport);
        default: ch_log_fatal("Not implemented\n");
    }

    return NULL;
}

