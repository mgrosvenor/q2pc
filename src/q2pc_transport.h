/*
 * q2pc_transport.h
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_TRANSPORT_H_
#define Q2PC_TRANSPORT_H_

#include "../deps/chaste/chaste.h"


typedef enum { udp_ln = 0, tcp_ln, udp_nm, rdp_nm, udp_qj } transport_e;

typedef struct {
    transport_e type;
    i64 qjump_epoch;
    i64 qjump_limit;
} transport_s;


#endif /* Q2PC_TRANSPORT_H_ */
