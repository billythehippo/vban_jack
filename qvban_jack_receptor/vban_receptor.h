#ifndef VBAN_RECEPTOR_H
#define VBAN_RECEPTOR_H

#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>
#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>

#include "../vban_common/vbanJackClient.h"
#include "../vban_common/udpsocket.h"


QT_BEGIN_NAMESPACE
namespace Ui { class vban_receptor; }
QT_END_NAMESPACE

class vban_receptor : public QMainWindow
{
    Q_OBJECT

public:
    vban_receptor(QWidget *parent = nullptr);
    ~vban_receptor();

private slots:
    void timerHandler();
    void on_checkBox_2_stateChanged(int arg1);
    void on_pushButton_clicked();

private:
    Ui::vban_receptor *ui;
    QTimer* timer;
};
#endif // VBAN_RECEPTOR_H
