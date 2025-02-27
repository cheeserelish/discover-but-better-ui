/*
 *   SPDX-FileCopyrightText: 2021 Mariam Fahmy Sobhy <mariamfahmy66@gmail.com>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "RpmOstreeNotifier.h"

#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

RpmOstreeNotifier::RpmOstreeNotifier(QObject *parent)
    : BackendNotifierModule(parent)
    , m_hasUpdates(false)
    , m_needsReboot(false)
{
}

RpmOstreeNotifier::~RpmOstreeNotifier()
{
}

void RpmOstreeNotifier::recheckSystemUpdateNeeded()
{
    qInfo() << "rpm-ostree-notifier: Checking for system update";
    m_process = new QProcess(this);
    m_stdout = QByteArray();

    // Display stderr
    connect(m_process, &QProcess::readyReadStandardError, [this]() {
        QByteArray message = m_process->readAllStandardError();
        qWarning() << "rpm-ostree (error):" << message;
    });

    // Display and store stdout
    connect(m_process, &QProcess::readyReadStandardOutput, [this]() {
        QByteArray message = m_process->readAllStandardOutput();
        qInfo() << "rpm-ostree:" << message;
        m_stdout += message;
    });

    // Process command result
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_process->deleteLater();
        if (exitStatus != QProcess::NormalExit) {
            qWarning() << "rpm-ostree-notifier: Failed to check for system update";
            return;
        }
        if (exitCode == 77) {
            // rpm-ostree will exit with status 77 when no updates are available
            qInfo() << "rpm-ostree-notifier: No updates available";
            return;
        }
        if (exitCode != 0) {
            qWarning() << "rpm-ostree-notifier: Failed to check for system update. Exit code:" << exitCode;
            return;
        }

        // We have an update available. Let's look if we already have a pending
        // deployment for the new version. First, look for the new version
        // string in rpm-ostree stdout
        QString newVersion, line;
        QString output = QString(m_stdout);
        QTextStream stream(&output);
        while (stream.readLineInto(&line)) {
            if (line.contains(QLatin1String("Version: "))) {
                newVersion = line;
                break;
            }
        }

        // Could not find the new version in rpm-ostree output. This is unlikely
        // to ever happen.
        if (newVersion.isEmpty()) {
            qInfo() << "rpm-ostree-notifier: Could not find the version for the update available";
        }

        // Process the string to get just the version "number".
        newVersion = newVersion.trimmed();
        newVersion.remove(0, QStringLiteral("Version: ").length());
        newVersion.remove(newVersion.size() - QStringLiteral(" (XXXX-XX-XXTXX:XX:XXZ)").length(), newVersion.size() - 1);
        qInfo() << "rpm-ostree-notifier: Found new version:" << newVersion;

        // Have we already notified the user about this update?
        if (newVersion == m_updateVersion) {
            qInfo() << "rpm-ostree-notifier: New version has already been offered. Skipping.";
            return;
        }
        m_updateVersion = newVersion;

        // Look for an existing deployment with this version
        checkForPendingDeployment();
    });

    m_process->start(QStringLiteral("rpm-ostree"), {QStringLiteral("update"), QStringLiteral("--check")});
}

void RpmOstreeNotifier::checkForPendingDeployment()
{
    qInfo() << "rpm-ostree-notifier: Looking at existing deployments";
    m_process = new QProcess(this);
    m_stdout = QByteArray();

    // Display stderr
    connect(m_process, &QProcess::readyReadStandardError, [this]() {
        QByteArray message = m_process->readAllStandardError();
        qWarning() << "rpm-ostree (error):" << message;
    });

    // Store stdout to process as JSON
    connect(m_process, &QProcess::readyReadStandardOutput, [this]() {
        QByteArray message = m_process->readAllStandardOutput();
        m_stdout += message;
    });

    // Process command result
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_process->deleteLater();
        if (exitStatus != QProcess::NormalExit) {
            qWarning() << "rpm-ostree-notifier: Failed to check for existing deployments";
            return;
        }
        if (exitCode != 0) {
            // Unexpected error
            qWarning() << "rpm-ostree-notifier: Failed to check for existing deployments. Exit code:" << exitCode;
            return;
        }

        // Parse stdout as JSON and look at the deployments for a pending
        // deployment for the new version.
        const QJsonDocument jsonDocument = QJsonDocument::fromJson(m_stdout);
        if (jsonDocument.isObject() == false) {
            qWarning() << "rpm-ostree-notifier: Could not parse 'rpm-ostree status' output as JSON";
            return;
        }
        const QJsonArray deployments = jsonDocument.object().value("deployments").toArray();
        if (deployments.size() == 0) {
            qWarning() << "rpm-ostree-notifier: Could not find the deployments in 'rpm-ostree status' JSON output";
            return;
        }
        QString version;
        for (const QJsonValue &deployment : deployments) {
            version = deployment.toObject()["base-version"].toString();
            if (version.isEmpty()) {
                version = deployment.toObject()["version"].toString();
            }
            if (version.isEmpty()) {
                qInfo() << "rpm-ostree-notifier: Could not read version for deployment:" << deployment;
                continue;
            }
            if (version == m_updateVersion) {
                qInfo() << "rpm-ostree-notifier: Found an existing deployment for the update available";
                if (!m_needsReboot) {
                    m_needsReboot = true;
                    Q_EMIT needsRebootChanged();
                    return;
                }
            }
        }

        // No deployment found for the new version. Let's notify the user.
        qInfo() << "rpm-ostree-notifier: Notifying that a new update is available";
        m_hasUpdates = true;
        Q_EMIT foundUpdates();

        // TODO: Look for security updates fixed by this new deployment
    });

    m_process->start(QStringLiteral("rpm-ostree"), {QStringLiteral("status"), QStringLiteral("--json")});
}

bool RpmOstreeNotifier::hasSecurityUpdates()
{
    return false;
}

bool RpmOstreeNotifier::needsReboot() const
{
    return m_needsReboot;
}

bool RpmOstreeNotifier::hasUpdates()
{
    return m_hasUpdates;
}
