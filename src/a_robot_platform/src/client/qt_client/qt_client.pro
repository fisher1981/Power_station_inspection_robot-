#-------------------------------------------------
#
# Project created by QtCreator 2016-12-25T16:02:02
#
#-------------------------------------------------

QT       += core \
            gui \
            network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets  printsupport

TARGET = qt_client
TEMPLATE = app

QMAKE_CXXFLAGS += -std=c++0x
CONFIG += c++11


SOURCES += main.cpp\
        mainwindow.cpp \
    ../key_control.cpp \
    ../tcp_socket.cpp \
    ../../common/paras.cpp \
    ../../common/modbus.cpp \
    ../../common/map_image.cpp \
    ../udp_socket.cpp \
    ../../server/tcpsocketserver.cpp \
    ../../nav/nav.cpp


HEADERS  += mainwindow.h \
    ../key_control.h \
    ../tcp_socket.h \
    ../../common/common.h \
    ../../common/paras.h \
    ../../common/modbus.h\
    ../../common/use_display.h \
    ../../common/map_image.h \
    ../udp_socket.h \
    ../../server/uartlaser.h \
    ../../server/uartdriver.h \
    ../../server/tcpsocketserver.h \
    ../../nav/nav.h


FORMS    += mainwindow.ui

INCLUDEPATH += /opt/ros/kinetic/include

INCLUDEPATH += /usr/local/include \
               /usr/local/include/opencv \
               /usr/local/inlcude/opencv2 \

LIBS += -L/usr/local/lib/ -lopencv_highgui -lopencv_core -lopencv_imgproc \
                          -lopencv_legacy -lopencv_features2d -lopencv_nonfree \
                          -lopencv_flann

