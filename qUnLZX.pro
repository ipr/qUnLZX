#-------------------------------------------------
#
# Project created by QtCreator 2011-04-25T00:45:04
#
#-------------------------------------------------

QT       += core gui

TARGET = qUnLZX
TEMPLATE = app

DEFINES += _CRT_SECURE_NO_WARNINGS

SOURCES += main.cpp\
        mainwindow.cpp \
    UnLzx.cpp \
    AnsiFile.cpp \
    CrcSum.cpp

HEADERS  += mainwindow.h \
    UnLzx.h \
    AnsiFile.h \
    CrcSum.h

FORMS    += mainwindow.ui
