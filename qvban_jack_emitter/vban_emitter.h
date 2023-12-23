#ifndef VBAN_EMITTER_H
#define VBAN_EMITTER_H

#include <QMainWindow>
#include <getopt.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>

#include "../vban_common/vbanJackClient.h"
#include "../vban_common/udpsocket.h"

enum autoconnect_modes
{
    NO = 0,
    YES,
    CARD
};

#define SYNC_MARK 'CNYS'
typedef union
{
    VBanHeader header;
    int32_t packet[9];
} timePacket_t;

typedef struct
{
    VBanHeader header;
    uint32_t ipaddr;
} receptormark;


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


QT_BEGIN_NAMESPACE
namespace Ui { class vban_emitter; }
QT_END_NAMESPACE

class vban_emitter : public QMainWindow
{
    Q_OBJECT

public:
    vban_emitter(QWidget *parent = nullptr);
    ~vban_emitter();

private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();
    void on_checkBox_2_clicked();

    void on_comboBox_3_currentIndexChanged(int index);

private:
    Ui::vban_emitter *ui;
    bool socketIsInited;
    receptormark receptorlist[255];
    uint32_t receptornum = 0;
};
#endif // VBAN_EMITTER_H
