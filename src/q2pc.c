#include <stdio.h>
#include "../deps/chaste/chaste.h"
#include "../deps/chaste/options/options.h"

#include "server/q2pc_server.h"
#include "client/q2pc_client.h"
#include "transport/q2pc_transport.h"

USE_CH_LOGGER(CH_LOG_LVL_INFO,true,ch_log_tostderr,NULL);
USE_CH_OPTIONS;

static struct {
	//General Options
	i64 server;
	char* client;

	//Transports
	bool trans_tcp_ln;
	bool trans_udp_ln;
	bool trans_udp_nm;
	bool trans_rdp_nm;
	bool trans_udp_qj;

	//Qjump transport options
	i64 qjump_epoch;
	i64 qjump_psize;

	//Logging options
	bool log_no_colour;
	bool log_stdout;
	bool log_stderr;
	bool log_syslog;
	char* log_filename;
	i64 log_verbosity;

} options;


int main(int argc, char** argv)
{
	//General options
    ch_opt_addii(CH_OPTION_OPTIONAL,'s',"server","Put q2pc in server mode, specify the number of clients", &options.server, 0);
    ch_opt_addsi(CH_OPTION_OPTIONAL,'c',"client","Put q2pc in client mode, specify server address in x.x.x.x format", &options.client, NULL);

    //Transports
    ch_opt_addbi(CH_OPTION_FLAG,    'u',"udp-ln","Use Linux based UDP transport [default]", &options.trans_udp_ln, false);
    ch_opt_addbi(CH_OPTION_FLAG,    't',"tcp-ln","Use Linux based TCP transport", &options.trans_tcp_ln, false);
    ch_opt_addbi(CH_OPTION_FLAG,    'U',"udp-nm","Use NetMap based UDP transport", &options.trans_udp_nm, false);
    ch_opt_addbi(CH_OPTION_FLAG,    'R',"rdp-nm","Use NetMap based RDP transport", &options.trans_rdp_nm, false);
    ch_opt_addbi(CH_OPTION_FLAG,    'Q',"udp-qj","Use NetMap based UDP transport over Q-Jump", &options.trans_udp_qj, false);

    //Qjump Transport options
    ch_opt_addii(CH_OPTION_OPTIONAL, 'E',"qjump-epoch", "The Q-Jump epoch in microseconds", &options.qjump_epoch, 100 );
    ch_opt_addii(CH_OPTION_OPTIONAL, 'P',"qjump-psize", "The Q-Jump packet size in bytes", &options.qjump_psize, 64 );

    //Q2PC Logging
    ch_opt_addbi(CH_OPTION_FLAG,     'n', "no-colour",  "Turn off colour log output",     &options.log_no_colour, false);
    ch_opt_addbi(CH_OPTION_FLAG,     '0', "log-stdout", "Log to standard out", 	 	&options.log_stdout, 	false);
    ch_opt_addbi(CH_OPTION_FLAG,     '1', "log-stderr", "Log to standard error [default]", 	&options.log_stderr, 	false);
    ch_opt_addbi(CH_OPTION_FLAG,     'S', "log-syslog", "Log to system log",         &options.log_syslog,	false);
    ch_opt_addsi(CH_OPTION_OPTIONAL, 'F', "log-file",   "Log to the file supplied",	&options.log_filename, 	NULL);
    ch_opt_addii(CH_OPTION_OPTIONAL, 'v', "log-level",  "Log level verbosity (0 = lowest, 6 = highest)",  &options.log_verbosity, CH_LOG_LVL_INFO);

    //Parse it all up
    ch_opt_parse(argc,argv);


    //Configure logging
    ch_log_settings.filename    = options.log_filename;
    ch_log_settings.use_color   = !options.log_no_colour;
    ch_log_settings.log_level   = MAX(0, MIN(options.log_verbosity, CH_LOG_LVL_DEBUG3)); //Put a bound on it

    i64 log_opt_count = 0;
    log_opt_count += options.log_stderr ?  1 : 0;
    log_opt_count += options.log_stdout ?  1 : 0;
    log_opt_count += options.log_syslog ?  1 : 0;
    log_opt_count += options.log_filename? 1 : 0;

    //Too many options selected
    if(log_opt_count > 1){
        ch_log_fatal("Q2PC: Can only log to one format at a time, you've selected the following [%s%s%s%s]\n ",
                options.log_stderr ? "std-err " : "",
                options.log_stdout ? "std-out " : "",
                options.log_syslog ? "system log " : "",
                options.log_filename ? "file out " : ""
        );
    }

    //No option selected, use the default
    if(log_opt_count == 0){
        options.log_stdout = true;
    }

    //Configure the options as desired
    if(options.log_stderr){
        ch_log_settings.output_mode = ch_log_tostderr;
    } else if(options.log_stdout){
        ch_log_settings.output_mode = ch_log_tostdout;
    } else if(options.log_syslog){
        ch_log_settings.output_mode = ch_log_tosyslog;
    } else if (options.log_filename){
        ch_log_settings.output_mode = ch_log_tofile;
    }

    i64 transport_opt_count = 0;
    transport_opt_count += options.trans_udp_ln ? 1 : 0;
    transport_opt_count += options.trans_tcp_ln ? 1 : 0;
    transport_opt_count += options.trans_udp_nm ? 1 : 0;
    transport_opt_count += options.trans_rdp_nm ? 1 : 0;
    transport_opt_count += options.trans_udp_qj ? 1 : 0;

    //Make sure only 1 choice has been made
    if(transport_opt_count > 1){
        ch_log_fatal("Q2PC: Can only use one transport at a time, you've selected the following [%s%s%s%s%s ]\n ",
                options.trans_udp_ln ? "udp-ln " : "",
                options.trans_tcp_ln ? "tcp-ln " : "",
                options.trans_udp_nm ? "udp-nm " : "",
                options.trans_rdp_nm ? "rdp-nm " : "",
                options.trans_udp_qj ? "udp-qj " : ""
        );
    }

    //Set up the default
    if(transport_opt_count == 0){
        options.trans_udp_ln = 1;
    }

    //Finally figure out he actual one that we want
    transport_s transport = {0};
    transport.type          = options.trans_udp_ln ? udp_ln : transport.type;
    transport.type          = options.trans_tcp_ln ? tcp_ln : transport.type;
    transport.type          = options.trans_udp_nm ? udp_nm : transport.type;
    transport.type          = options.trans_rdp_nm ? rdp_nm : transport.type;
    transport.type          = options.trans_udp_qj ? udp_qj : transport.type;
    transport.qjump_epoch   = options.qjump_epoch;
    transport.qjump_limit   = options.qjump_psize;


    //Configure application options
    if(options.client && options.server ){
        ch_log_fatal("Q2PC: Configuration error, must be in client or server mode, not both.\n");
    }
    if(!options.client && !options.server ){
        ch_log_fatal("Q2PC: Configuration error, in server mode, you must specify at least 1 client.\n");
    }

    if(options.client){
        run_client(options.client,&transport);
    }
    else{
        run_server(options.server,&transport);
    }

    return 0;
}

