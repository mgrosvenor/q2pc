/*
 * q2pc_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#include "q2pc_server.h"
#include "../transport/q2pc_transport.h"
#include "../errors/errors.h"

void run_server(const i64 client_count , const transport_s* transport)
{

    q2pc_trans_server* trans = server_factory(transport,client_count);
    CH_VECTOR(TRANS_CONN)* cons = trans->connectall(trans, client_count);
    while(1){
        for(int i = 0; i < client_count; i++){
            q2pc_trans_conn* con = cons->off(cons,i);
            char* data = NULL;
            i64 len = 0;
            i64 result = con->read(con,&data, &len);
            if(result != Q2PC_ENONE){
                continue;
            }

            ch_log_debug2("Got %lu bytes in read\n", len);

        }
    }
    trans->delete(trans);


    return;
}
