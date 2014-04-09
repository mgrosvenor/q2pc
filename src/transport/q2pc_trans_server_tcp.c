/*
 * q2pc_trans_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#include "q2pc_trans_server_tcp.h"

typedef struct {
    int fd;
} q2pc_server_tcp_conn_priv;

typedef struct {
    int fd;
} q2pc_server_tcp_server_priv;


q2pc_trans_server* q2pc_sever_tcp_construct(i64 client_count)
{
    (void)client_count;
    return NULL;
}
