/*
 * q2pc_transport.h
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_TRANSPORT_H_
#define Q2PC_TRANSPORT_H_

#include "../../deps/chaste/chaste.h"


typedef enum { udp_ln = 0, tcp_ln, udp_nm, rdp_nm, udp_qj } transport_e;

typedef struct {
    transport_e type;
    i64 qjump_epoch;
    i64 qjump_limit;
    u16 port;
} transport_s;


typedef struct q2pc_trans_client_s{
    void (*connect)(struct q2pc_trans_client_s* this, const char* address);
    int (*read)(struct q2pc_trans_client_s* this, char** data, i64* len);
    int (*write)(struct q2pc_trans_client_s* this, char** data, i64* len);

    void (*delete)(struct q2pc_trans_client_s* this);
    void* priv;
} q2pc_trans_client;


q2pc_trans_client* client_factory(const transport_s* transport);

typedef struct q2pc_trans_conn_s {
    int (*read)(struct q2pc_trans_conn_s* this, char** data, i64* len);
    int (*write)(struct q2pc_trans_conn_s* this, char** data, i64* len);
    void (*delete)(struct q2pc_trans_conn_s* this);

    void* priv;
} q2pc_trans_conn;

typedef struct q2pc_trans_server_s {
    int (*connect)(struct q2pc_trans_server_s* this, i64 client_count, q2pc_trans_conn* con);
    void (*delete)(struct q2pc_trans_server_s* this);

    void* priv;
} q2pc_trans_server;


q2pc_trans_server* server_factory(const transport_s* transport, i64 client_count);

#endif /* Q2PC_TRANSPORT_H_ */
