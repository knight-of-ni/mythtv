######################################################################
# Automatically generated by qmake (1.02a) Wed Jul 24 19:17:01 2002
######################################################################

include ( ../settings.pro )
include (config.pro)

!exists( config.pro ) {
   error(Missing config.pro: please run the configure script)
}

TEMPLATE = lib
CONFIG += plugin thread
TARGET = mythmusic
target.path = $${PREFIX}/lib/mythtv/plugins
INSTALLS += target

installfiles.path = $${PREFIX}/share/mythtv
installfiles.files = musicmenu.xml music_settings.xml
uifiles.path = $${PREFIX}/share/mythtv/themes/default
uifiles.files = music-ui.xml images/*.png

INSTALLS += installfiles uifiles

LIBS += -lmad -lid3tag -logg -lvorbisfile -lvorbis -lvorbisenc -lcdaudio -lFLAC
LIBS += -lcdda_paranoia -lcdda_interface

# Input
HEADERS += audiooutput.h buffer.h cddecoder.h cdrip.h constants.h databasebox.h 
HEADERS += decoder.h flacdecoder.h flacencoder.h maddecoder.h mainvisual.h
HEADERS += metadata.h playbackbox.h playlist.h polygon.h output.h recycler.h 
HEADERS += streaminput.h synaesthesia.h encoder.h visualize.h
HEADERS += treecheckitem.h visual.h vorbisdecoder.h vorbisencoder.h polygon.h
HEADERS += bumpscope.h globalsettings.h
HEADERS += goom/filters.h goom/goomconfig.h goom/goom_core.h goom/graphic.h
HEADERS += goom/ifs.h goom/lines.h goom/mythgoom.h goom/drawmethods.h
HEADERS += goom/mmx.h goom/mathtools.h goom/tentacle3d.h goom/v3d.h

SOURCES += audiooutput.cpp cddecoder.cpp cdrip.cpp databasebox.cpp decoder.cpp 
SOURCES += flacdecoder.cpp flacencoder.cpp maddecoder.cpp main.cpp
SOURCES += mainvisual.cpp metadata.cpp playbackbox.cpp playlist.cpp output.cpp 
SOURCES += recycler.cpp streaminput.cpp encoder.cpp resample.c
SOURCES += synaesthesia.cpp treecheckitem.cpp vorbisdecoder.cpp 
SOURCES += vorbisencoder.cpp visualize.cpp bumpscope.cpp globalsettings.cpp
SOURCES += goom/filters.c goom/goom_core.c goom/graphic.c goom/tentacle3d.c
SOURCES += goom/ifs.c goom/ifs_display.c goom/lines.c goom/surf3d.c 
SOURCES += goom/zoom_filter_mmx.c goom/zoom_filter_xmmx.c goom/mythgoom.cpp
