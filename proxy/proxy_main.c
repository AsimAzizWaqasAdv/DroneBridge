/*
 *   Created by Wolfgang Christl
 *   This file is part of DroneBridge
 *   https://github.com/seeul8er/DroneBridge
 *   This is the DroneBridge Proxy module. It routes UDP <-> DroneBridge Control module and is used to send MSP/MAVLink
 *   messages
 *   This module might act as a reference design for future modules
 *   Link over DroneBridge Proxy module is fully transparent
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <zconf.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include "../common/wbc_lib.h"
#include "../common/db_get_ip.h"
#include "../common/db_protocol.h"
#include "../common/db_raw_receive.h"
#include "../common/db_raw_send_receive.h"

bool volatile keeprunning = true;
char if_name_proxy[IFNAMSIZ], if_name_telem[IFNAMSIZ];
char db_mode, enable_telemetry_module;
uint8_t comm_id = DEFAULT_V2_COMMID;
int c, app_port_proxy, app_port_telem, bitrate_op;

void intHandler(int dummy)
{
    keeprunning = false;
}

int process_command_line_args(int argc, char *argv[]){
    strncpy(if_name_proxy, DEFAULT_DB_IF, IFNAMSIZ);
    strncpy(if_name_telem, DEFAULT_DB_IF, IFNAMSIZ);
    db_mode = DEFAULT_DB_MODE;
    enable_telemetry_module = 'Y';
    app_port_proxy = APP_PORT_PROXY;
    app_port_telem = APP_PORT_TELEMETRY;
    opterr = 0;
    bitrate_op = DEFAULT_BITRATE_OPTION;
    while ((c = getopt (argc, argv, "n:m:c:p:b:t:i:l:")) != -1)
    {
        switch (c)
        {
            case 'n':
                strncpy(if_name_proxy, optarg, IFNAMSIZ);
                break;
            case 'i':
                strncpy(if_name_telem, optarg, IFNAMSIZ);
                break;
            case 't':
                enable_telemetry_module = *optarg;
                break;
            case 'm':
                db_mode = *optarg;
                break;
            case 'c':
                comm_id = (uint8_t) strtol(optarg, NULL, 10);
                break;
            case 'p':
                app_port_proxy = (int) strtol(optarg, NULL, 10);
                break;
            case 'l':
                app_port_telem = (int) strtol(optarg, NULL, 10);
                break;
            case 'b':
                bitrate_op = (int) strtol(optarg, NULL, 10);
                break;
            case '?':
                printf("DroneBridge Proxy module is used to do any UDP <-> DB_CONTROL_AIR routing. UDP IP given by "
                               "IP-checker module. Use"
                               "\n\t-n <network_IF_proxy_module> "
                               "\n\t-m [w|m] default is <m>"
                               "\n\t-t [Y|N] enable telemetry port/module"
                               "\n\t-i <network_IF_telemetry_module>"
                               "\n\t-l UDP port to send data to received over long range link on telemetry port. "
                               "IP comes from IP checker module."
                               "\n\t-p Specify a UDP port to which we send the data received over long range link. IP "
                               "comes from IP checker module. This port is also the local port for receiving UDP packets"
                               " to forward to DB_raw. Default port:%i"
                               "\n\t-c <communication id> Choose a number from 0-255. Same on groundstation and drone!"
                               "\n\t-b bitrate: \n\t1 = 2.5Mbit\n\t2 = 4.5Mbit\n\t3 = 6Mbit\n\t4 = 12Mbit (default)\n\t"
                               "5 = 18Mbit\n\t(bitrate option only supported with Ralink chipsets)"
                        , APP_PORT_PROXY);
                break;
            default:
                abort ();
        }
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);
    usleep((__useconds_t) 1e6);
    process_command_line_args(argc, argv);

    // set up long range receiving socket
    int lr_socket_proxy = open_socket_send_receive(if_name_proxy, comm_id, db_mode, bitrate_op, DB_DIREC_DRONE, DB_PORT_PROXY);
    int lr_socket_telem = open_receive_socket(if_name_telem, db_mode, comm_id, DB_DIREC_GROUND, DB_PORT_TELEMETRY);
    int udp_socket = socket (AF_INET, SOCK_DGRAM, 0);
    int fifo_osd = -1;
    if (enable_telemetry_module == 'Y'){
        fifo_osd = open("/root/telemetryfifo1", O_WRONLY);
        if (fifo_osd==-1){
            perror("DB_TEL_GROUND: Unable to open OSD FIFO\n");
            exit (EXIT_FAILURE);
        }
    }

    struct sockaddr_in client_telem_addr, client_proxy_addr, servAddr;
    // set up UDP socket remote address to forward proxy traffic (MSP) to
    client_proxy_addr.sin_family = AF_INET;
    client_proxy_addr.sin_addr.s_addr = inet_addr("192.168.2.2");
    client_proxy_addr.sin_port = htons(app_port_proxy);
    socklen_t len_client_addr = sizeof(client_proxy_addr);
    // set up UDP socket remote address to forward telemetry traffic (MAVLink, LTM) to
    client_telem_addr.sin_family = AF_INET;
    client_telem_addr.sin_addr.s_addr = inet_addr("192.168.2.2");
    client_telem_addr.sin_port = htons(app_port_telem);

    // local server port we bind to
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(app_port_proxy);
    const int y = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int));

    if (udp_socket < 0) {
        printf("DB_PROXY_GROUND: Unable to open socket (%s)\n", strerror(errno));
        exit (EXIT_FAILURE);
    }
    if (bind(udp_socket, (struct sockaddr *) &servAddr, sizeof (servAddr)) < 0) {
        printf("DB_PROXY_GROUND: Unable to bind to port %i (%s)\n", app_port_proxy, strerror(errno));
        exit (EXIT_FAILURE);
    }
    int broadcast=1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast,sizeof(broadcast))==-1) {
        printf("DB_PROXY_GROUND: Unable to set broadcast option %s\n",strerror(errno));
    }

    // init variables
    int radiotap_length = 0;
    int shID = init_shared_memory_ip();
    fd_set fd_socket_set;
    struct timeval select_timeout;
    struct data_uni *data_uni_to_drone = (struct data_uni *)
            (monitor_framebuffer + RADIOTAP_LENGTH + DB_RAW_V2_HEADER_LENGTH);
    uint8_t seq_num = 0;
    uint8_t lr_buffer[DATA_UNI_LENGTH];
    uint8_t udp_buffer[DATA_UNI_LENGTH-DB_RAW_V2_HEADER_LENGTH];
    size_t message_length = 0;

    printf("DB_PROXY_GROUND: started!\n");
    while(keeprunning) {
        select_timeout.tv_sec = 5;
        select_timeout.tv_usec = 0;
        FD_ZERO (&fd_socket_set);
        FD_SET(udp_socket, &fd_socket_set);
        FD_SET(lr_socket_proxy, &fd_socket_set);
        FD_SET(lr_socket_telem, &fd_socket_set);
        int select_return = select (FD_SETSIZE, &fd_socket_set, NULL, NULL, &select_timeout);
        if(select_return == 0){
            // timeout: get IP from IP checker although we always return messages to last knows client ip:port
            client_proxy_addr.sin_addr.s_addr = inet_addr(get_ip_from_ipchecker(shID));
        } else if (select_return > 0){
            if (FD_ISSET(udp_socket, &fd_socket_set)){
                // ---------------
                // incoming from UDP port 1607 - forward all to CONTROL_AIR via long range
                // Message app/UDP --> DB_CONTROL_AIR (e.g. MSP messages)
                // Proxy returns messages to last known client port
                // ---------------
                ssize_t l = recvfrom(udp_socket, udp_buffer, (DATA_UNI_LENGTH-DB_RAW_V2_HEADER_LENGTH), 0,
                                     (struct sockaddr *)&client_proxy_addr, &len_client_addr);
                int err = errno;
                if (l > 0){
                    memcpy(data_uni_to_drone->bytes, udp_buffer, (size_t) l);
                    send_packet_hp(DB_PORT_CONTROLLER, (u_int16_t) l, update_seq_num(&seq_num));
                } else {
                    printf("DB_PROXY_GROUND: UDP socket received an error: %s\n", strerror(err));
                }
            }
            if (FD_ISSET(lr_socket_proxy, &fd_socket_set)){
                // ---------------
                // incoming form long range proxy port
                // Message DB_CONTROL_AIR --> app/UDP (e.g. MSP messages)
                // ---------------
                ssize_t l = recv(lr_socket_proxy, lr_buffer, DATA_UNI_LENGTH, 0); int err = errno;
                if (l > 0){
                    radiotap_length = lr_buffer[2] | (lr_buffer[3] << 8);
                    message_length = lr_buffer[radiotap_length+7] | (lr_buffer[radiotap_length+8] << 8); // DB_v2
                    memcpy(udp_buffer, lr_buffer+(radiotap_length + DB_RAW_V2_HEADER_LENGTH), message_length);
                    sendto (udp_socket, udp_buffer, message_length, 0, (struct sockaddr *) &client_proxy_addr,
                            sizeof (client_proxy_addr));
                } else {
                    printf("DB_PROXY_GROUND: Long range socket received an error: %s\n", strerror(err));
                }
            }
            if (FD_ISSET(lr_socket_telem, &fd_socket_set)){
                // ---------------
                // incoming form long range telemetry port - write data to OSD-FIFO and pass on to app:1604
                // Message DB_CONTROL_AIR|DB_TELEMETRY_AIR --> app_udp:1604 (e.g. MAVLink or LTM messages)
                // ---------------
                ssize_t l = recv(lr_socket_telem, lr_buffer, DATA_UNI_LENGTH, 0); int err = errno;
                if (l > 0){
                    radiotap_length = lr_buffer[2] | (lr_buffer[3] << 8);
                    message_length = lr_buffer[radiotap_length+7] | (lr_buffer[radiotap_length+8] << 8); // DB_v2
                    memcpy(udp_buffer, lr_buffer+(radiotap_length + DB_RAW_V2_HEADER_LENGTH), message_length);
                    sendto (udp_socket, udp_buffer, message_length, 0, (struct sockaddr *) &client_telem_addr,
                            sizeof (client_telem_addr));
                    if (fifo_osd != -1){
                        ssize_t written = write(fifo_osd, udp_buffer, message_length);
                        if (written==-1){
                            perror("DB_TEL_GROUND: Could not write to OSD FIFO");
                        }
                    }
                } else {
                    printf("DB_TEL_GROUND: Long range socket received an error: %s\n", strerror(err));
                }
            }
        } else if (select_return == -1) {
            perror("DB_PROXY_GROUND: select() returned error: ");
        }
    }
    close(lr_socket_proxy);
    close(udp_socket);
    close(fifo_osd);
    return 0;
}