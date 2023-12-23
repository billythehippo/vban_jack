#include "udpsocket.h"


int UDP_init(int* socdesc, sockaddr_in* si, const char* __restrict IP, uint16_t port, char mode, int broadcast, int priority)
{
    int bcast = 0;
    int reuse = 1;
    int prio  = 6; // 1- low priority, 7 - high priority
    sockaddr_in serv_addr;
    static int sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(sd<0)
    {
        perror("Invalid socket descriptor!\n");
        return -1;
    }
    fprintf(stderr, "Socket creating, %d\n", sd);

    if (*socdesc==sd)
    {
        if(setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse))<0)
        {
            perror("Can't reuse address or port!\n");
            return -1;
        }
    }
    else *socdesc = sd;

    if (broadcast==1) bcast = 1;
    if(setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast))<0)
    {
        perror("Can't allow broadcast mode!\n");
        return -1;
    }
    else fprintf(stderr, "Broadcast mode successfully set!\n");

    if ((priority>0)&&(priority<8)) prio = priority;
    if(setsockopt(sd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
    {
        perror("Can't set socket priority\n");
        return -1;
    }
    else fprintf(stderr, "UDP priority is set to %d!\n", prio);

    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(si, 0, sizeof(*si));

    //server parameters
    serv_addr.sin_family=AF_INET;	//IPv4
    //serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);	//local area networks only (by destination)
    serv_addr.sin_addr.s_addr=inet_addr(IP);
    serv_addr.sin_port=htons(port);

    if(mode=='s') // bind, like server
    {
        if(bind(sd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr))<0)
        {
            perror("Error in func bind()");
            return -1;
        }
    }
    else *si=serv_addr; // no bind, like client

    return *socdesc;
}


int UDP_send_datagram(u_int32_t ip, uint16_t port, uint8_t* buf, uint32_t buf_len, int priority)
{
    int prio = 3;
    sockaddr_in saddr;
    static int sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sd<0)
    {
        perror("Invalid socket descriptor!\n");
        return -1;
    }
    if ((priority>0)&&(priority<7)) prio = priority;
    if(setsockopt(sd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
    {
        perror("Can't set socket priority\n");
        return -1;
    }
    saddr.sin_family=AF_INET;
    saddr.sin_addr.s_addr=ip;
    saddr.sin_port=port;
    socklen_t sisize=sizeof(saddr);
    int ret = sendto(sd, buf, buf_len, 0, (const struct sockaddr*)&saddr, sisize);
    return ret;
}


int UDP_send(int socdesc, sockaddr_in* si, uint8_t* buf, uint32_t buf_len)
{
    socklen_t sisize=sizeof(*si);
    if(socdesc) return sendto(socdesc, buf, buf_len, 0, (const struct sockaddr*)si, sisize);
    else return -1;
}


int UDP_answer(int socdesc, sockaddr_in* si, uint8_t* buf, uint32_t buf_len)
{
    socklen_t sisize=sizeof(*si);
    if(socdesc) return sendto(socdesc, buf, buf_len, MSG_CONFIRM, (const struct sockaddr*)si, sisize);
    else return -1;
}


int UDP_recv(int socdesc, sockaddr_in* si,  uint8_t* buf, uint32_t buf_len)
{
    socklen_t sisize=sizeof(*si);
    if(socdesc) return recvfrom(socdesc, buf, buf_len, MSG_WAITALL, (struct sockaddr*)si, &sisize); //MSG_WAITALL MSG_DONTWAIT
    else return -1;
}


void disp_recv_addr_info(sockaddr_in* si)
{
    printf("Client_addr_info:\n");
    printf("IP: %s\n", inet_ntoa(si->sin_addr));
    printf("Addr struct size: %lu\n", sizeof(si));
}
