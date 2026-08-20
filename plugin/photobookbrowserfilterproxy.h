#ifndef PHOTOBOOKBROWSERFILTERPROXY_H
#define PHOTOBOOKBROWSERFILTERPROXY_H

#include <QSortFilterProxyModel>

// Combines the free-text search with the Used/Unused status filter. A
// plain QSortFilterProxyModel can only do one predicate via
// setFilterFixedString(), which is why the original combo box didn't
// actually filter anything - this replaces it with a real one.
class PhotoBookBrowserFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum StatusFilter { ShowAll, ShowUsed, ShowUnused };

    explicit PhotoBookBrowserFilterProxy(QObject* parent = nullptr);

    void setStatusFilter(StatusFilter filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    StatusFilter m_statusFilter {ShowAll};
};

#endif
