DESTDIR = $$PWD/_bin
TEMPLATE = app
CONFIG -= qt
CONFIG += console

win32:INCLUDEPATH += winpty\include
win32:LIBS += -L$$PWD\winpty\x64\lib -lwinpty

HEADERS += main.h \
	AbstractProcess.h \
	WinProcess.h \
	ProcessWin.h \
	ProcessConPtyWithWorker.h \
	base64.h \
	misc.h
SOURCES += main.cpp \
	AbstractProcess.cpp \
	WinProcess.cpp \
	ProcessWin.cpp \
	ProcessConPtyWithWorker.cpp \
	misc.cpp
