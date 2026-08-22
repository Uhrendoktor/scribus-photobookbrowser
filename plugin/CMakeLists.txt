include_directories(
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/scribus
)

set(PHOTOBOOKBROWSER_PLUGIN_SOURCES
	photobookbrowserplugin.cpp
	photobookbrowserpalette.cpp
	photobookbrowsermodel.cpp
	photobookbrowserdelegate.cpp
	photobookbrowserfilterproxy.cpp
	photobookbrowserthumbnailloader.cpp
)

set(SCRIBUS_PHOTOBOOKBROWSER_PLUGIN "photobookbrowser")

add_library(${SCRIBUS_PHOTOBOOKBROWSER_PLUGIN} MODULE
	${PHOTOBOOKBROWSER_PLUGIN_SOURCES}
)

# Thumbnail decoding runs on QtConcurrent::run(). Scribus 1.6.x uses Qt 5,
# while Scribus 1.7.x uses Qt 6. Select the Qt major version already used by
# the Scribus build instead of hard-coding one series.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent)

target_link_libraries(${SCRIBUS_PHOTOBOOKBROWSER_PLUGIN}
	${EXE_NAME}
	Qt${QT_VERSION_MAJOR}::Concurrent
)

if(WANT_PCH)
	target_precompile_headers(
		${SCRIBUS_PHOTOBOOKBROWSER_PLUGIN}
		PRIVATE "../plugins_pch.h"
	)
endif()

install(
	TARGETS ${SCRIBUS_PHOTOBOOKBROWSER_PLUGIN}
	LIBRARY
	DESTINATION ${PLUGINDIR}
	PERMISSIONS ${PLUGIN_PERMISSIONS}
)
