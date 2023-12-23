#include <getopt.h>
#include <poll.h>
#include <pthread.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "../vban_common/vbanJackClient.h"
#include "../vban_common/udpsocket.h"

rxclient_t client;
uint8_t autoconnect = 0;

static int audio_sd; //socket descriptor
struct sockaddr_in audio_si; //socket
char socket_mode = 's';

struct pollfd pds;

pthread_t rxtid;
pthread_attr_t attr;
pthread_mutex_t read_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;

config_t config;
uint8_t mapIsSet = 0;
uint16_t netoverruns = 0;

#define MED_SIZE 7
#define SYNC_MARK 'CNYS'
int32_t tns_lc_old;
int32_t tns_lc_new;
int32_t tns_rx_old;
int32_t tns_rx_new;
int32_t dt_lc;
int32_t dt_rx;
double ct = 1;
double ct1 = 1, ct2 = 1, ct3 = 1;
int dct;
double dctf, dctfold = 0;
int med_ct_arr[MED_SIZE];
uint8_t med_ind = 0;

enum autoconnect_modes
{
    NO = 0,
    YES,
    CARD
};


void processSyncPacket(rxclient_t* client, VBanPacket* packet, uint32_t ip);
void writeCorrectedSample(rxclient_t* client, VBanPacket* packet, uint16_t frame);


void* rxThread(void* param)
{
    VBanPacket packet;
    int16_t pktlen;
    char jackClientName[32];
    uint16_t framesize;
    uint16_t frame;
    union
    {
        uint32_t ip;
        uint8_t ipbytes[4];
    };
    union
    {
        VBanHeader infoheader;
        char infobytes[32];
    };

    fprintf(stderr, "Thread created!\n");
    while(1) // Thread is enabled cond
    {
        while(poll(&pds, 1, 100))
        {
            clock_gettime(CLOCK_REALTIME, &client.ts);
            pktlen = UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);
            if (pktlen)
            {
                ip = htonl(audio_si.sin_addr.s_addr);
                switch (packet.header.format_SR&VBAN_PROTOCOL_MASK)
                {
                case VBAN_PROTOCOL_AUDIO:
                    if (client.header.vban!=VBAN_HEADER_FOURC) // Client has not been inited!
                    {
                        if (((client.ipAddr==ip)||(client.ipBytes[0]==0))&&((strncmp(packet.header.streamname, client.header.streamname, 16)==0)||(client.header.streamname[0]==0)))
                        {
                            client.ipAddr = ip;
                            client.udpRxPort = int16betole(audio_si.sin_port);
                            memset(jackClientName, 0, 32);
                            createRxClient(&client, packet.header.streamname, ip, packet.header, pktlen);
                            if (autoconnect) jackRXConnect(&client);

                            jack_ringbuffer_write(client.rxBuffer, packet.data, pktlen-VBAN_HEADER_SIZE);
                            while(poll(&pds, 1, 0)) // Flush socket RX buffer with SYNC control
                            {
                                UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);
                                if ((packet.header.format_SR&VBAN_PROTOCOL_MASK)==VBAN_PROTOCOL_USER) processSyncPacket(&client, &packet, ip);
                            }
                            fprintf(stderr, "Flush socket!\r\n");
                        }
                    }
                    else
                    {
                        if ((strncmp(packet.header.streamname, client.header.streamname, 16)==0)&&(client.ipAddr==ip))
                        {
                            if ((client.header.format_SR ==packet.header.format_SR)&&
                                (client.header.format_bit==packet.header.format_bit)&&
                                (client.header.format_nbc==packet.header.format_nbc))
                            {
                                if (client.header.nuFrame!=packet.header.nuFrame)
                                {
                                    framesize = (packet.header.format_nbc + 1)*(VBanBitResolutionSize[packet.header.format_bit]);
                                    for (frame=0; frame<=packet.header.format_nbs; frame++)
                                    {
                                        //while(jack_ringbuffer_write_space(client.rxBuffer)<framesize);
                                        if (!(jack_ringbuffer_write_space(client.rxBuffer)<framesize))
                                        {
                                            //writeCorrectedSample(&client, &packet, frame);
                                            jack_ringbuffer_write(client.rxBuffer, &packet.data[frame*framesize], framesize);
                                        }
                                        else netoverruns++;
                                    }
                                    if (netoverruns)
                                    {
                                        fprintf(stderr, "Network overrun! %d samples lost.\n", netoverruns);
                                        netoverruns = 0;
                                        while(poll(&pds, 1, 0)) // Flush socket RX buffer with SYNC control
                                        {
                                            UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);
                                            if ((packet.header.format_SR&VBAN_PROTOCOL_MASK)==VBAN_PROTOCOL_USER) processSyncPacket(&client, &packet, ip);
                                        }
                                        fprintf(stderr, "Flush socket!\r\n");
                                    }
                                    if ((packet.header.nuFrame-client.header.nuFrame)>1) fprintf(stderr, "Network underrun!\n");
                                    client.header.nuFrame = packet.header.nuFrame;
                                }
                            }
                            else // Recreate Jack client
                            {
                                deleteRxClient(&client);
                                createRxClient(&client, packet.header.streamname, ip, packet.header, pktlen);
                                if (autoconnect) jackRXConnect(&client);
                                jack_ringbuffer_write(client.rxBuffer, packet.data, pktlen-VBAN_HEADER_SIZE);
                                while(poll(&pds, 1, 0)) UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);  // Flush socket RX buffer TOTALLY
                                fprintf(stderr, "Total flush socket!\r\n");
                            }
                        }
                    }
                    if (client.flags&FLUSH_SOCKET)
                    {
                        client.flags&=~FLUSH_SOCKET;
                        while(poll(&pds, 1, 0)) // Flush socket RX buffer with SYNC control
                        {
                            UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);
                            if ((packet.header.format_SR&VBAN_PROTOCOL_MASK)==VBAN_PROTOCOL_USER) processSyncPacket(&client, &packet, ip);
                        }
                        fprintf(stderr, "Flush socket!\r\n");
                    }
                    break;
                case VBAN_PROTOCOL_TXT:
                    if (packet.header.vban==VBAN_HEADER_FOURC)
                        if(!(strcmp(packet.header.streamname, "INFOREQUEST")))
                        {
                            memset(infobytes, 0, 32);
                            if (client.header.vban==0)
                            {
                                infoheader.vban = VBAN_HEADER_FOURC;
                                infoheader.format_SR = VBAN_PROTOCOL_TXT;
                                if (config.streamname[0]==0) strcat(infoheader.streamname, "Receptor");
                                else strcpy(infoheader.streamname, config.streamname);
                                strcat(&infobytes[28], "FREE");
                            }
                            else
                            {
                                infoheader = client.header;
                                infoheader.format_SR = (infoheader.format_SR&0xE0)|VBAN_PROTOCOL_TXT;
                                strcat(&infobytes[28], "BUSY");
                            }
                            UDP_send_datagram(audio_si.sin_addr.s_addr, audio_si.sin_port, (uint8_t*)&infobytes, 32, 5);
                        }
                    break;
                case VBAN_PROTOCOL_USER:
                    processSyncPacket(&client, &packet, ip);
                    break;
                default:
                    break;
                }
            }
        }
    }
}


int median(int* array, uint size)
{
    float min;
    uint min_i;

    for(uint i=0; i<((size>>1)+1); i++)
    {
        min = array[i];
        min_i = i;

        for(uint j=i+1; j<size; j++)
            if (array[j]<min)
            {
                min = array[j];
                min_i = j;
            }
        array[min_i] = array[i];
        array[i] = min;
    }

    return array[(size>>1)+1];
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
    printf("VBAN Receptor for JACK Audio Connection Kit\nby Billy the Hippo\n");
#ifdef __linux__
    printf("May also be used under Pipewire but prefix pw-jack -p<buffsize> is STRONGLY recommended\n");
#endif
    printf("\nUsage: vban_jack_receptor [OPTIONS]...\n\n");
    printf("-i, --ipaddr=ADDRESS    : IP address or mask of transmitter\n                        : (default - first found stream)\n");
    printf("-p, --port=PORT         : Port to bind (default 6980)\n");
    printf("-s, --streamname=NAME   : Stream name to receive\n                        : (default - first found stream)\n");
    printf("-q, --quality=ID        : Network quality: from 0 (default, best, low latency)\n                        :                  to 4 (worst, high latency)\n");
    printf("-c, --channels=LIST     : channel matrix to autoconnect.\n                        : LIST is of form x,y,z,...\n                        : default is to forward the stream as it is\n");
    printf("-a, --autoconnect=MODE  : Autoconnect to default card:\n                        : Y - yes,\n                        : N - no,\n                        : C - mimicrate to physical card\n");
    printf("-j, --clientname=NAME   : Jack client name\n");
    printf("-h, --help              : Display this help\n\n");
}


int get_options(int argc, char* const* argv)
{
    int c = 0;
    int ret = 0;
    int arg;
    int ipBytes[4];
    uint8_t i;

    static const struct option options[] =
    {
        {"ipaddr",      required_argument,  0, 'i'},
        {"port",        required_argument,  0, 'p'},
        {"streamname",  required_argument,  0, 's'},
        {"quality",     required_argument,  0, 'q'},
        {"channels",    required_argument,  0, 'c'},
        {"timer",       required_argument,  0, 't'},
        {"autoconnect", required_argument,  0, 'a'},
        {"clientname",  required_argument,  0, 'j'},
        {"help",        no_argument,        0, 'h'},
        {0,             0,                  0,  0 }
    };

    while (1)
    {
        c = getopt_long(argc, argv, "i:p:s:q:c:t:a:j:h", options, 0);
        if (c == -1)
        {
            //if (ret!=0) fprintf(stderr, "Using default parameters:\nPort: 6980, IP and name of stream: the first detected one!\n");
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
                else client.ipBytes[i] = ipBytes[i];
            }
            ret = 0;
            break;

        case 'p':
            arg = atoi(optarg);
            if ((arg>3000)&&(arg<65536)) client.udpPort = arg;
            else fprintf(stderr, "Port is not set or incorrect. Using default port 6980!\n");
            ret = 0;
            break;

        case 's':
            memset(client.header.streamname, 0, 16);
            strncpy(client.header.streamname, optarg, strlen(optarg));
            ret = 0;
            break;

        case 'q':
            arg = atoi(optarg);
            if ((arg>=0)&&(arg<=4)) client.netquality = arg;
            else fprintf(stderr, "Quality index is incorrect. Using default value 0 (best)!\n");
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
            }
            else if ((arg==32)||(arg==64)||(arg==128)||(arg==256))
            {
                client.flags|= MODE_TIMER;
                client.rxSamples = arg;
                fprintf(stderr, "Awaiting timer packets with %d samples per one!\n", arg);
            }
            else
            {
                client.flags&=~MODE_TIMER;
            }
            break;

        case 'a':
            if ((optarg[0]=='y')|(optarg[0]=='Y'))
            {
                autoconnect = YES;
                if(mapIsSet==0) for(i=0; i<config.nbchannels; i++) client.map[i] = i;
            }
            else if ((optarg[0]=='c')|(optarg[0]=='C')) autoconnect = CARD;
            else autoconnect = NO;
            ret = 0;
            break;

        case 'j':
            memset(client.name, 0, 16);
            if (strlen(optarg)>15) fprintf(stderr, "Jack Client Name is larger than 15 symbols!\n");
            else strcpy(client.name, optarg);
            break;

        case 'h':
            usage();
            return 1;

        default:
            fprintf(stderr, "Using default parameters:\nPort: 6980, IP and name of stream: the first detected one!\n");
            ret = 0;
        }
    }

    return ret;
}


void init_defaults(config_t* config, rxclient_t* client)
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
}


void processSyncPacket(rxclient_t* client, VBanPacket* packet, uint32_t ip)
{
    if ((strncmp(packet->header.streamname, client->header.streamname, 16)==0)&&(client->ipAddr==ip)&&(*(uint32_t*)packet->data==SYNC_MARK))
    {
        //clock_gettime(CLOCK_REALTIME, &client->ts);
        tns_lc_old = tns_lc_new;
        tns_rx_old = tns_rx_new;
        tns_lc_new = client->ts.tv_nsec;
        tns_rx_new = *(int32_t*)&packet->data[4];
        dt_lc = tns_lc_new-tns_lc_old;
        if (dt_lc<0) dt_lc+= 1e9;
        dt_rx = tns_rx_new-tns_rx_old;
        if (dt_rx<0) dt_rx+= 1e9;
        if ((dt_rx<510000000)&&(dt_lc<510000000)&&(dt_rx>490000000)&&(dt_lc>490000000))
        {
            ct = 0.99*ct + (dt_lc!=0 ? (double)dt_rx/((double)dt_lc*100.0) : 0);
            //ct1 = 0.99*ct1 + (dt_lc!=0 ? (double)dt_rx/((double)dt_lc*100.0) : 0);
            //ct = 0.99*ct + 0.01*ct1;
            //ct3 = 0.9*ct3 + 0.1*ct2;
            //ct  = 0.9*ct  + 0.1*ct3;
            //ct = (dt_lc!=0 ? (float)dt_rx/((float)dt_lc) : 0);
            //if (abs(ct)>1.001) ct = 1.001;
            //else if (abs(ct)<0.999) ct = 0.999;

            dct = (int)round((1-ct)*1e6);
            //dctf = dct;//0.9*dctf + 0.1*dct;

            med_ct_arr[med_ind] = (int)round((1-ct)*1e6);
            med_ind++;
            if (med_ind==MED_SIZE) med_ind = 0;

            dctf = median(med_ct_arr, MED_SIZE);

            fprintf(stderr, "%u    %d    %d    %f    %f    %f\n", client->samplecount, dt_lc, dt_rx, (float)(tns_rx_new-tns_lc_new)/1000000000, ct, dctf);
        }
    }
}


void writeCorrectedSample(rxclient_t* client, VBanPacket* packet, uint16_t frame)
{
    uint16_t framesize = (packet->header.format_nbc + 1)*(VBanBitResolutionSize[packet->header.format_bit]);

    if ((client->samplecount>=client->sampleCountMax)&&(abs(dctf)<=100))
    {
        if (!(dctf<-1))
        {
            jack_ringbuffer_write(client->rxBuffer, &packet->data[frame*framesize], framesize);
            if (dctf>1)
            {
                jack_ringbuffer_write(client->rxBuffer, &packet->data[frame*framesize], framesize); //additional
                fprintf(stderr, "Added sample!\n");
            }
        }
        else fprintf(stderr, "Removed sample!\n");
        if (dctf!=0)
        {
            client->fraction+= 1000000/abs(dctf);
            client->sampleCountMax = client->fraction;
            client->fraction-= client->sampleCountMax;
            client->sampleCountMax--;
        }
        client->samplecount = 0;
    }
    else
    {
        if (dctf!=dctfold)
        {
            client->fraction+= 1000000/abs(dctf);
            client->sampleCountMax = client->fraction;
            client->fraction-= client->sampleCountMax;
            client->sampleCountMax--;
            dctfold = dctf;
        }
        client->samplecount++;
        jack_ringbuffer_write(client->rxBuffer, &packet->data[frame*framesize], framesize);
    } //*/
}


int main(int argc, char *argv[])
{
    union
    {
        uint32_t ip;
        uint8_t ipbytes[4];
    };


    init_defaults(&config, &client);
    if (get_options(argc, argv)) return 1;
    if (config.IPaddr[0]==0) strcpy(config.IPaddr, "0.0.0.0");

    audio_sd = UDP_init(&audio_sd, &audio_si, config.IPaddr, client.udpPort, socket_mode, 1, 6);
    if (audio_sd<0)
    {
        fprintf(stderr, "Can't bind the socket!\n");
    }
    else
    {
        fprintf(stderr, "Socket created, %d\n", audio_sd);
        pds.fd = audio_sd;
        pds.events = POLLIN;
    }

    pthread_attr_init(&attr);
    pthread_create(&rxtid, &attr, rxThread, NULL);

    while(poll(&pds, 1, 0)) // Flush socket RX buffer with SYNC control
    {
        UDP_recv(audio_sd, &audio_si, NULL, VBAN_PROTOCOL_MAX_SIZE);
    }

    while(1)
    {
        sleep(1);
    }
    return 0;
}
