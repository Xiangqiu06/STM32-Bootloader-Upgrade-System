QT       += core gui widgets serialport
CONFIG   += c++11
TARGET    = fw_upgrade_tool
TEMPLATE  = app

SOURCES += \
    src/main.cpp \
    src/hexloader.cpp \
    src/mainwindow.cpp \
    src/protocol.cpp \
    src/transport_serial.cpp \
    src/transport_can.cpp \
    src/upgradesession.cpp

HEADERS += \
    src/hexloader.h \
    src/mainwindow.h \
    src/protocol.h \
    src/transport.h \
    src/transport_serial.h \
    src/transport_can.h \
    src/transport_net.h \
    src/upgradesession.h
