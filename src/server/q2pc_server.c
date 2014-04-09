/*
 * q2pc_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#include "q2pc_server.h"
#include "../transport/q2pc_transport.h"

void run_server(const i64 client_count , const transport_s* transport)
{

    q2pc_trans_server* trans = server_factory(transport,client_count);
    (void) trans;


    return;
}
