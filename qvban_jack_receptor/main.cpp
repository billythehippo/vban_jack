#include "vban_receptor.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    vban_receptor w;
    w.show();
    return a.exec();
}
