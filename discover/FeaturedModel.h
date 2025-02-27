/*
 *   SPDX-FileCopyrightText: 2016 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef FEATUREDMODEL_H
#define FEATUREDMODEL_H

#include "AbstractAppsModel.h"
#include <QPointer>

namespace KIO
{
class StoredTransferJob;
}

class FeaturedModel : public AbstractAppsModel
{
    Q_OBJECT
public:
    FeaturedModel();
    ~FeaturedModel() override
    {
    }

    void refresh() override;
};

#endif // FEATUREDMODEL_H
