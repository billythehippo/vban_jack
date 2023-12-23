#include "vban_emitter.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    vban_emitter w;
    w.show();
    return a.exec();
}
