#include "photobookbrowserpalette.h"
#include "photobookbrowsermodel.h"
#include "photobookbrowserfilterproxy.h"
#include "photobookbrowserdelegate.h"

#include "pageitem.h"
#include "pageitem_imageframe.h"
#include "scribus.h"
#include "scribusdoc.h"
#include "scribusview.h"
#include "commonstrings.h"
#include "selection.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace
{
// Coalesce bursts of document-change notifications (Scribus can fire its
// "content changed" signal many times per second while dragging/typing)
// into a single usage recompute so the live update stays cheap.
constexpr int kLiveRefreshDelayMs = 200;
const QString kSettingsOrg = QStringLiteral("Scribus");
const QString kSettingsApp = QStringLiteral("PhotoBookBrowser");
const QString kSettingsFolderKey = QStringLiteral("lastFolder");
}

PhotoBookBrowserPalette::PhotoBookBrowserPalette(QWidget* parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("PhotoBookBrowserPalette"));
    setWindowTitle(tr("PhotoBook Browser"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);

    auto* toolbar = new QHBoxLayout;
    auto* folder = new QToolButton(root);
    folder->setText(tr("Folder…"));
    folder->setToolTip(tr("Choose the folder of photos for this album"));
    auto* refreshButton = new QToolButton(root);
    refreshButton->setText(tr("Refresh"));
    m_autoFillButton = new QToolButton(root);
    m_autoFillButton->setText(tr("Fill empty frames"));
    m_autoFillButton->setToolTip(tr("Place unused photos into every empty image frame on the document"));
    toolbar->addWidget(folder);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(m_autoFillButton);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* controls = new QHBoxLayout;
    m_search = new QLineEdit(root);
    m_search->setPlaceholderText(tr("Search filename or folder…"));
    m_filter = new QComboBox(root);
    m_filter->addItems({tr("All"), tr("Used"), tr("Unused")});
    m_sort = new QComboBox(root);
    m_sort->addItems({tr("Name"), tr("Newest first"), tr("Oldest first")});
    controls->addWidget(m_search, 1);
    controls->addWidget(m_filter);
    controls->addWidget(m_sort);
    layout->addLayout(controls);

    m_view = new QListView(root);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setUniformItemSizes(true);
    m_view->setIconSize(PhotoBookBrowserDelegate::thumbnailSize());
    m_view->setSpacing(10);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setItemDelegate(new PhotoBookBrowserDelegate(m_view));
    // Enables dragging a thumbnail straight onto the Scribus page/canvas;
    // Scribus's canvas already accepts dropped image files and creates a
    // picture frame at the cursor, so no drop handling is needed here.
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setDefaultDropAction(Qt::CopyAction);
    layout->addWidget(m_view, 1);

    m_summary = new QLabel(root);
    layout->addWidget(m_summary);
    setWidget(root);

    m_model = new PhotoBookBrowserModel(this);
    m_proxy = new PhotoBookBrowserFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_view->setModel(m_proxy);

    m_liveRefreshTimer = new QTimer(this);
    m_liveRefreshTimer->setSingleShot(true);
    m_liveRefreshTimer->setInterval(kLiveRefreshDelayMs);
    connect(m_liveRefreshTimer, &QTimer::timeout, this, &PhotoBookBrowserPalette::performLiveRefresh);

    connect(folder, &QToolButton::clicked, this, &PhotoBookBrowserPalette::chooseFolder);
    connect(refreshButton, &QToolButton::clicked, this, &PhotoBookBrowserPalette::refresh);
    connect(m_autoFillButton, &QToolButton::clicked, this, &PhotoBookBrowserPalette::autoFillEmptyFrames);
    connect(m_search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_view, &QListView::doubleClicked, this, &PhotoBookBrowserPalette::activate);
    connect(m_view, &QListView::customContextMenuRequested, this, &PhotoBookBrowserPalette::contextMenu);
    connect(m_model, &PhotoBookBrowserModel::usageUpdated, this, &PhotoBookBrowserPalette::updateSummary);
    connect(m_model, &PhotoBookBrowserModel::scanStarted, this, &PhotoBookBrowserPalette::scanStarted);
    connect(m_model, &PhotoBookBrowserModel::scanFinished, this, &PhotoBookBrowserPalette::scanFinished);
    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this, &PhotoBookBrowserPalette::statusFilterChanged);
    connect(m_sort, qOverload<int>(&QComboBox::currentIndexChanged), this, &PhotoBookBrowserPalette::sortModeChanged);

    sortModeChanged(m_sort->currentIndex());
    loadSettings();
    updateSummary();
}

void PhotoBookBrowserPalette::loadSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString folder = settings.value(kSettingsFolderKey).toString();
    if (!folder.isEmpty() && QFileInfo::exists(folder))
        m_model->setFolder(folder);
}

void PhotoBookBrowserPalette::saveFolderSetting(const QString& folder)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kSettingsFolderKey, folder);
}

void PhotoBookBrowserPalette::setDocument(ScribusDoc* doc)
{
    if (m_doc)
    {
        // ---- API-sensitive block ------------------------------------
        // ScribusDoc emits a general "changed()" signal for most content
        // edits (this is how the built-in Outline/Layers palettes stay
        // live without polling). If your 1.6.5 checkout names this
        // differently, this disconnect/connect pair is the only place
        // that needs adjusting - everything else reacts to
        // scheduleLiveRefresh()/performLiveRefresh() regardless of what
        // triggers it.
        disconnect(m_doc, SIGNAL(changed()), this, SLOT(scheduleLiveRefresh()));
        // ----------------------------------------------------------------
    }

    m_doc = doc;
    m_model->updateUsage(doc);

    if (m_doc)
        connect(m_doc, SIGNAL(changed()), this, SLOT(scheduleLiveRefresh()));
}

void PhotoBookBrowserPalette::scheduleLiveRefresh()
{
    m_liveRefreshTimer->start();
}

void PhotoBookBrowserPalette::performLiveRefresh()
{
    m_model->updateUsage(m_doc);
}

void PhotoBookBrowserPalette::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select photo folder"));
    if (!dir.isEmpty())
    {
        m_model->setFolder(dir);
        saveFolderSetting(dir);
    }
}

void PhotoBookBrowserPalette::refresh()
{
    m_model->scan();
}

void PhotoBookBrowserPalette::activate(const QModelIndex& proxyIndex)
{
    const QModelIndex source = m_proxy->mapToSource(proxyIndex);
    if (!source.isValid())
        return;

    insertImage(source.data(PhotoBookBrowserModel::PathRole).toString(), false);
}

void PhotoBookBrowserPalette::insertImage(const QString& path, bool replaceSelected)
{
    if (!m_doc || !m_doc->view())
    {
        QMessageBox::warning(this, tr("PhotoBook Browser"), tr("No Scribus document is active."));
        return;
    }

    // This is the API-sensitive adapter. If your 1.6.5 checkout exposes a
    // different itemAdd overload, compiler output should point only here.
    // The exact itemAdd signature is intentionally isolated from the browser.
    if (replaceSelected && m_doc->m_Selection && m_doc->m_Selection->count() == 1)
    {
        PageItem* selected = m_doc->m_Selection->itemAt(0);
        if (selected && selected->isImageFrame())
        {
            auto* frame = selected->asImageFrame();
            frame->loadImage(path, false, false);
            m_doc->view()->DrawNew();
            m_model->updateUsage(m_doc);
            return;
        }
    }

    // Use the current visible page and a modest default frame. The final
    // placement can be refined after the first local compile/test pass.
    const int pageIndex = m_doc->currentPageNumber();
    if (pageIndex < 0 || pageIndex >= m_doc->Pages->count())
        return;

    ScPage* page = m_doc->Pages->at(pageIndex);
    if (!page)
        return;

    const double x = page->xOffset() + 20.0;
    const double y = page->yOffset() + 20.0;
    const double w = 150.0;
    const double h = 110.0;

    int z = m_doc->itemAdd(
        PageItem::ImageFrame,
        PageItem::Unspecified,
        x, y, w, h,
        m_doc->itemToolPrefs().shapeLineWidth,
        m_doc->itemToolPrefs().imageFillColor,
        m_doc->itemToolPrefs().imageStrokeColor
    );

    if (z < 0 || z >= m_doc->Items->count())
        return;

    PageItem* frame = m_doc->Items->at(z);

    if (!frame)
        return;

    auto* imageFrame = frame->asImageFrame();
    if (!imageFrame)
        return;

    imageFrame->setImageXYScale(1.0, 1.0);
    imageFrame->loadImage(path, false, false);
    imageFrame->setImageScalingMode(true, true);

    m_doc->scMW()->deselectAll();
    m_doc->view()->selectItem(frame);
    m_doc->view()->DrawNew();
    m_model->updateUsage(m_doc);
}

void PhotoBookBrowserPalette::autoFillEmptyFrames()
{
    if (!m_doc)
    {
        QMessageBox::warning(this, tr("PhotoBook Browser"), tr("No Scribus document is active."));
        return;
    }

    QVector<PageItem*> emptyFrames;
    for (int i = 0; i < m_doc->Items->count(); ++i)
    {
        PageItem* item = m_doc->Items->at(i);
        if (item && item->isImageFrame())
        {
            auto* frame = item->asImageFrame();
            if (frame && frame->Pfile.isEmpty())
                emptyFrames.append(item);
        }
    }

    if (emptyFrames.isEmpty())
    {
        QMessageBox::information(this, tr("PhotoBook Browser"), tr("No empty image frames on this document."));
        return;
    }

    QVector<QString> unusedPaths;
    for (int i = 0; i < m_model->rowCount(); ++i)
    {
        const QModelIndex idx = m_model->index(i, 0);
        if (!idx.data(PhotoBookBrowserModel::UsedRole).toBool())
            unusedPaths << idx.data(PhotoBookBrowserModel::PathRole).toString();
    }

    const int n = qMin(emptyFrames.size(), unusedPaths.size());
    for (int i = 0; i < n; ++i)
    {
        auto* imgFrame = emptyFrames[i]->asImageFrame();
        imgFrame->setImageXYScale(1.0, 1.0);
        imgFrame->loadImage(unusedPaths[i], false, false);
        
        // Original image dimensions in Scribus' coordinate system
        const double imageW = static_cast<double>(imgFrame->OrigW);
        const double imageH = static_cast<double>(imgFrame->OrigH);

        const double frameW = imgFrame->width();
        const double frameH = imgFrame->height();

        // "Cover": completely fill the frame, preserving aspect ratio
        const double scaleX = frameW / imageW;
        const double scaleY = frameH / imageH;
        const double scale = qMax(scaleX, scaleY);

        imgFrame->setImageXYScale(scale, scale);

        // Center the cropped image
        const double scaledW = imageW * scale;
        const double scaledH = imageH * scale;

        const double offsetX = (frameW - scaledW) / (2.0 * scale);
        const double offsetY = (frameH - scaledH) / (2.0 * scale);

        imgFrame->setImageXYOffset(offsetX, offsetY);

        imgFrame->setImageScalingMode(true, true);
    }

    m_doc->view()->DrawNew();
    m_model->updateUsage(m_doc);

    if (unusedPaths.size() < emptyFrames.size())
    {
        QMessageBox::information(this, tr("PhotoBook Browser"),
            tr("Filled %1 frame(s). Ran out of unused photos for the remaining %2 empty frame(s).")
                .arg(unusedPaths.size()).arg(emptyFrames.size() - unusedPaths.size()));
    }
    else
    {
        QMessageBox::information(this, tr("PhotoBook Browser"),
            tr("Filled %1 empty frame(s).").arg(n));
    }
}

void PhotoBookBrowserPalette::contextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = m_view->indexAt(pos);
    if (!proxyIndex.isValid())
        return;

    const QModelIndex source = m_proxy->mapToSource(proxyIndex);
    const QString path = source.data(PhotoBookBrowserModel::PathRole).toString();
    const bool used = source.data(PhotoBookBrowserModel::UsedRole).toBool();
    const QString pages = source.data(PhotoBookBrowserModel::PagesRole).toString();

    QMenu menu(this);
    QAction* add = menu.addAction(tr("Add to current page"));
    QAction* replace = menu.addAction(tr("Replace selected image frame"));
    menu.addSeparator();
    if (used)
    {
        QAction* info = menu.addAction(tr("Already placed on page(s): %1").arg(pages));
        info->setEnabled(false);
        menu.addSeparator();
    }
    QAction* reveal = menu.addAction(tr("Reveal in file manager"));

    QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (chosen == add)
        insertImage(path, false);
    else if (chosen == replace)
        insertImage(path, true);
    else if (chosen == reveal)
        QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
}

void PhotoBookBrowserPalette::updateSummary()
{
    int used = 0;
    int unused = 0;
    for (int i = 0; i < m_model->rowCount(); ++i)
    {
        if (m_model->index(i, 0).data(PhotoBookBrowserModel::UsedRole).toBool())
            ++used;
        else
            ++unused;
    }
    m_summary->setText(tr("%1 photos | %2 used | %3 unused")
                       .arg(used + unused).arg(used).arg(unused));
}

void PhotoBookBrowserPalette::statusFilterChanged(int index)
{
    m_proxy->setStatusFilter(static_cast<PhotoBookBrowserFilterProxy::StatusFilter>(index));
}

void PhotoBookBrowserPalette::sortModeChanged(int index)
{
    switch (index)
    {
    case 1: // Newest first
        m_proxy->setSortRole(PhotoBookBrowserModel::ModifiedRole);
        m_proxy->sort(0, Qt::DescendingOrder);
        break;
    case 2: // Oldest first
        m_proxy->setSortRole(PhotoBookBrowserModel::ModifiedRole);
        m_proxy->sort(0, Qt::AscendingOrder);
        break;
    default: // Name
        m_proxy->setSortRole(PhotoBookBrowserModel::FileNameRole);
        m_proxy->sort(0, Qt::AscendingOrder);
        break;
    }
}

void PhotoBookBrowserPalette::scanStarted()
{
    m_summary->setText(tr("Scanning photos…"));
}

void PhotoBookBrowserPalette::scanFinished()
{
    updateSummary();
}

void PhotoBookBrowserPalette::retranslateUi()
{
    setWindowTitle(tr("PhotoBook Browser"));
    m_search->setPlaceholderText(tr("Search filename or folder…"));
    updateSummary();
}
