/***************************************************************************
 *   Copyright © 2010 Jonathan Thomas <echidnaman@kubuntu.org>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef PACKAGEWIDGET_H
#define PACKAGEWIDGET_H

// Qt includes
#include <QModelIndex>
#include <QtGui/QLabel>
#include <QFutureWatcher>

// KDE includes
#include <KVBox>

#include <LibQApt/Package>

#include "../libmuonprivate_export.h"

class QLabel;
class QTimer;
class QVBoxLayout;

class KAction;
class KLineEdit;
class KPixmapSequenceOverlayPainter;

class DetailsWidget;
class PackageModel;
class PackageProxyModel;
class PackageView;

namespace QApt
{
    class Backend;
}

class MUONPRIVATE_EXPORT PackageWidget : public KVBox
{
    Q_OBJECT
public:
    enum PackagesType {
        AvailablePackages = 1,
        UpgradeablePackages = 2,
        MarkedPackages = 3
    };

    PackageWidget(QWidget *parent);
    ~PackageWidget();

    void setPackagesType(int type);
    void setHeaderText(const QString &text);
    void hideHeaderLabel();
    void showSearchEdit();
    int packagesType() {
        return m_packagesType;
    }

protected:
    QApt::Backend *m_backend;
    PackageView *m_packageView;
    DetailsWidget *m_detailsWidget;
    PackageModel *m_model;
    PackageProxyModel *m_proxyModel;
    KPixmapSequenceOverlayPainter *m_busyWidget;

private:
    QApt::CacheState m_oldCacheState;

    QFutureWatcher<QList<QApt::Package*> >* m_watcher;
    QWidget *m_headerWidget;
    QLabel *m_headerLabel;
    KLineEdit *m_searchEdit;
    QTimer *m_searchTimer;

    KAction *m_installAction;
    KAction *m_removeAction;
    KAction *m_upgradeAction;
    KAction *m_reinstallAction;
    KAction *m_keepAction;
    KAction *m_purgeAction;

    int m_packagesType;
    bool m_compressEvents;
    bool m_stop;

    QString digestReason(QApt::Package *pkg,
                         QApt::BrokenReason failType,
                         QHash<QString, QVariantMap> failReason);

public Q_SLOTS:
    void setBackend(QApt::Backend *backend);
    void reload();
    void startSearch();

private Q_SLOTS:
    void setupActions();
    void packageActivated(const QModelIndex &index);
    void contextMenuRequested(const QPoint &pos);
    void setSortedPackages();

    bool confirmEssentialRemoval();
    void saveState();
    void handleBreakage(QApt::Package *package);
    void actOnPackages(QApt::Package::State state);
    void setInstall(QApt::Package *package);
    void setPackagesInstall();
    void setRemove(QApt::Package *package);
    void setPackagesRemove();
    void setUpgrade(QApt::Package *package);
    void setPackagesUpgrade();
    void setReInstall(QApt::Package *package);
    void setPackagesReInstall();
    void setPurge(QApt::Package *package);
    void setPackagesPurge();
    void setKeep(QApt::Package *package);
    void setPackagesKeep();
    void showBrokenReason(QApt::Package *package);
};

#endif
