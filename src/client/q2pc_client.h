/*
 * q2pc_client.h
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_CLIENT_H_
#define Q2PC_CLIENT_H_

#include "../../deps/chaste/chaste.h"
#include "../transport/q2pc_transport.h"

void run_client(const char* ip_str,const transport_s* transport);

#endif /* Q2PC_CLIENT_H_ */
