#-------------------------------------------------
#
# Project created by QtCreator 2014-06-14T16:04:07
#
#-------------------------------------------------

QT       -= gui

include($$PWD/../../SQLiteStudio3/plugins.pri)

TARGET = CsvExport
TEMPLATE = lib

DEFINES += CSVEXPORT_LIBRARY

SOURCES += csvexport.cpp

HEADERS += csvexport.h\
        csvexport_global.h

FORMS += \
    CsvExport.ui

OTHER_FILES += \
    csvexport.json

RESOURCES += \
    csvexport.qrc



TRANSLATIONS += CsvExport_de.ts \
		CsvExport_it.ts \
		CsvExport_zh_CN.ts \
		CsvExport_sk.ts \
		CsvExport_ru.ts \
		CsvExport_pt_BR.ts \
		CsvExport_fr.ts \
		CsvExport_es.ts \
		CsvExport_pl.ts















