/*
 * q2pc_protocol.h
 *
 *  Created on: Apr 10, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_PROTOCOL_H_
#define Q2PC_PROTOCOL_H_

#include "../../deps/chaste/chaste.h"


typedef struct __attribute__((__packed__)) {
    i32 src_hostid;
    i64 vote;
} q2pc_msg;

#endif /* Q2PC_PROTOCOL_H_ */
