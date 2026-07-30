/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VersionCheckerTests.h"

#include "gui/VersionChecker.h"

void VersionCheckerTests::parsesMatchingInstaller()
{
  const QByteArray response = R"json({
    "tag_name": "v1.26.1",
    "html_url": "https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1",
    "assets": [
      {
        "name": "deskflow-1.26.1-win-x64.msi",
        "browser_download_url": "https://github.com/guojin21cy/deskflow/releases/download/v1.26.1/deskflow-1.26.1-win-x64.msi"
      },
      {
        "name": "deskflow-1.26.1-macos-arm64.dmg",
        "browser_download_url": "https://github.com/guojin21cy/deskflow/releases/download/v1.26.1/deskflow-1.26.1-macos-arm64.dmg"
      }
    ]
  })json";

  const auto release = VersionChecker::parseRelease(response, QStringLiteral("-macos-arm64.dmg"));

  QVERIFY(release.has_value());
  QCOMPARE(release->version, QStringLiteral("1.26.1"));
  QCOMPARE(
      release->downloadUrl,
      QUrl(QStringLiteral(
          "https://github.com/guojin21cy/deskflow/releases/download/v1.26.1/"
          "deskflow-1.26.1-macos-arm64.dmg"
      ))
  );
}

void VersionCheckerTests::fallsBackToReleasePage()
{
  const QByteArray response = R"json({
    "tag_name": "v1.26.1",
    "html_url": "https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1",
    "assets": [
      {
        "name": "other-deskflow-1.26.1-macos-arm64.dmg",
        "browser_download_url": "https://github.com/guojin21cy/deskflow/releases/download/v1.26.1/other-deskflow-1.26.1-macos-arm64.dmg"
      }
    ]
  })json";

  const auto release = VersionChecker::parseRelease(response, QStringLiteral("-macos-arm64.dmg"));

  QVERIFY(release.has_value());
  QCOMPARE(
      release->downloadUrl,
      QUrl(QStringLiteral("https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1"))
  );
}

void VersionCheckerTests::rejectsUntrustedReleasePage()
{
  const QByteArray response = R"json({
    "tag_name": "v1.26.1",
    "html_url": "http://github.com/guojin21cy/deskflow/releases/tag/v1.26.1",
    "assets": []
  })json";

  QVERIFY(!VersionChecker::parseRelease(response, QStringLiteral("-macos-arm64.dmg")).has_value());
}

void VersionCheckerTests::ignoresUntrustedAsset()
{
  const QByteArray response = R"json({
    "tag_name": "v1.26.1",
    "html_url": "https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1",
    "assets": [
      {
        "name": "deskflow-1.26.1-macos-arm64.dmg",
        "browser_download_url": "https://example.com/deskflow-1.26.1-macos-arm64.dmg"
      }
    ]
  })json";

  const auto release = VersionChecker::parseRelease(response, QStringLiteral("-macos-arm64.dmg"));

  QVERIFY(release.has_value());
  QCOMPARE(
      release->downloadUrl,
      QUrl(QStringLiteral("https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1"))
  );
}

void VersionCheckerTests::ignoresMismatchedAssetPath()
{
  const QByteArray response = R"json({
    "tag_name": "v1.26.1",
    "html_url": "https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1",
    "assets": [
      {
        "name": "deskflow-1.26.1-macos-arm64.dmg",
        "browser_download_url": "https://github.com/guojin21cy/deskflow/releases/download/v1.26.1/other.dmg"
      }
    ]
  })json";

  const auto release = VersionChecker::parseRelease(response, QStringLiteral("-macos-arm64.dmg"));

  QVERIFY(release.has_value());
  QCOMPARE(
      release->downloadUrl,
      QUrl(QStringLiteral("https://github.com/guojin21cy/deskflow/releases/tag/v1.26.1"))
  );
}

void VersionCheckerTests::rejectsInvalidResponse()
{
  QVERIFY(!VersionChecker::parseRelease(QByteArrayLiteral("{}"), QStringLiteral("-macos-arm64.dmg")).has_value());
}

QTEST_MAIN(VersionCheckerTests)
