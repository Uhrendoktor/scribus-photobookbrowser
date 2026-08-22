#include "photobookbrowserdelegate.h"
#include "photobookbrowsermodel.h"

#include <QColor>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionViewItem>

namespace
{
constexpr int kMargin = 6;
constexpr int kBadgeSize = 20;
constexpr int kTextHeight = 16;
}

PhotoBookBrowserDelegate::PhotoBookBrowserDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QSize PhotoBookBrowserDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    const QSize thumb = thumbnailSize();
    return QSize(thumb.width() + kMargin * 2, thumb.height() + kMargin * 2 + kTextHeight);
}

void PhotoBookBrowserDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect cell = option.rect;
    const bool used = index.data(PhotoBookBrowserModel::UsedRole).toBool();
    const bool selected = option.state & QStyle::State_Selected;

    if (selected)
    {
        painter->fillRect(cell, option.palette.highlight());
    }
    else if (option.state & QStyle::State_MouseOver)
    {
        painter->fillRect(cell, option.palette.alternateBase());
    }

    const QSize thumb = thumbnailSize();
    QRect thumbRect(cell.left() + (cell.width() - thumb.width()) / 2,
                     cell.top() + kMargin,
                     thumb.width(), thumb.height());

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    painter->setPen(Qt::NoPen);
    painter->setBrush(option.palette.dark());
    painter->drawRoundedRect(thumbRect, 4, 4);

    if (!icon.isNull())
    {
        // Do not use QIcon::pixmap(thumb) here. On Qt 6, the icon engine can
        // choose a square mode for portrait images, making them extend
        // beyond the intended aspect-ratio-preserving thumbnail area.
        // Render into the actual thumbnail rectangle explicitly instead.
        const QPixmap source = icon.pixmap(icon.actualSize(QSize(10000, 10000)));
        const QPixmap pm = source.scaled(thumbRect.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
        const QRect target(thumbRect.left() + (thumbRect.width() - pm.width()) / 2,
                            thumbRect.top() + (thumbRect.height() - pm.height()) / 2,
                            pm.width(), pm.height());

        // Used photos are dimmed a little so the eye is drawn to what
        // still needs to be placed - mirrors how most photobook tools
        // (e.g. "used" trays) fade already-placed assets.
        if (used)
        {
            QPixmap dimmed(pm.size());
            dimmed.fill(Qt::transparent);
            QPainter dimPainter(&dimmed);
            dimPainter.setOpacity(0.55);
            dimPainter.drawPixmap(0, 0, pm);
            dimPainter.end();
            painter->drawPixmap(target, dimmed);
        }
        else
        {
            painter->drawPixmap(target, pm);
        }
    }
    else
    {
        // Still decoding on a worker thread - keep the layout stable
        // instead of leaving a blank hole in the grid.
        painter->setPen(option.palette.color(QPalette::Disabled, QPalette::WindowText));
        painter->drawText(thumbRect, Qt::AlignCenter, tr("Loading…"));
    }

    if (used)
    {
        const int placements = index.data(PhotoBookBrowserModel::PlacementsRole).toInt();
        const QRect badgeRect(thumbRect.right() - kBadgeSize - 2, thumbRect.bottom() - kBadgeSize - 2,
                               kBadgeSize, kBadgeSize);

        painter->setBrush(QColor(46, 160, 67)); // used = green, unused = untouched thumbnail
        painter->setPen(QPen(Qt::white, 1.5));
        painter->drawEllipse(badgeRect);

        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 0.85);
        painter->setFont(f);
        const QString label = placements > 1 ? QString::number(placements) : QStringLiteral("\u2713");
        painter->drawText(badgeRect, Qt::AlignCenter, label);
    }

    const QRect textRect(cell.left() + 2, thumbRect.bottom() + 2, cell.width() - 4, kTextHeight);
    painter->setPen(selected ? option.palette.color(QPalette::HighlightedText)
                              : option.palette.color(QPalette::Text));
    QFont textFont = painter->font();
    textFont.setBold(false);
    textFont.setPointSizeF(textFont.pointSizeF() * 0.85);
    painter->setFont(textFont);
    const QFontMetrics fm(textFont);
    const QString elided = fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, textRect.width());
    painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, elided);

    painter->restore();
}
