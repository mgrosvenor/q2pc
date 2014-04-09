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

typedef struct {
    int fd;
} q2pc_server_tcp_conn_priv;

typedef struct {
    int fd;
} q2pc_server_tcp_server_priv;


static int doconnect(struct q2pc_trans_server_s* this, i64 client_count, q2pc_trans_conn* con)
{
    (void) this;
    (void) con;
    (void) client_count;
    return 0;
}

static void delete(struct q2pc_trans_server_s* this)
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

    int flags = 0;
    flags &= ~O_NONBLOCK;
    if( fcntl(priv->fd, F_SETFL, flags) == -1){
        ch_log_fatal("Could not non-blocking: %s\n",strerror(errno));
    }

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

   result->priv    = priv;
   result->connect = doconnect;
   result->delete  = delete;

   init(priv, client_count, transport);


   return result;
}
