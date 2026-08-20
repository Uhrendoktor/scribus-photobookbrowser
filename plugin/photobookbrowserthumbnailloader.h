#ifndef PHOTOBOOKBROWSERTHUMBNAILLOADER_H
#define PHOTOBOOKBROWSERTHUMBNAILLOADER_H

#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>

// Small helper that decodes and scales thumbnails on a background thread
// pool so opening a folder with hundreds of photos never blocks the
// Scribus UI. Results stream back one at a time via thumbnailReady(), which
// is exactly what lets the browser grid "fill in live" instead of freezing
// until everything is decoded.
class PhotoBookThumbnailLoader : public QObject
{
    Q_OBJECT
public:
    explicit PhotoBookThumbnailLoader(QObject* parent = nullptr);

    void setThumbnailSize(const QSize& size) { m_size = size; }

    // No-op if a request for this path is already in flight.
    void request(const QString& path);

signals:
    void thumbnailReady(const QString& path, const QImage& image);

private:
    QThreadPool m_pool;
    QSet<QString> m_pending;
    QSize m_size {160, 160};
};

#endif
