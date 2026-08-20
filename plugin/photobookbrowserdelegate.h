#ifndef PHOTOBOOKBROWSERDELEGATE_H
#define PHOTOBOOKBROWSERDELEGATE_H

#include <QStyledItemDelegate>

// Paints the thumbnail grid cells: a checkmark badge (plus placement
// count) for images already placed in the document, a subtle dimming so
// used photos visually recede versus unused ones, a loading placeholder
// while the async thumbnail is still decoding, and the filename underneath
// - the "which images are already used" requirement from a plain
// IconMode QListView isn't visible at all without this.
class PhotoBookBrowserDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit PhotoBookBrowserDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    static QSize thumbnailSize() { return {150, 120}; }
};

#endif
