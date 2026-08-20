#ifndef PHOTOBOOKBROWSERPLUGIN_H
#define PHOTOBOOKBROWSERPLUGIN_H

#include "pluginapi.h"
#include "scplugin.h"

class PhotoBookBrowserPalette;
class QAction;

class PhotoBookBrowserPlugin : public ScPersistentPlugin
{
    Q_OBJECT

public:
    PhotoBookBrowserPlugin();
    ~PhotoBookBrowserPlugin() override;

    QString fullTrName() const override;
    const AboutData* getAboutData() const override;
    void deleteAboutData(const AboutData* about) const override;
    void languageChange() override;
    void addToMainWindowMenu(ScribusMainWindow* mw) override;

    void setDoc(ScribusDoc* doc) override;
    void unsetDoc() override;
    void changedDoc(ScribusDoc* doc) override;

    bool initPlugin() override;
    bool cleanupPlugin() override;

private:
	ScribusDoc* m_Doc { nullptr };
    PhotoBookBrowserPalette* m_palette {nullptr};
    QAction* m_toggleAction {nullptr};
};

extern "C" PLUGIN_API int photobookbrowser_getPluginAPIVersion();
extern "C" PLUGIN_API ScPlugin* photobookbrowser_getPlugin();
extern "C" PLUGIN_API void photobookbrowser_freePlugin(ScPlugin* plugin);

#endif
