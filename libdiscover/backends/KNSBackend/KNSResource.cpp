/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "KNSResource.h"
#include "KNSBackend.h"
#include <KLocalizedString>
#include <KNSCore/Engine>
#include <KShell>
#include <QProcess>
#include <QRegularExpression>

#include "ReviewsBackend/Rating.h"
#include <appstream/AppStreamUtils.h>
#include <attica/provider.h>

KNSResource::KNSResource(const KNSCore::EntryInternal &entry, QStringList categories, KNSBackend *parent)
    : AbstractResource(parent)
    , m_categories(std::move(categories))
    , m_entry(entry)
    , m_lastStatus(entry.status())
{
    connect(this, &KNSResource::stateChanged, parent, &KNSBackend::updatesCountChanged);
}

KNSResource::~KNSResource() = default;

AbstractResource::State KNSResource::state()
{
    switch (m_entry.status()) {
    case KNS3::Entry::Invalid:
        return Broken;
    case KNS3::Entry::Downloadable:
        return None;
    case KNS3::Entry::Installed:
        return Installed;
    case KNS3::Entry::Updateable:
        return Upgradeable;
    case KNS3::Entry::Deleted:
    case KNS3::Entry::Installing:
    case KNS3::Entry::Updating:
        return None;
    }
    return None;
}

KNSBackend *KNSResource::knsBackend() const
{
    return qobject_cast<KNSBackend *>(parent());
}

QVariant KNSResource::icon() const
{
    const QString thumbnail = m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall1);
    return thumbnail.isEmpty() ? knsBackend()->iconName() : m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall1);
}

QString KNSResource::comment()
{
    QString ret = m_entry.shortSummary();
    if (ret.isEmpty()) {
        ret = m_entry.summary();
        int newLine = ret.indexOf(QLatin1Char('\n'));
        if (newLine > 0) {
            ret.truncate(newLine);
        }
        ret.remove(QRegularExpression(QStringLiteral("\\[\\/?[a-z]*\\]")));
        ret.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    }
    return ret;
}

QString KNSResource::longDescription()
{
    QString ret = m_entry.summary();
    if (m_entry.shortSummary().isEmpty()) {
        const int newLine = ret.indexOf(QLatin1Char('\n'));
        if (newLine < 0)
            ret.clear();
        else
            ret = ret.mid(newLine + 1).trimmed();
    }
    ret.remove(QLatin1Char('\r'));
    ret.replace(QStringLiteral("[li]"), QStringLiteral("\n* "));
    // Get rid of all BBCode markup we don't handle above
    ret.remove(QRegularExpression(QStringLiteral("\\[\\/?[a-z]*\\]")));
    // Find anything that looks like a link (but which also is not some html
    // tag value or another already) and make it a link
    static const QRegularExpression urlRegExp(
        QStringLiteral("(^|\\s)(http[-a-zA-Z0-9@:%_\\+.~#?&//=]{2,256}\\.[a-z]{2,4}\\b(\\/[-a-zA-Z0-9@:;%_\\+.~#?&//=]*)?)"),
        QRegularExpression::CaseInsensitiveOption);
    ret.replace(urlRegExp, QStringLiteral("<a href=\"\\2\">\\2</a>"));
    return ret;
}

QString KNSResource::name() const
{
    return m_entry.name();
}

QString KNSResource::packageName() const
{
    return m_entry.uniqueId();
}

QStringList KNSResource::categories()
{
    return m_categories;
}

QUrl KNSResource::homepage()
{
    return m_entry.homepage();
}

void KNSResource::setEntry(const KNSCore::EntryInternal &entry)
{
    const bool diff = entry.status() != m_lastStatus;
    m_entry = entry;
    if (diff) {
        m_lastStatus = entry.status();
        Q_EMIT stateChanged();
    }
}

KNSCore::EntryInternal KNSResource::entry() const
{
    return m_entry;
}

QJsonArray KNSResource::licenses()
{
    return {{AppStreamUtils::license(m_entry.license())}};
}

quint64 KNSResource::size()
{
    const auto downloadInfo = m_entry.downloadLinkInformationList();
    return downloadInfo.isEmpty() ? 0 : downloadInfo.at(0).size * 1024;
}

QString KNSResource::installedVersion() const
{
    return !m_entry.version().isEmpty() ? m_entry.version() : m_entry.releaseDate().toString();
}

QString KNSResource::availableVersion() const
{
    return !m_entry.updateVersion().isEmpty()   ? m_entry.updateVersion()
        : !m_entry.updateReleaseDate().isNull() ? m_entry.updateReleaseDate().toString()
        : !m_entry.version().isEmpty()          ? m_entry.version()
                                                : releaseDate().toString();
}

QString KNSResource::origin() const
{
    return m_entry.providerId();
}

QString KNSResource::displayOrigin() const
{
    if (auto providers = knsBackend()->engine()->atticaProviders(); !providers.isEmpty()) {
        auto provider = providers.constFirst();
        if (provider->name() == QLatin1String("api.kde-look.org")) {
            return i18nc("The name of the KDE Store", "KDE Store");
        }
        return providers.constFirst()->name();
    }
    return QUrl(m_entry.providerId()).host();
}

QString KNSResource::section()
{
    return m_entry.category();
}

static void appendIfValid(QList<QUrl> &list, const QUrl &value, const QUrl &fallback = {})
{
    if (!list.contains(value)) {
        if (value.isValid() && !value.isEmpty())
            list << value;
        else if (!fallback.isEmpty())
            appendIfValid(list, fallback);
    }
}

void KNSResource::fetchScreenshots()
{
    QList<QUrl> preview;
    appendIfValid(preview, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall1)));
    appendIfValid(preview, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall2)));
    appendIfValid(preview, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall3)));

    QList<QUrl> screenshots;
    appendIfValid(screenshots, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewBig1)), QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall1)));
    appendIfValid(screenshots, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewBig2)), QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall2)));
    appendIfValid(screenshots, QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewBig3)), QUrl(m_entry.previewUrl(KNSCore::EntryInternal::PreviewSmall3)));

    Q_EMIT screenshotsFetched(preview, screenshots);
}

void KNSResource::fetchChangelog()
{
    Q_EMIT changelogFetched(m_entry.changelog());
}

QStringList KNSResource::extends() const
{
    return knsBackend()->extends();
}

QUrl KNSResource::url() const
{
    return QUrl(QStringLiteral("kns://") + knsBackend()->name() + QLatin1Char('/') + QUrl(m_entry.providerId()).host() + QLatin1Char('/') + m_entry.uniqueId());
}

bool KNSResource::canExecute() const
{
    return knsBackend()->engine()->hasAdoptionCommand();
}

void KNSResource::invokeApplication() const
{
    knsBackend()->engine()->adoptEntry(m_entry);
}

QString KNSResource::executeLabel() const
{
    return knsBackend()->engine()->useLabel();
}

QDate KNSResource::releaseDate() const
{
    return m_entry.updateReleaseDate().isNull() ? m_entry.releaseDate() : m_entry.updateReleaseDate();
}

QVector<int> KNSResource::linkIds() const
{
    QVector<int> ids;
    const auto linkInfo = m_entry.downloadLinkInformationList();
    for (const auto &e : linkInfo) {
        if (e.isDownloadtypeLink)
            ids << e.id;
    }
    return ids;
}

QUrl KNSResource::donationURL()
{
    return QUrl(m_entry.donationLink());
}

Rating *KNSResource::ratingInstance()
{
    if (!m_rating) {
        const int noc = m_entry.numberOfComments();
        const int rating = m_entry.rating();
        Q_ASSERT(rating <= 100);
        m_rating.reset(new Rating(packageName(), noc, rating / 10));
    }
    return m_rating.data();
}

QString KNSResource::author() const
{
    return m_entry.author().name();
}
