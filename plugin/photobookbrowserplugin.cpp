#include "photobookbrowserplugin.h"
#include "photobookbrowserpalette.h"

#include "scribus.h"
#include "scribusapp.h"
#include "ui/scmwmenumanager.h"

#include <QAction>
#include <QDateTime>
#include <QMenu>

PhotoBookBrowserPlugin::PhotoBookBrowserPlugin() = default;

PhotoBookBrowserPlugin::~PhotoBookBrowserPlugin()
{
    delete m_palette;
    m_palette = nullptr;
}

QString PhotoBookBrowserPlugin::fullTrName() const
{
    return QObject::tr("PhotoBook Browser");
}

const ScPlugin::AboutData* PhotoBookBrowserPlugin::getAboutData() const
{
    auto* about = new AboutData;
    about->authors = QStringLiteral("PhotoBook Browser contributors");
    about->shortDescription = QObject::tr("Live photo browser for Scribus photobooks");
    about->description = QObject::tr(
        "Shows images from a selected folder, marks images used by the current "
        "Scribus document, and allows images to be inserted, dragged onto the "
        "page, or used to replace selected image frames."
    );
    about->version = QStringLiteral("0.1.0");
    about->releaseDate = QDateTime(QDate(2026, 8, 20), QTime(0, 0));
    about->copyright = QStringLiteral("Copyright (C) 2026 <Uhrendoktor>");
    about->license = QStringLiteral("GPL-2.0-or-later");
    return about;
}

void PhotoBookBrowserPlugin::deleteAboutData(const AboutData* about) const
{
    delete about;
}

void PhotoBookBrowserPlugin::languageChange()
{
    if (m_palette)
        m_palette->retranslateUi();
    if (m_toggleAction)
        m_toggleAction->setText(fullTrName());
}

void PhotoBookBrowserPlugin::addToMainWindowMenu(ScribusMainWindow* mw)
{
    if (!m_palette)
        m_palette = new PhotoBookBrowserPalette(mw);

    mw->addDockWidget(Qt::RightDockWidgetArea, m_palette);

    m_palette->hide();

    if (m_Doc)
        m_palette->setDocument(m_Doc);

    m_toggleAction = m_palette->toggleViewAction();
    m_toggleAction->setText(fullTrName());
    m_toggleAction->setStatusTip(
        QObject::tr("Show or hide the PhotoBook Browser panel")
    );

    QMenu *windowsMenu = mw->scrMenuMgr->getLocalPopupMenu("Windows");
    if (windowsMenu)
        windowsMenu->addAction(m_toggleAction);
}

void PhotoBookBrowserPlugin::setDoc(ScribusDoc* doc)
{
    m_Doc = doc;
    if (m_palette)
        m_palette->setDocument(doc);
}

void PhotoBookBrowserPlugin::unsetDoc()
{
    m_Doc = nullptr;
    if (m_palette)
        m_palette->setDocument(nullptr);
}

void PhotoBookBrowserPlugin::changedDoc(ScribusDoc* doc)
{
    m_Doc = doc;
    if (m_palette)
        m_palette->setDocument(doc);
}

bool PhotoBookBrowserPlugin::initPlugin()
{
    return true;
}

bool PhotoBookBrowserPlugin::cleanupPlugin()
{
    if (m_palette)
        m_palette->hide();
    return true;
}

extern "C" PLUGIN_API int photobookbrowser_getPluginAPIVersion()
{
    return PLUGIN_API_VERSION;
}

extern "C" PLUGIN_API ScPlugin* photobookbrowser_getPlugin()
{
    return new PhotoBookBrowserPlugin;
}

extern "C" PLUGIN_API void photobookbrowser_freePlugin(ScPlugin* plugin)
{
    delete plugin;
}
