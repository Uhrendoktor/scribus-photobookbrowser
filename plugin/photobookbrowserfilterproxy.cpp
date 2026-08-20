#include "photobookbrowserfilterproxy.h"
#include "photobookbrowsermodel.h"

PhotoBookBrowserFilterProxy::PhotoBookBrowserFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(PhotoBookBrowserModel::PathRole);
    setSortRole(PhotoBookBrowserModel::FileNameRole);
    setDynamicSortFilter(true);
}

void PhotoBookBrowserFilterProxy::setStatusFilter(StatusFilter filter)
{
    if (m_statusFilter == filter)
        return;
    m_statusFilter = filter;
    invalidateFilter();
}

bool PhotoBookBrowserFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent))
        return false;

    if (m_statusFilter == ShowAll)
        return true;

    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const bool used = idx.data(PhotoBookBrowserModel::UsedRole).toBool();
    return m_statusFilter == ShowUsed ? used : !used;
}
