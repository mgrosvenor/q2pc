/*
 * q2pc_server.h
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_SERVER_H_
#define Q2PC_SERVER_H_

#include "../../deps/chaste/chaste.h"
#include "../transport/q2pc_transport.h"

void run_server(const i64 client_count , const transport_s* transport);

#endif /* Q2PC_SERVER_H_ */
