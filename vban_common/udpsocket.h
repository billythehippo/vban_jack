#ifndef UDPSOCKET_H_
#define UDPSOCKET_H_

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void UDP_deinit(int socdesc)
{
    if(socdesc)
    {
        shutdown(socdesc, SHUT_RDWR);
        //close(socdesc);
    }
}

int UDP_init(int* socdesc, sockaddr_in* si, const char* __restrict IP, uint16_t port, char mode, int broadcast, int priority);
int UDP_send_datagram(u_int32_t ip, uint16_t port, uint8_t* buf, uint32_t buf_len, int priority);
int UDP_send(int socdesc, sockaddr_in* si, uint8_t* buf, uint32_t buf_len);
int UDP_answer(int socdesc, sockaddr_in* si, uint8_t* buf, uint32_t buf_len);
int UDP_recv(int socdesc, sockaddr_in* si, uint8_t* buf, uint32_t buf_len);
void disp_recv_addr_info(sockaddr_in* si);

#endif
