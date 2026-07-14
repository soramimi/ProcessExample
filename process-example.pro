DESTDIR = $$PWD/_bin
TEMPLATE = app
CONFIG -= qt
CONFIG += console

win32:INCLUDEPATH += winpty\include
win32:LIBS += -L$$PWD\winpty\x64\lib -lwinpty

win32:DEFINES += NOMINMAX

HEADERS += main.h \
	AbstractProcess.h \
	BasicProcessWin.h \
	ProcessWin.h \
	ProcessConPtyWithWorker.h \
	base64.h \
	misc.h
SOURCES += main.cpp \
	AbstractProcess.cpp \
	BasicProcessWin.cpp \
	ProcessWin.cpp \
	ProcessConPtyWithWorker.cpp \
	misc.cpp
