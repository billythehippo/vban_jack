QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ../vban_common/udpsocket.cpp \
    ../vban_common/vbanJackClient.cpp \
    main.cpp \
    vban_emitter.cpp

HEADERS += \
    ../vban_common/udpsocket.h \
    ../vban_common/vban.h \
    ../vban_common/vbanJackClient.h \
    ../vban_common/vban_functions.h \
    vban_emitter.h

FORMS += \
    vban_emitter.ui

LIBS += -lpthread -ljack

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
