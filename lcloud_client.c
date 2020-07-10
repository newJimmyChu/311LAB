////////////////////////////////////////////////////////////////////////////////
//
//  File          : lcloud_client.c
//  Description   : This is the client side of the Lion Clound network
//                  communication protocol.
//
//  Author        : Patrick McDaniel
//  Last Modified : Sat 28 Mar 2020 09:43:05 AM EDT
//

// Include Files
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

// Project Include Files
#include <lcloud_network.h>
#include <cmpsc311_log.h>
//#include <psdk_inc/_ip_types.h>
#include <cmpsc311_util.h>

#define READ 1
#define WRITE 2
#define PROBE 3
#define POWER_OFF 4
#define NET_REG_SIZE 8
#define NET_BUFFER_SIZE 1024

#define REGISTER_MASK_B0 (uint64_t)0xf000000000000000
#define REGISTER_MASK_B1 (uint64_t)0x0f00000000000000
#define REGISTER_MASK_C0 (uint64_t)0x00ff000000000000
#define REGISTER_MASK_C1 (uint64_t)0x0000ff0000000000
#define REGISTER_MASK_C2 (uint64_t)0x000000ff00000000
#define REGISTER_MASK_D0 (uint64_t)0x00000000ffff0000
#define REGISTER_MASK_D1 (uint64_t)0x000000000000ffff

#define LCFHANDLE_MASK_ID 	 (uint32_t)0xff000000
#define LCFHANDLE_MASK_HANDLE 	 (uint32_t)0x00ffffff

#define SHIFT_BITS_B0 60
#define SHIFT_BITS_B1 56
#define SHIFT_BITS_C0 48
#define SHIFT_BITS_C1 40
#define SHIFT_BITS_C2 32
#define SHIFT_BITS_D0 16
#define SHIFT_BITS_D1 0

// Global variables
int socket_fd = -1;
//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_lcloud_bus_request
// Description  : This the client regstateeration that sends a request to the 
//                lion client server.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed

LCloudRegisterFrame client_lcloud_bus_request( LCloudRegisterFrame reg, void *buf ) {

    struct sockaddr_in server;
    uint32_t opcode = 0;
    uint64_t net_reg = 0;
    uint64_t response_reg = 0;
    char send_buffer[NET_BUFFER_SIZE];
    char receive_buffer[NET_BUFFER_SIZE];
    int read_size = 0;
    // Initialize send buffer
    memset(send_buffer, 0, sizeof(send_buffer));

    // If there is no connection between client and server
    if (socket_fd < 0) {
        // Initialize the socket address and client address
        memset(&server, 0, sizeof(server));
        // Initialize server address (struct)
        server.sin_family = AF_INET;
        server.sin_port = htons(LCLOUD_DEFAULT_PORT);
        server.sin_addr.s_addr = inet_addr(LCLOUD_DEFAULT_IP);
        //int g = inet_aton(LCLOUD_DEFAULT_IP , &server.sin_addr);
        
        // Initialize socket
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            return (-1);
        }
        // Set up connection with the server
        
        int a = connect(socket_fd, (struct sockaddr *)&server, sizeof(server));
        if (a < 0) {
            return (-1);
        }

    }

    // Extract opcode from LCloudRegisterFrame reg
    uint64_t operation = (reg & REGISTER_MASK_C0) >> SHIFT_BITS_C0;
    opcode = 0;
    if (operation == 0 || operation == 1 || operation == 2 || operation == 5) {
        opcode = PROBE;
    } else if (operation == 3) {
        if (((reg & REGISTER_MASK_C2) >> SHIFT_BITS_C2) == LC_XFER_READ) {
            opcode = READ;
        } else {
            opcode = WRITE;
        }
    } else if (operation == 4) {
        opcode = POWER_OFF;
    }
    
    

    net_reg = htonll64(reg);
    switch(opcode) {
        // Read operation: sending reg to server and receive buffer from the server
        case READ:
            // Send the reg to server
            memcpy(send_buffer, (char *)&net_reg, NET_REG_SIZE);
            write(socket_fd, send_buffer, NET_REG_SIZE);

            // Receive reg and 1024 bytes of data from server
            read_size = read(socket_fd, receive_buffer, 264);
            memcpy(&response_reg, &receive_buffer[0], NET_REG_SIZE);
            memcpy(buf, &receive_buffer[NET_REG_SIZE], 256);
            response_reg = ntohll64(response_reg);
            break;

        case WRITE:
            // Send the reg following with data to server
            memcpy(send_buffer, (char *)&net_reg, NET_REG_SIZE);
            write(socket_fd, send_buffer, NET_REG_SIZE);
            memcpy(&send_buffer[NET_REG_SIZE], buf, 256);
            write(socket_fd, send_buffer, 256);

            // Receiving only the reg from server
            read_size = read(socket_fd, receive_buffer, 8);
            memcpy(&response_reg, &receive_buffer[0], NET_REG_SIZE);
            response_reg = ntohll64(response_reg);
            break;

        case PROBE:
            // Send only the reg to server
            memcpy(send_buffer, (char *)&net_reg, NET_REG_SIZE);
            write(socket_fd, send_buffer, NET_REG_SIZE);


            // Receive only the reg from server
            read_size = read(socket_fd, receive_buffer, NET_REG_SIZE);
            memcpy(&response_reg, &receive_buffer[0], NET_REG_SIZE);
            response_reg = ntohll64(response_reg);
            break;

        case POWER_OFF:
            // Send only the reg to server
            memcpy(send_buffer, (char *)&net_reg, NET_REG_SIZE);
            write(socket_fd, send_buffer, NET_REG_SIZE);

            // Receive only the reg from server
            read_size = read(socket_fd, receive_buffer, NET_BUFFER_SIZE);
            memcpy(&response_reg, &receive_buffer[0], NET_REG_SIZE);
            response_reg = ntohll64(response_reg);
            close(socket_fd);
            socket_fd = -1;
            break;
    }

    return (response_reg);

}

