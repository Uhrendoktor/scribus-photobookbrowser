#ifndef PHOTOBOOKBROWSERPALETTE_H
#define PHOTOBOOKBROWSERPALETTE_H

#include <QDockWidget>

class QLabel;
class QLineEdit;
class QListView;
class QComboBox;
class QToolButton;
class QTimer;
class ScribusDoc;
class PhotoBookBrowserModel;
class PhotoBookBrowserFilterProxy;

class PhotoBookBrowserPalette : public QDockWidget
{
    Q_OBJECT
public:
    explicit PhotoBookBrowserPalette(QWidget* parent = nullptr);

    void setDocument(ScribusDoc* doc);
    void retranslateUi();

private slots:
    void chooseFolder();
    void refresh();
    void activate(const QModelIndex& index);
    void contextMenu(const QPoint& pos);
    void updateSummary();
    void statusFilterChanged(int index);
    void sortModeChanged(int index);
    void scanStarted();
    void scanFinished();
    void autoFillEmptyFrames();
    void scheduleLiveRefresh();
    void performLiveRefresh();

private:
    void insertImage(const QString& path, bool replaceSelected);
    void loadSettings();
    void saveFolderSetting(const QString& folder);

    QLineEdit* m_search {nullptr};
    QComboBox* m_filter {nullptr};
    QComboBox* m_sort {nullptr};
    QListView* m_view {nullptr};
    QLabel* m_summary {nullptr};
    QToolButton* m_autoFillButton {nullptr};
    PhotoBookBrowserModel* m_model {nullptr};
    PhotoBookBrowserFilterProxy* m_proxy {nullptr};
    ScribusDoc* m_doc {nullptr};
    QTimer* m_liveRefreshTimer {nullptr};
};

#endif
