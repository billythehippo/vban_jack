#include "vban_emitter.h"
#include "ui_vban_emitter.h"

//#include "../vban_common/vbanJackClient.h"
//#include "../vban_common/udpsocket.h"

bool enabled = false;

txclient_t client;
uint8_t autoconnect = 0;

static int audio_sd; //socket descriptor
struct sockaddr_in audio_si; //socket
char socket_mode = 'c';
uint16_t txport = 0;

struct pollfd pds;
struct pollfd ids;

config_t config;
uint8_t mapIsSet = 0;

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

pthread_t synctid;
pthread_attr_t syncattr;
pthread_mutex_t sync_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  sync_ready = PTHREAD_COND_INITIALIZER;

pthread_t sendtid;
pthread_attr_t sendattr;
pthread_mutex_t send_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  send_ready = PTHREAD_COND_INITIALIZER;
uint8_t threadIsEnabled = 1;

timePacket_t timePacket;

#define DTARRSIZE 50
#define DTARRLAST DTARRSIZE-1
int64_t dtarr[DTARRSIZE];
int16_t dtind = 0;
int32_t tnew;
int32_t told;
int64_t dt;
float dtf;


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
}


void* syncThread(void* param)
{
    while(enabled) // Thread is enabled cond
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
    pthread_exit(NULL);
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

    while(enabled) // Thread is enabled cond
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
    pthread_exit(NULL);
}


vban_emitter::vban_emitter(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::vban_emitter)
{
    ui->setupUi(this);
}

vban_emitter::~vban_emitter()
{
    delete ui;
}

void vban_emitter::on_pushButton_clicked()
{
    uint8_t red;
    uint packet;
    uint packetlen;

    if (enabled)
    {
        ui->lineEdit->setEnabled(true);
        ui->checkBox->setEnabled(true);
        ui->checkBox_2->setEnabled(true);
        ui->comboBox->setEnabled(true);
        if (ui->checkBox_2->isChecked()) ui->comboBox_2->setEnabled(true);
        ui->spinBox->setEnabled(true);
        ui->spinBox_2->setEnabled(true);
        ui->spinBox_3->setEnabled(true);
        ui->spinBox_4->setEnabled(true);
        ui->spinBox_5->setEnabled(true);
        ui->spinBox_6->setEnabled(true);
        ui->pushButton->setText("Start");

        enabled = false;
        pthread_join(sendtid, NULL);
        pthread_join(synctid, NULL);
//        pthread_exit(NULL);
        deleteTxClient(&client);
    }
    else
    {
        enabled = true;
        ui->lineEdit->setEnabled(false);
        ui->checkBox->setEnabled(false);
        ui->checkBox_2->setEnabled(false);
        ui->comboBox->setEnabled(false);
        ui->comboBox_2->setEnabled(false);
        ui->spinBox->setEnabled(false);
        ui->spinBox_2->setEnabled(false);
        ui->spinBox_3->setEnabled(false);
        ui->spinBox_4->setEnabled(false);
        ui->spinBox_5->setEnabled(false);
        ui->spinBox_6->setEnabled(false);
        ui->pushButton->setText("Stop");

        init_defaults(&config, &client);

        config.IPbytes[3] = ui->spinBox_2->text().toUInt();
        config.IPbytes[2] = ui->spinBox_3->text().toUInt();
        config.IPbytes[1] = ui->spinBox_4->text().toUInt();
        config.IPbytes[0] = ui->spinBox_5->text().toUInt();

        memset(config.IPaddr, 0, 16);
        strcat(config.IPaddr, ui->spinBox_2->text().toStdString().c_str());
        strcat(config.IPaddr, ".");
        strcat(config.IPaddr, ui->spinBox_3->text().toStdString().c_str());
        strcat(config.IPaddr, ".");
        strcat(config.IPaddr, ui->spinBox_4->text().toStdString().c_str());
        strcat(config.IPaddr, ".");
        strcat(config.IPaddr, ui->spinBox_5->text().toStdString().c_str());

        config.port = ui->spinBox_6->text().toUInt();

        memcpy(config.streamname, ui->lineEdit->text().toStdString().c_str(), 16);

        config.nbchannels = ui->spinBox->text().toUInt();

        config.VBANResolution = ui->comboBox->currentIndex() + 1;
        if (config.VBANResolution>2) config.VBANResolution++;

        if (ui->checkBox_2->isChecked())
        {
            client.flags|= MODE_TIMER;
            client.maxSamplesPerPacket = ui->comboBox_2->currentText().toUInt();
        }
        else client.flags&=~MODE_TIMER;

        if (ui->checkBox->isChecked())
        {
            for (uint16_t i=0; i<256; i++) client.map[i] = i;
            client.flags|= AUTOCONNECT;
        }

        // int socket
        if (socketIsInited) UDP_deinit(audio_sd);

        audio_sd = UDP_init(&audio_sd, &audio_si, config.IPaddr, config.port, socket_mode, 1, 6);
        if (audio_sd<0)
        {
            fprintf(stderr, "Can't init the socket!\n");
        }
        else
        {
            socketIsInited = true;
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
            if (client.flags&AUTOCONNECT) jackTXConnect(&client);
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
        }
    }
}


void vban_emitter::on_pushButton_2_clicked()
{
    VBanPacket request;
    receptormark rmark;
    static int ireq_sd;
    static int info_sd; //socket descriptor
    struct sockaddr_in ireq_si;
    struct sockaddr_in info_si; //socket
    uint32_t rnum;
    QString itemname;
    char ipstr[16];

    union
    {
        VBanHeader infoheader;
        char infobytes[32];
    };

    config.port = ui->spinBox_6->text().toUInt();

    ireq_sd = UDP_init(&ireq_sd, &ireq_si, "0.0.0.0", config.port, 'c', 1, 3);

    request.header.vban = VBAN_HEADER_FOURC;
    request.header.format_SR = VBAN_PROTOCOL_TXT;
    request.header.format_nbs = 0xFF;
    request.header.format_nbc = 0xFF;
    request.header.format_bit = 0;
    request.header.nuFrame = 0;
    memset(request.header.streamname, 0, 16);
    strcat(request.header.streamname, "INFOREQUEST");


    UDP_send(ireq_sd, &ireq_si, (uint8_t*)&request, sizeof(VBanHeader));
    info_sd = UDP_init(&info_sd, &info_si, "0.0.0.0", ireq_si.sin_port, 'c', 1, 3);
    ids.fd = info_sd;
    ids.events = POLLIN;

//    UDP_send_datagram(0xFFFFFFFF, htons(config.port), (uint8_t*)&request, sizeof(VBanHeader), 3);

    while(poll(&ids, 1, 100))
    {
        UDP_recv(ids.fd, &info_si, (uint8_t*)&infobytes, 32);
        UDP_deinit(info_sd);
        rmark.header = infoheader;
        rmark.header.format_SR = VBAN_PROTOCOL_TXT;
        rmark.header.format_nbs = 0xFF;
        rmark.header.format_nbc = 0xFF;
        rmark.header.format_bit = 0;
        rmark.ipaddr = info_si.sin_addr.s_addr;
        itemname.clear();
        itemname.append(rmark.header.streamname);
        itemname.append(", ");
        memset(ipstr, 0, 16);
        sprintf(ipstr, "%hhu.%hhu.%hhu.%hhu", ((uint8_t*)&rmark.ipaddr)[0], ((uint8_t*)&rmark.ipaddr)[1], ((uint8_t*)&rmark.ipaddr)[2], ((uint8_t*)&rmark.ipaddr)[3]);
        itemname.append(ipstr);
        if (receptornum==0)
        {
            receptorlist[receptornum] = rmark;
            receptornum++;
            ui->comboBox_3->addItem(itemname);
            itemname.clear();
            ui->comboBox_3->setCurrentIndex(0);
        }
        else
        {
            rnum = 0;
            while(rnum<receptornum)
            {
                if(!(memcmp((uint8_t*)&receptorlist[rnum], (uint8_t*)&rmark, sizeof(receptormark)))) break;
                else rnum++;
            }
            if (receptornum==rnum)
            {
                receptorlist[receptornum] = rmark;
                receptornum++;
                ui->comboBox_3->addItem(itemname);
            }
        }
    }
}

void vban_emitter::on_checkBox_2_clicked()
{
    if (ui->checkBox_2->isChecked()) ui->comboBox_2->setEnabled(true);
    else ui->comboBox_2->setEnabled(false);
}



void vban_emitter::on_comboBox_3_currentIndexChanged(int index)
{
    union
    {
        uint32_t ipaddr;
        uint8_t  ipbytes[4];
    };
    ipaddr = receptorlist[index].ipaddr;
    if (ui->comboBox_3->currentIndex()>=0)
    {
        ui->spinBox_2->setValue(ipbytes[0]);
        ui->spinBox_3->setValue(ipbytes[1]);
        ui->spinBox_4->setValue(ipbytes[2]);
        ui->spinBox_5->setValue(ipbytes[3]);
    }
}

