#include "vbanJackClient.h"
#include "udpsocket.h"

pthread_mutex_t tx_read_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tx_data_ready = PTHREAD_COND_INITIALIZER;
pthread_t sendPacketThread;

uint8_t callback_is_set = 0;
rxclient_t* rxc;


int createTxClient(txclient_t* client, const char* __restrict name,  uint32_t IP, uint16_t port, uint8_t VBANResolution, uint nbchannels, int* audio_sd, struct sockaddr_in* audio_si)
{
    uint8_t ret = 1;
    uint channel;
    uint nframes;
    char jackClientName[32];
    char port_name[32];
    uint8_t nameLen;
//    uint8_t samplesize;
//    uint framesize;
//    uint bufsize;
//    uint timerbufsize;

    if (nbchannels==0)
    {
        fprintf(stderr, "No channels, exiting!\n");
        return 0;
    }

    client->flags&=(AUTOCONNECT|CARD_LIKE|MODE_TIMER);
    client->header.vban = VBAN_HEADER_FOURC;
    client->header.nuFrame = 0;
    if (VBANResolution>7) VBANResolution = VBAN_BITFMT_16_INT;
    client->header.format_bit = VBANResolution;
    if (nbchannels>256) nbchannels = 256;
    client->header.format_nbc = (nbchannels - 1);

    nameLen = strlen(name);
    if (nameLen>16) nameLen = 16;
    memset(client->header.streamname, 0, 16);
    strncpy(client->header.streamname, name, nameLen);

    client->ipAddr = IP;
    client->udpPort = port;
    client->audio_sd = audio_sd;
    client->audio_si = audio_si;
    memset(jackClientName, 0, 32);
    sprintf(jackClientName, "%d.%d.%d.%d %s", client->ipBytes[3], client->ipBytes[2], client->ipBytes[1], client->ipBytes[0], client->header.streamname);

    client->client = jack_client_open(jackClientName, JackNullOption, NULL);
    if (client->client==0)
    {
        fprintf(stderr, "%s: could not open jack client\n", __func__);
        ret = 0;
    }
    else
    {
        client->audioBuffSize    = jack_get_buffer_size(client->client);
        nframes                  = client->audioBuffSize;
        client->sampleRate       = jack_get_sample_rate(client->client);
        client->header.format_SR = getSampleRateIndex(client->sampleRate);
        if (client->maxSamplesPerPacket==0) client->maxSamplesPerPacket = VBAN_SAMPLES_MAX_NB;

        for (channel = 0; channel<nbchannels; channel++)
        {
            snprintf(port_name, sizeof(port_name)-1, "Input_%u", (unsigned int)(channel+1));
            client->ports[channel] = jack_port_register(client->client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
            if (client->ports[channel] == 0)
            {
                fprintf(stderr, "%s: impossible to set jack port for input channel %d\r\n", __func__, channel);
                ret = 0;
            }
        }

        if (jack_set_process_callback(client->client, txProcess, (void*)client))
        {
            fprintf(stderr, "%s: impossible to set jack process callback", __func__);
            ret = 0;
        }
        /*else if (jack_set_buffer_size_callback(client->client, NULL, NULL))
        {
            fprintf(stderr, "%s: impossible to set jack buffer size callback", __func__);
            ret = 0;
        }*/
        else
        {
            if (createTxBuffers(client, nframes)) ret = 0;
        }
    }
    if (ret==0) deleteTxClient(client);
    else jack_activate(client->client);
    return ret;
}


int createRxClient(rxclient_t* client, const char* __restrict name, uint32_t IP, VBanHeader header, uint16_t pktlen)
{
    uint8_t ret = 1;
    uint channel;
    uint nbchannels;
    uint nframes;
    char jackClientName[32];
    char port_name[32];
    char stream_format[64];
    char bitrate[8];
    long samplerate;
    uint8_t nameLen;
//    uint rbsize;
//    uint pfsize;

    client->flags&=(AUTOCONNECT|CARD_LIKE);
    client->bufEmptyCount = 0;
    client->badPackets = 0;
    client->samplecount = 0;
    client->sampleCountMax = 1000000;
    client->fraction = 0;
    client->pktlen = pktlen;

    client->header = header;
    nbchannels = client->header.format_nbc + 1;

    if (name!=NULL)
    {
        nameLen = strlen(name);
        if (nameLen>16) nameLen = 16;
        memset(client->header.streamname, 0, 16);
        strncpy(client->header.streamname, name, nameLen);
    }

    client->ipAddr = IP;
    memset(jackClientName, 0, 32);
    if ((client->ipBytes[3]!=127)&&(client->ipBytes[2]!=0)&&(client->ipBytes[1]!=0)) sprintf(jackClientName, "%s %d.%d.%d.%d %s", client->name, client->ipBytes[3], client->ipBytes[2], client->ipBytes[1], client->ipBytes[0], client->header.streamname);
    else sprintf(jackClientName, "%s %s", client->name, client->header.streamname); // Prevent licalhost IP in Name

    client->client = jack_client_open(jackClientName, JackNullOption, NULL);
    if (client->client==0)
    {
        fprintf(stderr, "%s: could not open jack client\n", __func__);
        ret = 0;
    }
    else
    {
        client->audioBuffSize   = jack_get_buffer_size(client->client);
        client->sampleRate      = jack_get_sample_rate(client->client);
        nframes                 = client->audioBuffSize;
        client->sampleCountMax  = client->sampleRate*10;

        if (VBanSRList[header.format_SR]!=client->sampleRate)
        {
            // TODO : Rework this for Resampler
            jack_client_close(client->client);
            fprintf(stderr, "%s: samplerate mismatch:\n            Jack server samplerate is %u\n,            Stream samplerate is %ld\n", __func__, client->sampleRate, VBanSRList[header.format_SR]);
            ret = 0;
        }
        else
        {
            for (channel = 0; channel<nbchannels; channel++)
            {
                snprintf(port_name, sizeof(port_name)-1, "Output_%u", (unsigned int)(channel+1));
                client->ports[channel] = jack_port_register(client->client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
                if (client->ports[channel] == 0)
                {
                    fprintf(stderr, "%s: impossible to set jack port for out channel %d\r\n", __func__, channel);
                    ret = 0;
                }
            }

            if (jack_set_process_callback(client->client, rxProcess, (void*)client))
            {
                fprintf(stderr, "%s: impossible to set jack process callback", __func__);
                ret = 0;
            }
            /*else if (jack_set_buffer_size_callback(client->client, NULL, NULL))
            {
                fprintf(stderr, "%s: impossible to set jack buffer size callback", __func__);
                ret = 0;
            }*/
            else
            {
                createRxBuffers(client, nframes);
            }
        }
    }
    if (ret==0) deleteRxClient(client);
    else
    {
        jack_activate(client->client);
        memset(stream_format, 0, 64);
        switch (client->header.format_bit)
        {
        case VBAN_BITFMT_16_INT:
            sprintf(bitrate, "16bit");
            break;
        case VBAN_BITFMT_24_INT:
            sprintf(bitrate, "24bit");
            break;
        case VBAN_BITFMT_32_INT:
            sprintf(bitrate, "32bit");
            break;
        case VBAN_BITFMT_32_FLOAT:
            sprintf(bitrate, "32float");
            break;
        default:
            break;
        }
        samplerate = VBanSRList[client->header.format_SR&VBAN_SR_MASK];
        sprintf(stream_format, "Incoming stream: %s, %d channels, %s, %ldHz\r\n", client->header.streamname, client->header.format_nbc + 1, bitrate, samplerate);
        fprintf(stderr, "%s", stream_format);
        fprintf(stderr, "%d.%d.%d.%d:%d\r\n", client->ipBytes[3], client->ipBytes[2], client->ipBytes[1], client->ipBytes[0], client->udpRxPort);
    }
    return ret;
}


void deleteTxClient(txclient_t* client)
{
    fprintf(stderr, "Closing JACK client %s", client->header.streamname);
    jack_deactivate(client->client);
    jack_client_close(client->client);
    freeTxBuffers(client);;
    memset(client->ports, 0, VBAN_CHANNELS_MAX_NB * sizeof(jack_port_t*));
    client->client = 0;
}


void deleteRxClient(rxclient_t* client)
{
    fprintf(stderr, "Closing JACK client %s", client->header.streamname);
    jack_deactivate(client->client);
    jack_client_close(client->client);
    freeRxBuffers(client);
    memset(client->ports, 0, VBAN_CHANNELS_MAX_NB * sizeof(jack_port_t*));
    client->client = 0;
}


uint8_t createTxBuffers(txclient_t* client, size_t nframes)
{
    uint samplesize = VBanBitResolutionSize[client->header.format_bit];
    uint nbchannels = client->header.format_nbc + 1;
    uint framesize = samplesize*nbchannels;
    uint bufsize = framesize*nframes;
    uint timerbufsize = framesize*client->maxSamplesPerPacket;
    client->pktNum = 1;

    if (client->flags&MODE_TIMER)
    {
        while((timerbufsize/client->pktNum)>VBAN_DATA_MAX_SIZE) client->pktNum = client->pktNum * 2;
        client->pktLen = timerbufsize/client->pktNum;
        client->header.format_nbs = (client->pktLen/(samplesize*nbchannels)) - 1;
        client->timerPeriod = (int32_t)round(1000000000.0*(client->maxSamplesPerPacket)/client->sampleRate);
        fprintf(stderr, "Buffer size is %d, Timer period is %d ns\n", bufsize/(client->pktNum*samplesize), client->timerPeriod);

    }
    else
    {
        while((client->audioBuffSize/client->pktNum)>VBAN_SAMPLES_MAX_NB) client->pktNum = client->pktNum * 2;
        while((bufsize/client->pktNum)>VBAN_DATA_MAX_SIZE) client->pktNum = client->pktNum * 2;
        client->pktLen = bufsize/client->pktNum;
        client->header.format_nbs = (client->pktLen/(samplesize*nbchannels)) - 1;

    }

    client->txBufSize = nbchannels*nframes; // in samples!
    if (client->txBufSize == 0)
    {
        fprintf(stderr, "%s: impossible to calculate packet data length", __func__);
        return 1;
    }
    else
    {
        if ((client->flags&MODE_TIMER)&&(timerbufsize*2<bufsize))
        {
            // RINGBUFFER
            client->txrBufSize = 2*bufsize;
            client->txrBuffer = jack_ringbuffer_create(client->txrBufSize);
            char* const zeros = (char*)calloc(1, (client->header.format_nbs+1)*samplesize*2);
            jack_ringbuffer_write(client->txrBuffer, zeros, timerbufsize*2);
            free(zeros);

        }
        else
        {
            // LINEAR BUFFER AND PACKET QUEUE
            client->flags&=~MODE_TIMER;
            client->txBuffer = (float*)malloc(client->txBufSize * sizeof(float));

        }
        client->txPackets = (char*)malloc((VBAN_HEADER_SIZE + client->pktLen)*client->pktNum);

        client->syncCnt = 0;
        client->syncPer = (40960/client->audioBuffSize) - 1;
        return 0;
    }
}


void createRxBuffers(rxclient_t* client, size_t nframes)
{
    uint nbchannels = client->header.format_nbc + 1;
    uint rbsize = nbchannels*nframes*VBanBitResolutionSize[client->header.format_bit];
    uint pfsize = client->pktlen - VBAN_HEADER_SIZE;
    rbsize = (rbsize<pfsize ? pfsize : rbsize);
    rbsize = rbsize*(1 + client->netquality)*3;
    client->rxBuffer = jack_ringbuffer_create(rbsize);
    if (client->netquality>0)
    {
        if ((client->flags&MODE_TIMER)&&((uint)client->rxSamples<nframes))
        {
            char* const zeros = (char*)calloc(1, nbchannels*client->rxSamples*VBanBitResolutionSize[client->header.format_bit]*2);
            jack_ringbuffer_write(client->rxBuffer, zeros, rbsize/2);
            free(zeros);
        }
       else
        {
            client->flags&=~MODE_TIMER;
            char* const zeros = (char*)calloc(1, rbsize/2);
            jack_ringbuffer_write(client->rxBuffer, zeros, rbsize/2);
            free(zeros);
        }
    }
    client->rxbuf = (float*)malloc(nbchannels*sizeof(float));
    client->sparts = (char*)malloc(nbchannels*8);
}


void freeTxBuffers(txclient_t* client)
{
    if (client->txrBuffer!=0) jack_ringbuffer_free(client->txrBuffer);
    free(client->txBuffer); //if (client->txBuffer)
    free(client->txPackets);
}


void freeRxBuffers(rxclient_t* client)
{
    if (client->rxBuffer!=0) jack_ringbuffer_free(client->rxBuffer);
    free(client->rxbuf);
    free(client->sparts);
}


void jackTXConnect(txclient_t* client)
{
    int ret = 0;
    char const** ports;
    volatile size_t port_id;
    volatile size_t pports = 0;

    ports = jack_get_ports(client->client, 0, 0, JackPortIsPhysical|JackPortIsOutput);

    if (ports != 0)
    {
        for (port_id=0; port_id<1024; port_id++)
        {
            if (ports[port_id]==0) break;
            if (strstr(ports[port_id], "midi")==NULL) pports++;
        }
        for (port_id=0; port_id<(client->header.format_nbc+1); port_id++)
        {
            if (client->map[port_id]<=pports)
            {
                if (ports[client->map[port_id]]!=NULL) if (strstr(ports[client->map[port_id]], "midi")==NULL)
                {
                    ret = jack_connect(client->client, ports[client->map[port_id]], jack_port_name(client->ports[port_id]));
                    if (ret)
                    {
                        fprintf(stderr, "%s: could not autoconnect channel %zu\n", __func__, port_id);
                    }
                    else
                    {
                        fprintf(stderr, "%s: channel %zu autoconnected\n", __func__, port_id);
                    }
                }
            }
        }

        jack_free(ports);
    }
    else
    {
        fprintf(stderr, "%s: could not autoconnect channels\n", __func__);
    }
}


void jackRXConnect(rxclient_t* client)
{
    int ret = 0;
    char const** ports;
    volatile size_t port_id;
    volatile size_t pports = 0;

    ports = jack_get_ports(client->client, 0, 0, JackPortIsPhysical|JackPortIsInput);

    if (ports != 0)
    {
        for (port_id=0; port_id<1024; port_id++)
        {
            if (ports[port_id]==0) break;
            if (strstr(ports[port_id], "midi")==NULL) pports++;
        }

        for (port_id=0; port_id<(client->header.format_nbc+1); port_id++)
        {
            if (client->map[port_id]<=pports)
            {
                if (strstr(ports[client->map[port_id]], "midi")==NULL)
                {
                    ret = jack_connect(client->client, jack_port_name(client->ports[port_id]), ports[client->map[port_id]]);
                    if (ret)
                    {
                        fprintf(stderr, "%s: could not autoconnect channel %zu'n", __func__, port_id);
                    }
                    else
                    {
                        fprintf(stderr, "%s: channel %zu autoconnected\n", __func__, port_id);
                    }
                }
            }
        }

        jack_free(ports);
    }
    else
    {
        fprintf(stderr, "%s: could not autoconnect channels\n", __func__);
    }
}


void prepareTXPackets(txclient_t* client)
{
    uint pktPtr = 0;
    uint8_t resolution = client->header.format_bit;
    uint8_t samplesize = VBanBitResolutionSize[resolution];
    uint pktNum = client->pktNum;
    uint packet;
    uint sample;
    uint nsamples = client->txBufSize/client->pktNum;
    uint smplPtr = 0;
    char* packets = client->txPackets;
    float* buffer = client->txBuffer;

    for (packet=0; packet<pktNum; packet++)
    {
        memcpy(&packets[pktPtr], (char*)&client->header, VBAN_HEADER_SIZE);
        pktPtr+= VBAN_HEADER_SIZE;
        for (sample=0; sample<nsamples; sample++)
        {
            convertSampleTX((uint8_t*)&packets[pktPtr], buffer[smplPtr+sample], resolution);
            //convertSampleTX(sampleparts, buffer[smplPtr+sample], vbanres);
            //memcpy((char*)&packets[packet][pktPtr], (char*)sampleparts, pktPtrStep);
            pktPtr+= samplesize;
        }
        smplPtr+= nsamples;
        inc_nuFrame(&client->header);
    }
}


void getRXPacket(rxclient_t* client, char* packet, uint16_t pktlen)
{
    uint16_t datasize;
    uint16_t rbAvailSize;

    datasize = pktlen-VBAN_HEADER_SIZE;
    rbAvailSize = jack_ringbuffer_write_space(client->rxBuffer);
    rbAvailSize = stripData(rbAvailSize, client->header.format_bit, (client->header.format_nbc+1));
    if (!(rbAvailSize<datasize))
        jack_ringbuffer_write(client->rxBuffer, &packet[VBAN_HEADER_SIZE], datasize);
    else
    {
        jack_ringbuffer_write(client->rxBuffer, &packet[VBAN_HEADER_SIZE], rbAvailSize);
        //while(jack_ringbuffer_write_space(rxClient.rxBuffer)<(datasize-rbAvailSize));
        //jack_ringbuffer_write(client->rxBuffer, &packet[VBAN_HEADER_SIZE+rbAvailSize], datasize-rbAvailSize);
    }
    //jack_ringbuffer_write(client->rxBuffer, packet, pktlen);
}


int txProcess(jack_nframes_t nframes, void *arg)
{
    txclient_t* client = ((txclient_t*)arg);
    static jack_default_audio_sample_t* buffers[VBAN_CHANNELS_MAX_NB];
    uint samplesize;
    uint frame;
    uint8_t framesize;
    uint channel;
    uint nchannels = 0;
    uint32_t index = 0;
    uint8_t red;
    uint packet;
    uint packetlen = VBAN_HEADER_SIZE + client->pktLen;
    uint pktPtr = 0;
    uint pnframes;
    uint framePtr = 0;
//    uint8_t ret;
    uint8_t sampleParts[8];
    uint overruns = 0;

    //clock_gettime(CLOCK_REALTIME, &client->ts);
    if (nframes!=client->audioBuffSize)
    {
        client->audioBuffSize = nframes;
        freeTxBuffers(client);
        createTxBuffers(client, nframes);
    }

    nchannels = client->header.format_nbc + 1;
    samplesize = VBanBitResolutionSize[client->header.format_bit];
    framesize = nchannels*samplesize;
    pnframes = client->txBufSize/client->pktNum;

    for (channel = 0; channel < nchannels; channel++)
        buffers[channel] = (jack_default_audio_sample_t*)jack_port_get_buffer(client->ports[channel], nframes);

    if (client->flags&MODE_TIMER)
    {
        for (frame=0; frame<nframes; frame++)
            if (!(jack_ringbuffer_write_space(client->txrBuffer)<framesize))
            {
                for (channel=0; channel<nchannels; channel++)
                {
                    convertSampleTX(sampleParts, (((jack_default_audio_sample_t*)buffers[channel])[frame]), client->header.format_bit);
                    if (jack_ringbuffer_write(client->txrBuffer, (char*)sampleParts, (size_t)samplesize) < samplesize) overruns++;
                }
            }
    }
    else
    {
        for (frame=0; frame<nframes; frame++)
            for (channel=0; channel<nchannels; channel++)
            {
                client->txBuffer[index] = buffers[channel][frame];
                index++;
            }

        client->flags|= READY_READ;
        /*if (pthread_mutex_trylock(&tx_read_thread_lock)==0)
        {
            pthread_cond_signal(&tx_data_ready);
            pthread_mutex_unlock(&tx_read_thread_lock);
        }//*/
        /*prepareTXPackets(client);
        for (packet=0; packet<client->pktNum; packet++)
        {
            for (red=0; red<(client->redundancy+1); red++)
                UDP_send(*client->audio_sd, client->audio_si, (uint8_t*)&client->txPackets[packet*packetlen], packetlen);
        }//*/

        for (packet=0; packet<client->pktNum; packet++)
        {
            memcpy(&client->txPackets[pktPtr], (char*)&client->header, VBAN_HEADER_SIZE);
            pktPtr+= VBAN_HEADER_SIZE;
            for (frame=0; frame<pnframes; frame++)
            {
                convertSampleTX((uint8_t*)&client->txPackets[pktPtr], client->txBuffer[framePtr+frame], client->header.format_bit);
                pktPtr+= samplesize;
            }

            for (red=0; red<(client->redundancy+1); red++)
                UDP_send(*client->audio_sd, client->audio_si, (uint8_t*)&client->txPackets[packet*packetlen], packetlen);

            framePtr+= pnframes;
            inc_nuFrame(&client->header);
        }

        client->flags&=~READY_READ;
    }

    return 0;
}


int rxProcess(jack_nframes_t nframes, void *arg)
{
    //uint8_t ret;
    rxclient_t* client = ((rxclient_t*)arg);
    static jack_default_audio_sample_t* buffers[VBAN_CHANNELS_MAX_NB];
    jack_ringbuffer_t* ringBuffer = client->rxBuffer;
    uint frame;
    uint channel;
    uint nchannels = 0;
    union
    {
        uint8_t sampleparts[8];
        uint64_t sp;
    };
    uint8_t resolution = client->header.format_bit;
    uint8_t samplesize = VBanBitResolutionSize[resolution];
    uint readspace;
    uint framesize;
    uint lostframes;

    if (nframes!=client->audioBuffSize)
    {
        client->audioBuffSize = nframes;
        freeRxBuffers(client);
        createRxBuffers(client, nframes);
        client->flags|= FLUSH_SOCKET;
    }

    nchannels = client->header.format_nbc + 1;
    framesize = nchannels*samplesize;

    for (channel = 0; channel < nchannels; channel++)
        buffers[channel] = (jack_default_audio_sample_t*)jack_port_get_buffer(client->ports[channel], nframes);

    lostframes = 0;
    for (frame=0; frame<nframes; frame++)
    {
        readspace = jack_ringbuffer_read_space(ringBuffer);
        if (readspace>=framesize)
        {
            jack_ringbuffer_read(client->rxBuffer, client->sparts, framesize);
            for (channel=0; channel<nchannels; channel++)
            {
                client->rxbuf[channel] = convertSampleRX((uint8_t*)&client->sparts[samplesize*channel], resolution);
            }
        }
        else
        {
            lostframes++;
            if (client->badPackets==32)
                for (channel=0; channel<nchannels; channel++)
                {
                    client->rxbuf[channel] = 0;
                }
        }
        for (channel=0; channel<nchannels; channel++) buffers[channel][frame] = client->rxbuf[channel];
    }
    if (lostframes>0)
    {
        if (client->badPackets<32)
        {
            client->badPackets++;
            fprintf(stderr, "Short read! Missing %d frames\n", lostframes);
        }
        //else exit(1);
    }
    else if (client->badPackets>0) client->badPackets--;

    return 0;
}


