#ifndef PHOTOBOOKBROWSERMODEL_H
#define PHOTOBOOKBROWSERMODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QSet>
#include <QString>
#include <QVector>

class ScribusDoc;
class PageItem;
class PhotoBookThumbnailLoader;
class QImage;

struct PhotoBookEntry
{
    QString path;
    QDateTime modified;
    QSet<int> pages;
    QVector<PageItem*> frames;
    QIcon icon;
    bool iconRequested { false };

    bool used() const { return !frames.isEmpty(); }
};

// Model backing the thumbnail grid. Owns the folder scan, the async
// thumbnail cache and the "which images are placed in the document"
// bookkeeping. Nothing in here touches Scribus UI classes directly except
// PageItem, so it stays easy to unit-test/reuse outside the palette.
class PhotoBookBrowserModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        PathRole = Qt::UserRole + 1,
        UsedRole,
        PagesRole,
        PlacementsRole,
        ModifiedRole,
        FileNameRole
    };

    explicit PhotoBookBrowserModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

    void setFolder(const QString& folder);
    QString folder() const { return m_folder; }
    void scan();
    void updateUsage(ScribusDoc* doc);

    const PhotoBookEntry* entry(int row) const;

    // How many images currently have zero placements in the document.
    int unusedCount() const;

signals:
    void usageUpdated();
    void scanStarted();
    void scanFinished();

private slots:
    void applyScanResult(const QStringList& files);
    void applyThumbnail(const QString& path, const QImage& image);
    void watchedDirectoryChanged(const QString& dir);

private:
    static QString keyForPath(const QString& path);
    void requestThumbnail(int row) const;
    void rewatchFolders(const QStringList& directories);

    QString m_folder;
    QVector<PhotoBookEntry> m_entries;
    QHash<QString, int> m_rowForPath;
    QFileSystemWatcher m_watcher;
    PhotoBookThumbnailLoader* m_thumbLoader {nullptr};
    ScribusDoc* m_doc {nullptr};
    bool m_scanning {false};
};

#endif
