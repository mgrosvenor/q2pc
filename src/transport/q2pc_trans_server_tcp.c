/*
 * q2pc_trans_server.c
 *
 *  Created on: Apr 9, 2014
 *      Author: mgrosvenor
 */
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>

#include "q2pc_trans_server_tcp.h"
#include "conn_vector.h"
#include "../errors/errors.h"

typedef struct {
    int fd;
    void* read_buffer;
    i64   read_buffer_used;
    i64   read_buffer_size;

    //For the delimiter
    void* read_working_buffer;
    i64   read_working_buffer_used;
    i64   read_working_buffer_size;

    //
    void* write_buffer;
    i64   write_buffer_used;
    i64   write_buffer_size;
} q2pc_server_tcp_conn_priv;

typedef struct {
    int fd;
} q2pc_server_tcp_server_priv;

int delimit(char* buff, int len)
{
    (void)len;
    (void)buff;
    return 6;
}




static int conn_read(struct q2pc_trans_conn_s* this, char** data_o, i64* len_o)
{
    q2pc_server_tcp_conn_priv* priv = (q2pc_server_tcp_conn_priv*)this->priv;
    int result = read(priv->fd, priv->read_buffer, priv->read_buffer_size);
    if(result < 0){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return Q2PC_EAGAIN; //Reading would have blocked, we don't want this
        }

        ch_log_fatal("TCP read failed on fd=%i - %s\n",priv->fd,strerror(errno));
    }

    if(result == 0){
        return Q2PC_EFIN;
    }

    priv->read_buffer_used = result;

    *data_o = priv->read_buffer;
    *len_o  = priv->read_buffer_used;

    return Q2PC_ENONE;
}

static int delimitor(struct q2pc_trans_conn_s* this, char** data_o, i64* len_o)
{
    q2pc_server_tcp_conn_priv* priv = (q2pc_server_tcp_conn_priv*)this->priv;

    if(priv->read_working_buffer_used){
        int64_t delimit_size = delimit(priv->read_working_buffer, priv->read_working_buffer_used);
        if(delimit_size > 0){
            *data_o = priv->read_working_buffer;
            *len_o  = delimit_size;
            return Q2PC_ENONE; //Success!!
        }
    }

    char* data;
    i64 len;
    int result = conn_read(this, &data,&len);
    if(result != Q2PC_ENONE){
        return result;
    }

    ch_log_debug2("Read another %lubytes to %p\n", len, data);

    while(len > priv->read_working_buffer_size - priv->read_working_buffer_used){
        ch_log_debug2("Growing working buffer from %lu to %lu\n", priv->read_working_buffer_size, priv->read_working_buffer_size * 2 );
        priv->read_working_buffer_size *= 2;
        priv->read_working_buffer = realloc(priv->read_working_buffer, priv->read_working_buffer_size);
    }

    ch_log_debug2("Adding %lu bytes at %p (offset=%lu)\n",
            len,
            (char*)priv->read_working_buffer + priv->read_working_buffer_used,
            priv->read_working_buffer_used
    );

    memcpy((char*)priv->read_working_buffer + priv->read_working_buffer_used, data, len);
    priv->read_working_buffer_used += len;

    int64_t delimit_size = delimit(priv->read_working_buffer, priv->read_working_buffer_used);
    if(delimit_size > 0 && delimit_size <= priv->read_working_buffer_used){
        *data_o = priv->read_working_buffer;
        *len_o  = delimit_size;
        ch_log_debug2("Found packet size=%lu\n", *len_o);
        return Q2PC_ENONE;
    }

    //This is naughty, it says there isn't any data here, try again later, which is kind of true.
    return Q2PC_EAGAIN;
}



static int conn_write(struct q2pc_trans_conn_s* this, char** data_o, i64* len_o)
{
    (void)this;
    (void)data_o;
    (void)len_o;
    return 0;
}

static void conn_delete(struct q2pc_trans_conn_s* this)
{
    (void)this;
}




//Wait for all clients to connect
static CH_VECTOR(TRANS_CONN)* doconnectall(struct q2pc_trans_server_s* this, i64 client_count )
                {
    q2pc_server_tcp_server_priv* priv = (q2pc_server_tcp_server_priv*)this->priv;

    CH_VECTOR(TRANS_CONN)* result = CH_VECTOR_NEW(TRANS_CONN,client_count,NULL);

    for(int i = 0; i < client_count; i++){
        int accept_fd = accept(priv->fd, NULL, NULL);
        if( accept_fd < 0 ){
            //            if(errno == EAGAIN || errno == EWOULDBLOCK){
            //                continue; //Reading would have blocked, we don't want this
            //            }

            ch_log_fatal("TCP accept failed - %s\n",strerror(errno));
        }

        q2pc_server_tcp_conn_priv* new_priv = calloc(1,sizeof(q2pc_server_tcp_conn_priv));
        if(!new_priv){
            ch_log_fatal("Malloc failed!\n");
        }

#define BUFF_SIZE (4096 * 1021) //A 4MB buffer. Just because
        void* new_buffs = calloc(1,2 * BUFF_SIZE);
        if(!new_buffs){
            ch_log_fatal("Malloc failed!\n");
        }

        new_priv->read_buffer = new_buffs;
        new_priv->write_buffer = (char*)new_buffs + BUFF_SIZE;
        new_priv->read_buffer_size = BUFF_SIZE;
        new_priv->write_buffer_size = BUFF_SIZE;

        new_priv->fd = accept_fd;
        int flags = 0;
        flags &= ~O_NONBLOCK;
        if( fcntl(new_priv->fd, F_SETFL, flags) == -1){
            ch_log_fatal("Could not set non-blocking on fd=%i: %s\n",new_priv,strerror(errno));
        }

        result->off(result,i)->priv     = new_priv;
        result->off(result,i)->read     = delimitor;
        result->off(result,i)->write    = conn_write;
        result->off(result,i)->delete   = conn_delete;

    }


    return result;
                }

static void serv_delete(struct q2pc_trans_server_s* this)
{
    if(this){
        if(this->priv){
            free(this->priv);
        }

        free(this);
    }

}



static void init(q2pc_server_tcp_server_priv* priv, i64 client_count, const transport_s* transport)
{

    ch_log_debug1("Constructing for %li clients\n", client_count);
    for(int i = 0; i < client_count; i++){

    }

    priv->fd = socket(AF_INET,SOCK_STREAM,0);
    if (priv->fd < 0 ){
        ch_log_fatal("Could not create TCP socket (%s)\n", strerror(errno));
    }


    int reuse_opt = 1;
    if(setsockopt(priv->fd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(int)) < 0) {
        ch_log_fatal("TCP set option failed: %s\n",strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; //Listen to any port
    addr.sin_port        = htons(transport->port);

    if(bind(priv->fd, (struct sockaddr *)&addr, sizeof(addr)) ){
        uint64_t i = 0;

        //Will wait up to two minutes trying if the address is in use.
        //Helpful for quick restarts of apps as linux keeps some state
        //arround for a while.
        const int64_t seconds_per_try = 5;
        const int64_t seconds_total = 120;
        for(i = 0; i < seconds_total / seconds_per_try && errno == EADDRINUSE; i++){
            ch_log_debug1("%i] %s --> sleeping for %i seconds...\n",i, strerror(errno), seconds_per_try);
            sleep(seconds_per_try);
            bind(priv->fd, (struct sockaddr *)&addr, sizeof(addr));
        }

        if(errno){
            ch_log_fatal("TCP server bind failed: %s\n",strerror(errno));
        }
        else{
            ch_log_debug1("Successfully bound after delay.\n");
        }
    }


    int result = listen(priv->fd, 0);
    if(unlikely( result < 0 )){
        ch_log_fatal("TCP server listen failed: %s\n",strerror(errno));
    }

    //    int flags = 0;
    //    flags &= ~O_NONBLOCK;
    //    if( fcntl(priv->fd, F_SETFL, flags) == -1){
    //        ch_log_fatal("Could not non-blocking: %s\n",strerror(errno));
    //    }

    ch_log_debug1("Done constructing for %li clients\n", client_count);

}


q2pc_trans_server* q2pc_sever_tcp_construct(const transport_s* transport, i64 client_count)
{
    q2pc_trans_server* result = (q2pc_trans_server*)calloc(1,sizeof(q2pc_trans_server));
    if(!result){
        ch_log_fatal("Could not allocate TCP server structure\n");
    }

    q2pc_server_tcp_server_priv* priv = (q2pc_server_tcp_server_priv*)calloc(1,sizeof(q2pc_server_tcp_server_priv));
    if(!priv){
        ch_log_fatal("Could not allocate TCP server private structure\n");
    }

    result->priv         = priv;
    result->connectall   = doconnectall;
    result->delete       = serv_delete;

    init(priv, client_count, transport);


    return result;
}
