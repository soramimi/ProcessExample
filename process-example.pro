DESTDIR = $$PWD/_bin
TEMPLATE = app
CONFIG -= qt
CONFIG += console

win32:INCLUDEPATH += winpty\include
win32:LIBS += -L$$PWD\winpty\x64\lib -lwinpty

win32:DEFINES += NOMINMAX

HEADERS += main.h \
	AbstractProcess.h \
	BasicProcessPosix.h \
	base64.h \
	misc.h
	
SOURCES += main.cpp \
	AbstractProcess.cpp \
	BasicProcessPosix.cpp \
	misc.cpp

win32:HEADERS += \
	BasicProcessWin.h \
	ProcessConPtyWithWorker.h \
	ProcessWin.h

win32:SOURCES += \
	BasicProcessWin.cpp \
	ProcessConPtyWithWorker.cpp \
	ProcessWin.cpp
