#include "vban_receptor.h"
#include "ui_vban_receptor.h"

bool enabled = false;

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
uint8_t threadIsEnabled = 1;

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
    uint16_t pktlen;
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
    while(threadIsEnabled) // Thread is enabled cond
    {
        while(threadIsEnabled&&poll(&pds, 1, 100))
        {
            clock_gettime(CLOCK_REALTIME, &client.ts);
            pktlen = UDP_recv(audio_sd, &audio_si, (uint8_t*)&packet, VBAN_PROTOCOL_MAX_SIZE);
            if (pktlen)
            {
                ip = int32betole(audio_si.sin_addr.s_addr);
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
                            if (client.flags&AUTOCONNECT) jackRXConnect(&client);
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
                                if (client.flags&AUTOCONNECT) jackRXConnect(&client);
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
    pthread_exit(NULL);
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


vban_receptor::vban_receptor(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::vban_receptor)
{
    ui->setupUi(this);
    timer = new QTimer();
    connect(timer, SIGNAL(timeout()), this, SLOT(timerHandler()));
    timer->start(1000);
}


vban_receptor::~vban_receptor()
{
    delete ui;
}


void vban_receptor::timerHandler()
{
    QString str;
    if (client.header.streamname[0]!=0)
    {
        if (ui->lineEdit->text().isEmpty()) ui->lineEdit->setText(client.header.streamname);
        str.clear();
        if (client.badPackets==32)
        {
            str.append("Disconnected");
        }
        else
        {
            str.append(client.header.streamname);
            str.append(", ");
            str+=(QString::number(client.header.format_nbc+1));
            str.append(" channels, ");
            switch (client.header.format_bit)
            {
            case 1:
                str.append("16bit\n");
                break;
            case 2:
                str.append("24bit\n");
                break;
            case 4:
                str.append("32bit\n");
                break;
            default: break;
            }
            str+= QString::number(client.ipBytes[3]) + '.' + QString::number(client.ipBytes[2]) + '.' + QString::number(client.ipBytes[1]) + '.' + QString::number(client.ipBytes[0]) + ':' + QString::number(client.udpRxPort);
        }
        ui->label_6->setText(str);
    }
}


void vban_receptor::on_pushButton_clicked()
{
    char jackClientName[32];
    union
    {
        uint32_t ip;
        uint8_t ipbytes[4];
    };

    if (enabled)
    {
        enabled = false;
        //ui->lineEdit->setEnabled(true);
        ui->lineEdit->setReadOnly(false);
        ui->lineEdit->setText(NULL);
        ui->checkBox->setEnabled(true);
        ui->checkBox_2->setEnabled(true);
        ui->comboBox->setEnabled(true);
        if (ui->checkBox_2->isChecked()) ui->comboBox_2->setEnabled(true);
        ui->spinBox_2->setEnabled(true);
        ui->spinBox_3->setEnabled(true);
        ui->spinBox_4->setEnabled(true);
        ui->spinBox_5->setEnabled(true);
        ui->spinBox_6->setEnabled(true);
        ui->pushButton->setText("Start");

        //UDP_deinit(audio_sd);
        threadIsEnabled = 0;
        pthread_join(rxtid, NULL);
        deleteRxClient(&client);
    }
    else
    {
        enabled = true;
        //ui->lineEdit->setEnabled(false);
        ui->lineEdit->setReadOnly(true);
        ui->checkBox->setEnabled(false);
        ui->checkBox_2->setEnabled(false);
        ui->comboBox->setEnabled(false);
        ui->comboBox_2->setEnabled(false);
        ui->spinBox_2->setEnabled(false);
        ui->spinBox_3->setEnabled(false);
        ui->spinBox_4->setEnabled(false);
        ui->spinBox_5->setEnabled(false);
        ui->spinBox_6->setEnabled(false);
        ui->pushButton->setText("Stop");

        threadIsEnabled = 1;
        init_defaults(&config, &client);
        if (config.IPaddr[0]==0) strcpy(config.IPaddr, "0.0.0.0");

        config.IPbytes[3] = ui->spinBox_2->text().toUInt();
        config.IPbytes[2] = ui->spinBox_3->text().toUInt();
        config.IPbytes[1] = ui->spinBox_4->text().toUInt();
        config.IPbytes[0] = ui->spinBox_5->text().toUInt();

        memset(config.IPaddr, 0, 16);
        sprintf(config.IPaddr, "%s.%s.%s.%s", ui->spinBox_2->text().toStdString().c_str(), ui->spinBox_3->text().toStdString().c_str(), ui->spinBox_4->text().toStdString().c_str(), ui->spinBox_5->text().toStdString().c_str());

        config.port = ui->spinBox_6->text().toUInt();
        client.udpPort = config.port;

        memcpy(config.streamname, ui->lineEdit->text().toStdString().c_str(), 16);

        if (client.header.format_bit>1) client.header.format_bit++;

        client.netquality = ui->comboBox->currentText().toUInt();

        if (ui->checkBox_2->isChecked())
        {
            client.flags|= MODE_TIMER;
            client.rxSamples = ui->comboBox_2->currentText().toUInt();
        }
        else client.flags&=~MODE_TIMER;

        if (ui->checkBox->isChecked())
        {
            for (uint16_t i=0; i<256; i++) client.map[i] = i;
            client.flags|= AUTOCONNECT;
        }

        if (audio_sd<=0)
        {
            audio_sd = UDP_init(&audio_sd, &audio_si, config.IPaddr, client.udpPort, socket_mode, 1, 6);
            if (audio_sd<0)
            {
                fprintf(stderr, "Can't bind the socket!\n");
                QMessageBox::warning(this, "Warning!","Can't bind the socket!\nMaybe, it is already in use...");
            }
            else
            {
                fprintf(stderr, "Socket created, %d\n", audio_sd);

                pds.fd = audio_sd;
                pds.events = POLLIN;
                pthread_attr_init(&attr);
                pthread_create(&rxtid, &attr, rxThread, NULL);

                while(poll(&pds, 1, 0)) // Flush socket RX buffer with SYNC control
                {
                    UDP_recv(audio_sd, &audio_si, NULL, VBAN_PROTOCOL_MAX_SIZE);
                }
            }
        }

    }
}


void vban_receptor::on_checkBox_2_stateChanged(int arg1)
{
    if (ui->checkBox_2->isChecked()) ui->comboBox_2->setEnabled(true);
    else ui->comboBox_2->setEnabled(false);
}
