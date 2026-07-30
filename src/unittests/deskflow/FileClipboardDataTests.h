/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class FileClipboardDataTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void roundTripSingleFile();
  void roundTripMultipleFiles();
  void roundTripEmptyList();
  void roundTripBinaryContent();
  void unmarshallRejectsTruncatedHeader();
  void unmarshallRejectsTruncatedName();
  void unmarshallRejectsTruncatedData();
  void unmarshallRejectsOversizedLength();
  void sanitizeNameStripsDirectories();
  void sanitizeNameRejectsTraversal();
  void writeThenReadRoundTrip();
  void readFilesSkipsMissing();
  void readFilesRejectsSelectionOverLimit();

private:
  Log m_log;
};
