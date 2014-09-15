#-------------------------------------------------
#
# Project created by QtCreator 2014-09-11T10:57:25
#
#-------------------------------------------------

include($$PWD/../../SQLiteStudio3/plugins.pri)

QT       -= gui

TARGET = SqlEnterpriseFormatter
TEMPLATE = lib

DEFINES += SQLENTERPRISEFORMATTER_LIBRARY

SOURCES += sqlenterpriseformatter.cpp \
    formatstatement.cpp \
    formatselect.cpp

HEADERS += sqlenterpriseformatter.h\
        sqlenterpriseformatter_global.h \
    formatstatement.h \
    formatselect.h

OTHER_FILES += \
    sqlenterpriseformatter.json

FORMS += \
    sqlenterpriseformatter.ui

RESOURCES += \
    sqlenterpriseformatter.qrc
