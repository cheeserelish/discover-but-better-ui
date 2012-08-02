/***************************************************************************
 *   Copyright © 2012 Jonathan Thomas <echidnaman@kubuntu.org>             *
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

#include "TransactionModel.h"

#include <KDebug>

// Own includes
#include "Application.h"
#include "ApplicationBackend.h"
#include "ApplicationModel/ApplicationModel.h"
#include "TransactionListener.h"
#include "Transaction.h"

TransactionModel::TransactionModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_appBackend(nullptr)
{
}

QVariant TransactionModel::data(const QModelIndex& index, int role) const
{
    if(!index.isValid())
        return QVariant();

    TransactionListener *trans = m_transactions.at(index.row());

    switch (role) {
        case ApplicationModel::NameRole:
            return trans->resource()->name();
        case ApplicationModel::IconRole:
            return trans->resource()->icon();
        case ApplicationModel::CommentRole:
            return trans->resource()->comment();
        case ApplicationModel::RatingRole:
            return -1;
        case ApplicationModel::ActiveRole:
            return true;
        case ApplicationModel::ProgressRole:
            return trans->progress();
        case ApplicationModel::ProgressTextRole:
            return trans->comment();
        case ApplicationModel::InstalledRole:
            return false;
        case Qt::ToolTipRole:
            return QVariant();
        case ApplicationModel::ApplicationRole:
            return qVariantFromValue<QObject *>(trans->resource());
        default:
            return QVariant();
    }

    return QVariant();
}

int TransactionModel::rowCount(const QModelIndex &parent) const
{
    if(parent.isValid())
        return 0;
    return m_transactions.size();
}

void TransactionModel::setBackend(ApplicationBackend* appBackend)
{
    m_appBackend = appBackend;
    connect(m_appBackend, SIGNAL(transactionProgressed(Transaction*,int)), this, SLOT(externalUpdate()));
    connect(m_appBackend, SIGNAL(transactionAdded(Transaction*)), this, SLOT(addTransaction(Transaction*)));
    connect(m_appBackend, SIGNAL(transactionCancelled(Application*)),
            this, SLOT(removeTransaction(Application*)));
}

void TransactionModel::addTransactions(const QList<Transaction *> &transList)
{
    for (Transaction *trans : transList) {
        addTransaction(trans);
    }
}

void TransactionModel::addTransaction(Transaction *trans)
{
    if (!trans || !trans->resource())
        return;

    beginInsertRows(QModelIndex(), 0, 0);
    TransactionListener *listener = new TransactionListener(this);
    listener->setBackend(m_appBackend);
    listener->setResource(trans->resource());
    m_transactions.append(listener);
    endInsertRows();
}

void TransactionModel::removeTransaction(AbstractResource *app)
{
    TransactionListener *toRemove = nullptr;
    for (TransactionListener *listener : m_transactions) {
        if(listener->resource() == app) {
            toRemove = listener;
            break;
        }
    }

    if (toRemove)
        removeTransaction(toRemove);
}

void TransactionModel::removeTransaction(TransactionListener *listener)
{
    if (listener) {
        int row = m_transactions.indexOf(listener);
        beginRemoveRows(QModelIndex(), row, row);
        m_transactions.removeAll(listener);
        delete listener;
        endRemoveRows();
    }

    if (!m_transactions.size())
        emit lastTransactionCancelled();
}

void TransactionModel::clear()
{
    beginResetModel();
    qDeleteAll(m_transactions);
    m_transactions.clear();
    endResetModel();
}

void TransactionModel::externalUpdate()
{
    emit dataChanged(QModelIndex(), QModelIndex());
}
