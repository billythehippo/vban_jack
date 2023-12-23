#include <getopt.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>

#include "../vban_common/vbanJackClient.h"
#include "../vban_common/udpsocket.h"

txclient_t client;
uint8_t autoconnect = 0;

static int audio_sd; //socket descriptor
struct sockaddr_in audio_si; //socket
char socket_mode = 'c';

struct pollfd pds;

config_t config;
uint8_t mapIsSet = 0;

enum autoconnect_modes
{
    NO = 0,
    YES,
    CARD
};

// sync timer
int tfd; // timer file descriptor
struct itimerspec ts; // time mark
struct pollfd tds[1]; // poll file descriptor for timer
int polln; // timer subroutine counter
uint64_t tval;  // value to read timerfd
int tlen;

// queue timer
int tqfd;
struct itimerspec tqs;
struct pollfd tqds[1];
int pollqn;
uint64_t tqval;
int tqlen;

#define SYNC_MARK 'CNYS'
typedef union
{
    VBanHeader header;
    int32_t packet[9];
} timePacket_t;

pthread_t synctid;
pthread_attr_t syncattr;
pthread_mutex_t sync_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  sync_ready = PTHREAD_COND_INITIALIZER;

pthread_t sendtid;
pthread_attr_t sendattr;
pthread_mutex_t send_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  send_ready = PTHREAD_COND_INITIALIZER;

timePacket_t timePacket;

#define DTARRSIZE 50
#define DTARRLAST DTARRSIZE-1
int64_t dtarr[DTARRSIZE];
int16_t dtind = 0;
int32_t tnew;
int32_t told;
int64_t dt;
float dtf;


inline void initTimePacket(timePacket_t* timePacket, txclient_t* client)
{
    memset(timePacket, 0, 36);
    timePacket->header.vban = VBAN_HEADER_FOURC;
    timePacket->header.format_SR = 0xE0; // USER SubProto, 7 is code for ts.nsec
    timePacket->header.format_bit = 0;
    timePacket->header.format_nbc = 0;
    timePacket->header.format_nbs = 0;
    timePacket->header.nuFrame = 0;
    strcpy(timePacket->header.streamname, client->header.streamname);
    timePacket->packet[7] = SYNC_MARK;
    timePacket->packet[8] = 0;
}


int parseMap(uint8_t* map, uint8_t* nbchannels, char* argv)
{
    size_t index = 0;
    unsigned int chan = 0;
    char* token;

    token = strtok(argv, ",");

    while ((index < VBAN_CHANNELS_MAX_NB) && (token != 0))
    {
        if (sscanf(token, "%u", &chan) == EOF)
            break;
        if ((chan > VBAN_CHANNELS_MAX_NB) || (chan < 1))
        {
            fprintf(stderr, "Invalid channel id %u, stop parsing", chan);
            break;
        }
        map[index++] = (unsigned char)(chan - 1);
        token = strtok(0, ",");
    }
    *nbchannels = index - 1;

    return 0;
}


void usage()
{
    printf("VBAN Emitter for JACK Audio Connection Kit\nby Billy the Hippo\n");
#ifdef __linux__
    printf("May also be used under Pipewire but prefix pw-jack -p<buffsize> is STRONGLY recommended\n");
#endif
    printf("\nUsage: vban_jack_emitter [OPTIONS]...\n\n");
    printf("-i, --ipaddr=ADDRESS    : IP address of target receiver\n");
    printf("-p, --port=PORT         : Destination port (default 6980)\n");
    printf("-s, --streamname=NAME   : Stream name\n");
    printf("-n, --nbchannels=NUMBER : Number of channels (if matrix is not set,\n                        : if set - this parameter will be ignored)\n");
    printf("-c, --channels=LIST     : channel matrix to autoconnect.\n                        : LIST is of form x,y,z,...\n                        : default is to forward the stream as it is\n");
    printf("-a, --autoconnect=MODE  : Autoconnect to default card:\n                        :    Y - yes,\n                        :    N - no,\n                        :    C - mimicrate to physical card\n");
    printf("-f, --format=FORMAT     : Bit format:\n                        :    16 - 16bit integer(default),\n                        :    24 - 24 bit integer,\n                        :    32 - 32 bit float\n");
    printf("-r, --redundancy        : How many times should we duplicate each packet?\n                        : 0 (default) to 5\n");
    printf("-h, --help              : Display this help\n\n");
}


int get_options(int argc, char* const* argv)
{
    int c = 0;
    int ret = -1;
    int arg;
    int ipBytes[4];
    uint i;

    static const struct option options[] =
    {
        {"ipaddr",      required_argument,  0, 'i'},
        {"port",        required_argument,  0, 'p'},
        {"streamname",  required_argument,  0, 's'},
        {"nbchannels",  required_argument,  0, 'n'},
        {"channels",    required_argument,  0, 'c'},
        {"timer",       required_argument,  0, 't'},
        {"autoconnect", required_argument,  0, 'a'},
        {"format",      required_argument,  0, 'f'},
        {"redundancy",  required_argument,  0, 'r'},
        {"help",        no_argument,        0, 'h'},
        {0,             0,                  0,  0 }
    };

    while (1)
    {
        c = getopt_long(argc, argv, "i:p:s:n:c:t:a:f:r:h", options, 0);
        if (c == -1)
        {
            if (ret!=0) usage();
            return ret;
        }

        switch (c)
        {
        case 'i':
            strncpy(config.IPaddr, optarg, strlen(optarg));
            sscanf(config.IPaddr, "%d.%d.%d.%d", &ipBytes[3], &ipBytes[2], &ipBytes[1], &ipBytes[0]);
            for (i=0; i<4; i++)
            {
                if ((ipBytes[i]>255)||(ipBytes[i]<0))
                {
                    fprintf(stderr, "Incorrect IP address!\n");
                    return 1;
                }
                else config.IPbytes[i] = ipBytes[i];
            }
            ret = 0;
            break;

        case 'p':
            arg = atoi(optarg);
            if ((arg>3000)&&(arg<65536)) config.port = arg;
            else fprintf(stderr, "Port is not set or incorrect. Using default port 6980!\n");
            ret = 0;
            break;

        case 's':
            memset(config.streamname, 0, 16);
            strncpy(config.streamname, optarg, strlen(optarg));
            ret = 0;
            break;

        case 'n':
            arg = atoi(optarg);
            if ((arg>0)&&(arg<=256)) config.nbchannels = arg;
            else
            {
                if (arg>256) fprintf(stderr, "Number of channels is too large! Maximum value is 256! Using default 2\n");
                else fprintf(stderr, "Number of channels is incorrect! Using default 2\n");
                config.nbchannels = 2;
            }
            if (mapIsSet==0) for(i=0; i<config.nbchannels; i++) client.map[i] = i;
            ret = 0;
            break;

        case 'c':
            ret = parseMap(client.map, &client.header.format_nbc, optarg);
            mapIsSet = 1;
            break;

        case 't':
            arg = atoi(optarg);
            if (arg==0)
            {
                client.flags&=~MODE_TIMER;
                client.maxSamplesPerPacket = VBAN_SAMPLES_MAX_NB;
                fprintf(stderr, "Send Timer is OFF! Data will be sent by fact!\n");
            }
            else if ((arg==32)||(arg==64)||(arg==128)||(arg==256))
            {
                client.flags|= MODE_TIMER;
                client.maxSamplesPerPacket = arg;
                fprintf(stderr, "Send Timer is ON! Data will be sent by %d samples parts!\n", arg);
            }
            else
            {
                client.flags&=~MODE_TIMER;
                client.maxSamplesPerPacket = VBAN_SAMPLES_MAX_NB;
                fprintf(stderr, "Invalid timer value!\nSend Timer is OFF! Data will be sent by fact!\n");
            }
            break;

        case 'a':
            if ((optarg[0]=='y')|(optarg[0]=='Y')) autoconnect = YES;
            else if ((optarg[0]=='c')|(optarg[0]=='C')) autoconnect = CARD;
            else autoconnect = NO;
            ret = 0;
            break;

        case 'f':
            arg = atoi(optarg);
            if (arg==16)
            {
                config.VBANResolution = VBAN_BITFMT_16_INT;
                fprintf(stderr, "Audio format is 16bit integer\n");
            }
            else if (arg==24)
            {
                config.VBANResolution = VBAN_BITFMT_24_INT;
                fprintf(stderr, "Audio format is 24bit integer\n");
            }
            else if (arg==32)
            {
                config.VBANResolution = VBAN_BITFMT_32_FLOAT;
                fprintf(stderr, "Audio format is 32bit float\n");
            }
            else
            {
                config.VBANResolution = VBAN_BITFMT_16_INT;
                fprintf(stderr, "Audio format is incorrect! Using default 16bit integer\n");
            }
            ret = 0;
            break;

        case 'r':
            arg = atoi(optarg);
            if ((arg>=0)&&(arg<6)) client.redundancy+= arg;
            else fprintf(stderr, "Invalid redundancy! Starting with no redundancy!\n");
            ret = 0;
            break;

        case 'h':
            usage();
            return 1;

        default:
            fprintf(stderr, "Using default parameters:\nPort: 6980, IP and name of stream: the first detected one!\n");
        }

    }

    if (client.ipAddr==0)
    {
        fprintf(stderr, "Stream destination IP addres is not set!\n");
        return 1;
    }
    if (client.header.streamname[0]==0)
    {
        fprintf(stderr, "Stream name is not set!\n");
        return 1;
    }

    return ret;
}


void init_defaults(config_t* config, txclient_t* client)
{
    config->IPbytes = (uint8_t*)&config->IP;
    config->VBANResolution = 1;
    config->nbchannels = 2;
    config->port = 6980;
    memset(config->streamname, 0, 16);

    memset(client, 0, sizeof(rxclient_t));
    client->ipAddr = 0;
    client->udpPort = 6980;
    client->header.format_bit = VBAN_BITFMT_16_INT;
    memset(client->map, 0, VBAN_CHANNELS_MAX_NB);
    client->redundancy = 0;

//    client->flags|= MODE_TIMER;
}


void* syncThread(void* param)
{
    while(1) // Thread is enabled cond
    {
        polln = poll(tds, tlen, -1);
        for (int i = 0; i < tlen && polln-- > 0; ++i)
        {
            if (tds[i].revents & POLLIN)
            {
                int tret = read(tfd, &tval, sizeof(tval));
                if (tret != sizeof(tval)) // ret should be 8
                {
                    fprintf(stderr, "Timer error\n");
                    break;
                }
                else // on timer actions
                {
                    clock_gettime(CLOCK_REALTIME, &client.ts);
                    timePacket.header.nuFrame = client.ts.tv_sec;
                    timePacket.packet[8] = client.ts.tv_nsec;
                    UDP_send(audio_sd, &audio_si, (uint8_t*)&timePacket, sizeof(timePacket_t));

                    told = tnew;
                    tnew = timePacket.packet[8];
                    dt = tnew - told;
                    if (dt<0) dt+= 1000000000;
                    fprintf(stderr, "%ld\n", dt);
                }
            }
        }
    }
}


void* sendThread(void* param)
{
    uint8_t red;
    uint packet;
    uint packetlen;
    int ret;
    uint ptr;
    uint frame;
    uint sample;
    uint8_t resolution;
    uint8_t samplesize;
    uint16_t nchannels;
    uint32_t framesize;
    uint nframes;

    while(1) // Thread is enabled cond
    {
        resolution = client.header.format_bit;
        samplesize = VBanBitResolutionSize[resolution];
        nchannels = (client.header.format_nbc+1);
        framesize = samplesize*nchannels;
        nframes = client.header.format_nbs+1;
        packetlen = VBAN_HEADER_SIZE + samplesize*nchannels*nframes;
        ret = poll(tqds, tqlen, -1);
        read(tqfd, &tqval, sizeof(tqval));

        for (packet=0; packet<client.pktNum; packet++)
        {
            memset(client.txPackets, 0, packetlen);
            memcpy(client.txPackets, (char*)&client.header, VBAN_HEADER_SIZE);
            ptr = VBAN_HEADER_SIZE;

            for(frame=0; frame<nframes; frame++)
            {
                if (!(jack_ringbuffer_read_space(client.txrBuffer)<framesize))
                    for(sample=0; sample<nchannels; sample++)
                    {
                        jack_ringbuffer_read(client.txrBuffer, (char*)&client.txPackets[ptr], samplesize);
                        ptr+= samplesize;
                    }
            }
            for (red=0; red<(client.redundancy+1); red++)
                UDP_send(*client.audio_sd, client.audio_si, (uint8_t*)&client.txPackets[0], packetlen);
            inc_nuFrame(&client.header);
        }
    }
}


int main(int argc, char *argv[])
{
    uint8_t red;
    uint packet;
    uint packetlen;

    init_defaults(&config, &client);

    if (get_options(argc, argv))
    {
        usage();
        return 1;
    }

    // int socket
    audio_sd = UDP_init(&audio_sd, &audio_si, config.IPaddr, config.port, socket_mode, 1, 6);
    if (audio_sd<0)
    {
        fprintf(stderr, "Can't init the socket!\n");
    }
    else
    {
        fprintf(stderr, "Socket created, %d\n", audio_sd);
    }

    // init sync timer
    tfd = timerfd_create(CLOCK_REALTIME,  0);
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 500000000;
    ts.it_value.tv_sec = 1;
    ts.it_value.tv_nsec = 0;
    timerfd_settime(tfd, 0, &ts, NULL);
    tlen = sizeof(tds) / sizeof(tds[0]);
    tds[0].fd = tfd;
    tds[0].events = POLLIN;

    //create JACK client and go!
    if(createTxClient(&client, config.streamname, config.IP, config.port, config.VBANResolution, config.nbchannels, &audio_sd, &audio_si))
    {
        if (client.flags&MODE_TIMER)
        {
            //init queue timer
            tqfd = timerfd_create(CLOCK_REALTIME,  0);
            tqs.it_interval.tv_sec = 0;
            tqs.it_interval.tv_nsec = client.timerPeriod + 1;
            tqs.it_value.tv_sec = 0;
            tqs.it_value.tv_nsec = client.timerPeriod + 1;
            timerfd_settime(tqfd, 0, &tqs, NULL);
            tqlen = sizeof(tqds)/sizeof(tqds[0]);
            tqds[0].fd = tqfd;
            tqds[0].events = POLLIN;

            pthread_attr_init(&sendattr);
            pthread_create(&sendtid, &sendattr, sendThread, NULL);
        }


        pthread_attr_init(&syncattr);
        pthread_create(&synctid, &syncattr, syncThread, NULL);

        initTimePacket(&timePacket, &client);

        while (1)
        {
            if (client.flags&MODE_TIMER)
            {
                sleep(1);
            }
            else
            {
                sleep(1);
                /*pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
                pthread_mutex_lock (&tx_read_thread_lock);
                while ((client.flags&READY_READ)==0) pthread_cond_wait(&tx_data_ready, &tx_read_thread_lock);
                prepareTXPackets(&client);
                packetlen = client.pktLen + VBAN_HEADER_SIZE;
                for (packet=0; packet<client.pktNum; packet++)
                {
                    for (red=0; red<(client.redundancy+1); red++)
                        UDP_send(*client.audio_sd, client.audio_si, (uint8_t*)&client.txPackets[packet*packetlen], packetlen);
                    //usleep(client.timerPeriod/2000);
                }

                client.flags&=~READY_READ;
                pthread_mutex_unlock(&tx_read_thread_lock);//*/
            }
        }
    }

    return 0;
}
