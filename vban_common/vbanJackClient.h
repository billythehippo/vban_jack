#ifndef VBANJACKCLIENT_H
#define VBANJACKCLIENT_H

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <time.h>

#include "vban.h"
#include "vban_functions.h"

extern pthread_mutex_t tx_read_thread_lock;
extern pthread_cond_t  tx_data_ready;
extern pthread_t sendPacketThread;

struct rxclient_t
{
    union
    {
        uint32_t ipAddr;
        uint8_t ipBytes[4];
    };
    VBanHeader header;
    uint16_t udpPort;
    uint16_t udpRxPort;
    uint16_t pktlen;
    uint audioBuffSize;
    uint sampleRate;
    uint8_t netquality;
    uint8_t bufEmptyCount;
    uint8_t flags;
#define FLUSH_SOCKET 0x01
#define CARD_LIKE 0x04
#define AUTOCONNECT 0x08
#define MODE_TIMER 0x80
    uint8_t map[VBAN_CHANNELS_MAX_NB];
    timespec ts;
    uint32_t samplecount;
    uint32_t sampleCountMax;
    float fraction;
    uint8_t badPackets;
    int16_t rxSamples;
    uint32_t netoverruns;
    char name[16];
    jack_client_t* client;
    jack_port_t* ports[VBAN_CHANNELS_MAX_NB];
    jack_ringbuffer_t* rxBuffer;
    char* sparts;
    float* rxbuf;
} __attribute__((packed));

struct txclient_t
{
    union
    {
        uint32_t ipAddr;
        uint8_t ipBytes[4];
    };
    VBanHeader header;
    uint16_t udpPort;
    uint sampleRate;
    uint audioBuffSize;
    uint8_t map[VBAN_CHANNELS_MAX_NB];
    uint16_t pktLen;
    uint16_t pktNum;
    timespec ts;
    float* txBuffer;
    jack_client_t* client;
    jack_port_t* ports[VBAN_CHANNELS_MAX_NB];
    jack_ringbuffer_t* txrBuffer;
    char* txPackets;
    uint txBufSize;
    uint txrBufSize;
    uint16_t maxSamplesPerPacket;
    uint8_t flags;
#define READY_READ 0x01
#define READY_SYNC 0x02
#define CARD_LIKE 0x04
#define AUTOCONNECT 0x08
#define MODE_TIMER 0x80
    uint32_t syncCnt;
    uint32_t syncPer;
    uint8_t redundancy;
    int32_t timerPeriod;
    char name[16];
    int* audio_sd;
    struct sockaddr_in* audio_si;
} __attribute__((packed));

struct config_t
{
    char streamname[16];
    char IPaddr[16];
    uint32_t IP;
    uint16_t port;
    uint16_t nbchannels;
    uint8_t VBANResolution;
    uint8_t netquality;
    uint8_t map[VBAN_CHANNELS_MAX_NB];
    uint8_t flags;
    int16_t rxSamples;
    timespec ts;
    uint8_t* IPbytes;
};

extern rxclient_t* rxc;

int createTxClient(txclient_t* client, const char* __restrict name, uint32_t IP, uint16_t port, uint8_t VBANResolution, uint nbchannels, int* audio_sd, struct sockaddr_in* audio_si);
int createRxClient(rxclient_t* client, const char* __restrict name, uint32_t IP, VBanHeader header, uint16_t pktlen);
void deleteTxClient(txclient_t* client);
void deleteRxClient(rxclient_t* client);
uint8_t createTxBuffers(txclient_t* client, size_t nframes);
void createRxBuffers(rxclient_t* client, size_t nframes);
void freeTxBuffers(txclient_t* client);
void freeRxBuffers(rxclient_t* client);
void jackTXConnect(txclient_t* client);
void jackRXConnect(rxclient_t* client);
void prepareTXPackets(txclient_t* client);
void getRXPacket(rxclient_t* client, char* packet, uint16_t pktlen);
int rxProcess(jack_nframes_t nframes, void *arg);
int txProcess(jack_nframes_t nframes, void *arg);

#endif // VBANJACKCLIENT_H
