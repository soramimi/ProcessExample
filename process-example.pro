DESTDIR = $$PWD/_bin
TEMPLATE = app
CONFIG -= qt
CONFIG += console

# win32:INCLUDEPATH += winpty\include
# win32:LIBS += -L$$PWD\winpty\x64\lib -lwinpty

HEADERS += main.h \
	WinProcess.h \
	base64.h \
	misc.h
SOURCES += main.cpp \
	WinProcess.cpp \
	misc.cpp
