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

# Scribus 1.7.x is Qt 6 based. Thumbnail decoding uses QtConcurrent::run().
find_package(Qt6 REQUIRED COMPONENTS Concurrent)

target_link_libraries(${SCRIBUS_PHOTOBOOKBROWSER_PLUGIN}
	${EXE_NAME}
	Qt6::Concurrent
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
