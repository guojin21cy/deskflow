/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VersionChecker.h"

#include "common/Settings.h"
#include "common/VersionInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <climits>

namespace {
const auto kGitHubHost = QStringLiteral("github.com");
const auto kReleasePath = QStringLiteral("/guojin21cy/deskflow/releases");

bool isTrustedReleaseUrl(const QUrl &url, const QString &expectedPath)
{
  return url.scheme() == QStringLiteral("https") && url.host() == kGitHubHost && url.port() == -1 &&
         url.userInfo().isEmpty() && url.query().isEmpty() && url.fragment().isEmpty() && url.path() == expectedPath;
}
} // namespace

VersionChecker::VersionChecker(QObject *parent) : QObject(parent), m_network{new QNetworkAccessManager(this)}
{
  connect(m_network, &QNetworkAccessManager::finished, this, &VersionChecker::replyFinished, Qt::UniqueConnection);
}

void VersionChecker::checkLatest() const
{
  const QString url = Settings::value(Settings::Gui::UpdateCheckUrl).toString();
  qDebug("checking for updates at: %s", qPrintable(url));
  auto request = QNetworkRequest(url);
  auto userAgent = QString("%1 %2 on %3").arg(kAppName, kVersion, QSysInfo::prettyProductName());
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  request.setRawHeader("X-Deskflow-Version", kVersion);
  request.setRawHeader("X-Deskflow-Language", qPrintable(QLocale::system().name()));
  m_network->get(request);
}

QUrl VersionChecker::downloadUrl() const
{
  return m_downloadUrl;
}

void VersionChecker::replyFinished(QNetworkReply *reply)
{
  const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto networkError = reply->error();
  const auto errorString = reply->errorString();
  const auto response = reply->readAll();
  reply->deleteLater();

  if (networkError != QNetworkReply::NoError) {
    qWarning("version check server error: %s", qPrintable(errorString));
    qWarning("version check server response: %s", qPrintable(QString(response)));
    qWarning("error checking for updates, http status: %d", httpStatus);
    return;
  }

  qDebug("version check server success, http status: %d", httpStatus);

  const auto release = parseRelease(response, preferredAssetSuffix());
  if (!release.has_value()) {
    qWarning() << "version check response is invalid";
    return;
  }

  m_downloadUrl = QUrl();
  if (compareVersions(kVersion, release->version) > 0) {
    qWarning().noquote() //
        << QStringLiteral("current version %1 out of date, update available: %2").arg(kVersion, release->version);
    m_downloadUrl = release->downloadUrl;
    Q_EMIT updateFound(release->version);
  } else {
    qDebug().noquote() << QStringLiteral("current version %1 is upto date").arg(kVersion);
  }
}

std::optional<VersionChecker::Release> VersionChecker::parseRelease(const QByteArray &data, const QString &assetSuffix)
{
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }

  const auto object = document.object();
  const auto tagName = object.value(QStringLiteral("tag_name")).toString();
  static const QRegularExpression versionPattern(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+)$"));
  const auto versionMatch = versionPattern.match(tagName);
  if (!versionMatch.hasMatch()) {
    return std::nullopt;
  }

  const auto version = versionMatch.captured(1);
  const auto releaseUrl = QUrl(object.value(QStringLiteral("html_url")).toString());
  const auto releasePath = QStringLiteral("%1/tag/%2").arg(kReleasePath, tagName);
  if (!isTrustedReleaseUrl(releaseUrl, releasePath)) {
    return std::nullopt;
  }

  const auto assetName = QStringLiteral("deskflow-%1%2").arg(version, assetSuffix);
  for (const auto &value : object.value(QStringLiteral("assets")).toArray()) {
    const auto asset = value.toObject();
    if (asset.value(QStringLiteral("name")).toString() != assetName) {
      continue;
    }

    const auto assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
    const auto assetPath = QStringLiteral("%1/download/%2/%3").arg(kReleasePath, tagName, assetName);
    if (isTrustedReleaseUrl(assetUrl, assetPath)) {
      return Release{version, assetUrl};
    }
  }

  return Release{version, releaseUrl};
}

QString VersionChecker::preferredAssetSuffix()
{
  auto architecture = QSysInfo::currentCpuArchitecture().toLower();

#if defined(Q_OS_MACOS)
  if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("x86_64")) {
    return QStringLiteral("-macos-%1.dmg").arg(architecture);
  }
#elif defined(Q_OS_WIN)
  if (architecture == QStringLiteral("x86_64")) {
    architecture = QStringLiteral("x64");
  }
  if (architecture == QStringLiteral("x64") || architecture == QStringLiteral("arm64")) {
    return QStringLiteral("-win-%1.msi").arg(architecture);
  }
#endif

  return {};
}

int VersionChecker::getStageVersion(QString stage)
{
  const char *stableName = "stable";
  const char *rcName = "rc";
  const char *betaName = "beta";

  // use max int for stable so it's always the highest value.
  const int stableValue = INT_MAX;
  const int rcValue = 2;
  const int betaValue = 1;
  const int otherValue = 0;

  if (stage.isEmpty() || stage == stableName) {
    return stableValue;
  } else if (stage.startsWith(rcName, Qt::CaseInsensitive)) {
    static QRegularExpression re("\\d*", QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(stage);
    if (match.hasMatch()) {
      // return the rc value plus the rc number (e.g. 2 + 1)
      // this should be ok since stable is max int.
      return rcValue + match.captured(1).toInt();
    }
  } else if (stage == betaName) {
    return betaValue;
  }

  return otherValue;
}

int VersionChecker::compareVersions(const QString &left, const QString &right)
{
  if (left.compare(right) == 0)
    return 0; // versions are same.

  QStringList leftParts = left.split("-");
  QStringList rightParts = right.split("-");

  QString leftNumber = leftParts.at(0);
  QString rightNumber = rightParts.at(0);

  QStringList leftNumberParts = leftNumber.split(".");
  QStringList rightNumberParts = rightNumber.split(".");

  auto leftStagePart = leftParts.size() > 1 ? leftParts.at(1) : "";
  auto rightStagePart = rightParts.size() > 1 ? rightParts.at(1) : "";

  const int leftMajor = leftNumberParts.at(0).toInt();
  const int leftMinor = leftNumberParts.at(1).toInt();
  const int leftPatch = leftNumberParts.at(2).toInt();
  const int leftStage = getStageVersion(leftStagePart);

  const int rightMajor = rightNumberParts.at(0).toInt();
  const int rightMinor = rightNumberParts.at(1).toInt();
  const int rightPatch = rightNumberParts.at(2).toInt();
  const int rightStage = getStageVersion(rightStagePart);

  const bool rightWins =
      (rightMajor > leftMajor) || ((rightMajor >= leftMajor) && (rightMinor > leftMinor)) ||
      ((rightMajor >= leftMajor) && (rightMinor >= leftMinor) && (rightPatch > leftPatch)) ||
      ((rightMajor >= leftMajor) && (rightMinor >= leftMinor) && (rightPatch >= leftPatch) && (rightStage > leftStage));

  return rightWins ? 1 : -1;
}
