/*
 * q2pc_server_worker.h
 *
 *  Created on: Apr 17, 2014
 *      Author: mgrosvenor
 */

#ifndef Q2PC_SERVER_WORKER_H_
#define Q2PC_SERVER_WORKER_H_


typedef struct{
    i64 lo;
    i64 hi;
    i64 count;
    i64 thread_id;
} thread_params_t;

void* run_thread( void* p);


#endif /* Q2PC_SERVER_WORKER_H_ */
