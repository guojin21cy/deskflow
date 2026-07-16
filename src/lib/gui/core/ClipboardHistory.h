/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

//! Records system clipboard changes so earlier copies can be restored
/*!
Watches QClipboard::dataChanged in the GUI process, which fires both for
local copies and for content synced from other screens by the core.
Entries are persisted under the settings directory so history survives
restarts.  Capture is skipped while the history itself restores an entry
to the clipboard.
*/
class ClipboardHistory : public QObject
{
  Q_OBJECT

public:
  enum class EntryType
  {
    Text,
    Image,
    Files
  };

  struct Entry
  {
    EntryType type = EntryType::Text;
    QString text;      //!< text content; for Files, newline-separated paths
    QString imageFile; //!< PNG file name inside the storage dir (Image only)
    QSize imageSize;   //!< pixel size (Image only)
    QDateTime time;
  };

  explicit ClipboardHistory(QObject *parent = nullptr);

  const QList<Entry> &entries() const
  {
    return m_entries;
  }

  //! Copy the entry at \p row back onto the system clipboard.
  void restore(int row);

  //! Remove the entry at \p row (deletes its image file, if any).
  void removeAt(int row);

  //! Remove all entries and their image files.
  void clear();

  //! Load the full image for an Image entry (null when missing).
  QImage imageForEntry(const Entry &entry) const;

Q_SIGNALS:
  void historyChanged();

private:
  void onClipboardChanged();
  void prepend(Entry entry, const QImage &image);
  void trimToLimit();
  void deleteImageFile(const Entry &entry) const;
  void load();
  void save() const;
  QString storageDir() const;

  // largest text captured; larger copies are ignored to keep storage sane
  static constexpr qsizetype kMaxTextBytes = 1 * 1024 * 1024; // 1 MiB

  QList<Entry> m_entries;
  bool m_suppressCapture = false;
};
