#include "photobookbrowsermodel.h"
#include "photobookbrowserthumbnailloader.h"

#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "scribusdoc.h"
#include "scpage.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QtConcurrentRun>

namespace
{
bool supportedImage(const QString& suffix)
{
    static const QSet<QString> exts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("jp2"),
        QStringLiteral("j2k")
    };
    return exts.contains(suffix.toLower());
}

void inspectItem(PageItem* item,
                 QHash<QString, PhotoBookEntry*>& usage,
                 int page)
{
    if (!item)
        return;

    if (item->isImageFrame())
    {
        auto* frame = item->asImageFrame();
        if (frame && !frame->Pfile.isEmpty())
        {
            const QString key = QFileInfo(frame->Pfile).canonicalFilePath().isEmpty()
                ? QFileInfo(frame->Pfile).absoluteFilePath()
                : QFileInfo(frame->Pfile).canonicalFilePath();

            auto it = usage.find(key);
            if (it != usage.end())
            {
                it.value()->pages.insert(page);
                it.value()->frames.append(frame);
            }
        }
    }

    if (item->isGroup())
    {
        const QList<PageItem*> children = item->asGroupFrame()->groupItemList;
        for (PageItem* child : children)
            inspectItem(child, usage, page);
    }
}
}

PhotoBookBrowserModel::PhotoBookBrowserModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &PhotoBookBrowserModel::watchedDirectoryChanged);

    m_thumbLoader = new PhotoBookThumbnailLoader(this);
    connect(m_thumbLoader, &PhotoBookThumbnailLoader::thumbnailReady,
            this, &PhotoBookBrowserModel::applyThumbnail);
}

int PhotoBookBrowserModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PhotoBookBrowserModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const auto& e = m_entries.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return QFileInfo(e.path).fileName();
    case Qt::DecorationRole:
        if (!e.icon.isNull())
            return e.icon;
        // Kick off async decoding the first time this row is actually
        // requested by the view rather than for every file up front - the
        // grid still fills in live, but scrolling a huge folder doesn't
        // queue thousands of decodes that are never seen.
        requestThumbnail(index.row());
        return QIcon();
    case Qt::ToolTipRole:
    {
        QString tip = e.path;
        if (e.used())
        {
            QStringList pages;
            for (int p : e.pages)
                pages << QString::number(p);
            std::sort(pages.begin(), pages.end(), [](const QString& a, const QString& b) {
                return a.toInt() < b.toInt();
            });
            tip += tr("\nUsed on page(s): %1 (%2 placement(s))")
                       .arg(pages.join(QStringLiteral(", ")))
                       .arg(e.frames.size());
        }
        else
        {
            tip += tr("\nNot placed yet");
        }
        return tip;
    }
    case PathRole:
        return e.path;
    case UsedRole:
        return e.used();
    case PagesRole:
    {
        QStringList pages;
        for (int p : e.pages)
            pages << QString::number(p);
        std::sort(pages.begin(), pages.end(), [](const QString& a, const QString& b) {
            return a.toInt() < b.toInt();
        });
        return pages.join(QStringLiteral(", "));
    }
    case PlacementsRole:
        return e.frames.size();
    case ModifiedRole:
        return e.modified;
    case FileNameRole:
        return QFileInfo(e.path).fileName();
    default:
        return {};
    }
}

Qt::ItemFlags PhotoBookBrowserModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList PhotoBookBrowserModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData* PhotoBookBrowserModel::mimeData(const QModelIndexList& indexes) const
{
    // Exporting plain file:// URLs (rather than a private mime type) is
    // what lets an image be dropped straight onto the Scribus page/canvas:
    // Scribus's own canvas drop handling already turns a dropped image
    // file into a picture frame at the drop position, so the browser just
    // needs to behave like a normal drag source.
    QList<QUrl> urls;
    for (const QModelIndex& idx : indexes)
    {
        if (idx.isValid())
            urls << QUrl::fromLocalFile(idx.data(PathRole).toString());
    }

    auto* mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

Qt::DropActions PhotoBookBrowserModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

QString PhotoBookBrowserModel::keyForPath(const QString& path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

void PhotoBookBrowserModel::setFolder(const QString& folder)
{
    const QString key = keyForPath(folder);
    if (key == m_folder)
        return;

    m_folder = key;
    scan();
}

void PhotoBookBrowserModel::scan()
{
    if (m_folder.isEmpty())
    {
        beginResetModel();
        m_entries.clear();
        m_rowForPath.clear();
        endResetModel();
        rewatchFolders({});
        emit usageUpdated();
        return;
    }

    if (m_scanning)
        return;
    m_scanning = true;
    emit scanStarted();

    const QString folder = m_folder;
    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher]() {
        applyScanResult(watcher->result());
        watcher->deleteLater();
    });

    // Walking the directory tree and stat-ing every file is the slow part
    // for large photo libraries, so it happens off the GUI thread. The
    // model applies the result back on the main thread in one shot.
    watcher->setFuture(QtConcurrent::run([folder]() -> QStringList {
        QStringList files;
        QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString p = it.next();
            if (supportedImage(QFileInfo(p).suffix()))
                files << p;
        }
        return files;
    }));
}

void PhotoBookBrowserModel::applyScanResult(const QStringList& files)
{
    // Keep already-decoded thumbnails across a rescan so re-plugging a USB
    // stick or re-running "Refresh" doesn't make the whole grid flicker
    // back to placeholders.
    QHash<QString, QIcon> previousIcons;
    for (const auto& e : m_entries)
    {
        if (!e.icon.isNull())
            previousIcons.insert(e.path, e.icon);
    }

    beginResetModel();
    m_entries.clear();
    m_rowForPath.clear();

    QSet<QString> directories;
    directories.insert(m_folder);

    for (const QString& p : files)
    {
        PhotoBookEntry e;
        e.path = keyForPath(p);
        e.modified = QFileInfo(p).lastModified();
        if (previousIcons.contains(e.path))
            e.icon = previousIcons.value(e.path);
        m_entries.append(e);
        directories.insert(QFileInfo(p).absolutePath());
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const auto& a, const auto& b) {
        return a.path.toCaseFolded() < b.path.toCaseFolded();
    });

    for (int i = 0; i < m_entries.size(); ++i)
        m_rowForPath.insert(m_entries.at(i).path, i);

    endResetModel();

    rewatchFolders(directories.values());

    m_scanning = false;
    emit scanFinished();

    // Re-apply usage against whatever document is currently tracked so a
    // rescan doesn't momentarily show everything as unused.
    updateUsage(m_doc);
}

void PhotoBookBrowserModel::rewatchFolders(const QStringList& directories)
{
    const QStringList current = m_watcher.directories();
    if (!current.isEmpty())
        m_watcher.removePaths(current);
    if (!directories.isEmpty())
        m_watcher.addPaths(directories);
}

void PhotoBookBrowserModel::watchedDirectoryChanged(const QString&)
{
    // Any add/remove/rename anywhere under the watched tree re-triggers a
    // (background) rescan so the grid reflects what's on disk live,
    // without the user having to press "Refresh" by hand.
    scan();
}

void PhotoBookBrowserModel::requestThumbnail(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return;

    auto& e = const_cast<PhotoBookEntry&>(m_entries.at(row));
    if (e.iconRequested || !e.icon.isNull())
        return;
    e.iconRequested = true;
    m_thumbLoader->request(e.path);
}

void PhotoBookBrowserModel::applyThumbnail(const QString& path, const QImage& image)
{
    const QString key = keyForPath(path);
    auto it = m_rowForPath.find(key);
    if (it == m_rowForPath.end())
        return;

    const int row = it.value();
    if (row < 0 || row >= m_entries.size())
        return;

    if (!image.isNull())
        m_entries[row].icon = QIcon(QPixmap::fromImage(image));

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

void PhotoBookBrowserModel::updateUsage(ScribusDoc* doc)
{
    m_doc = doc;

    QHash<QString, PhotoBookEntry*> usage;
    for (auto& e : m_entries)
    {
        e.pages.clear();
        e.frames.clear();
        usage.insert(keyForPath(e.path), &e);
    }

    if (doc)
    {
        for (int i = 0; i < doc->Items->count(); ++i)
        {
            PageItem* item = doc->Items->at(i);

            if (!item)
                continue;

            const int pageNumber = item->OwnPage + 1;

            inspectItem(item, usage, pageNumber);
        }
    }

    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(m_entries.size() - 1), {UsedRole, PagesRole, PlacementsRole, Qt::ToolTipRole});
    emit usageUpdated();
}

const PhotoBookEntry* PhotoBookBrowserModel::entry(int row) const
{
    return (row >= 0 && row < m_entries.size()) ? &m_entries.at(row) : nullptr;
}

int PhotoBookBrowserModel::unusedCount() const
{
    int n = 0;
    for (const auto& e : m_entries)
    {
        if (!e.used())
            ++n;
    }
    return n;
}
