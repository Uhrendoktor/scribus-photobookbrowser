#include "photobookbrowserthumbnailloader.h"

#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QtConcurrentRun>

PhotoBookThumbnailLoader::PhotoBookThumbnailLoader(QObject* parent)
    : QObject(parent)
{
    // Thumbnailing is I/O + decode heavy but shouldn't starve the rest of
    // Scribus (e.g. autosave, redraw). Cap workers instead of using every
    // core.
    m_pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount() / 2));
}

void PhotoBookThumbnailLoader::request(const QString& path)
{
    if (m_pending.contains(path))
        return;
    m_pending.insert(path);

    const QSize targetSize = m_size;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path]() {
        m_pending.remove(path);
        emit thumbnailReady(path, watcher->result());
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(&m_pool, [path, targetSize]() -> QImage {
        QImageReader reader(path);
        reader.setAutoTransform(true); // respect EXIF orientation
        // Downscale while decoding where the format supports it, which is
        // dramatically cheaper than loading a 24MP photo just to shrink it.
        const QSize original = reader.size();
        if (original.isValid())
        {
            QSize scaled = original;
            scaled.scale(targetSize * 2, Qt::KeepAspectRatio);
            reader.setScaledSize(scaled);
        }

        QImage img = reader.read();
        if (img.isNull())
            return QImage();

        return img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }));
}
