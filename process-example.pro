DESTDIR = $$PWD/_bin
TEMPLATE = app
CONFIG -= qt
CONFIG += console
CONFIG += c++17

win32:INCLUDEPATH += winpty\include
win32:LIBS += -L$$PWD\winpty\x64\lib -lwinpty

win32:DEFINES += NOMINMAX

HEADERS += AbstractProcess.h \
	base64.h \
	misc.h
	
SOURCES += main.cpp \
	AbstractProcess.cpp \
	misc.cpp

!win32:HEADERS += \
	BasicProcessPosix.h

!win32:SOURCES += \
	BasicProcessPosix.cpp

win32:HEADERS += \
	BasicProcessWin.h \
	ProcessConPtyWithWorker.h \
	ProcessWin.h

win32:SOURCES += \
	BasicProcessWin.cpp \
	ProcessConPtyWithWorker.cpp \
	ProcessWin.cpp
