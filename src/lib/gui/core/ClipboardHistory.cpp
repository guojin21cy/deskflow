/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistory.h"

#include "common/Settings.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>
#include <QUuid>

namespace {

const auto kHistoryFile = QStringLiteral("history.json");

QString entryTypeToString(ClipboardHistory::EntryType type)
{
  switch (type) {
  case ClipboardHistory::EntryType::Image:
    return QStringLiteral("image");
  case ClipboardHistory::EntryType::Files:
    return QStringLiteral("files");
  default:
    return QStringLiteral("text");
  }
}

ClipboardHistory::EntryType entryTypeFromString(const QString &type)
{
  if (type == QLatin1String("image"))
    return ClipboardHistory::EntryType::Image;
  if (type == QLatin1String("files"))
    return ClipboardHistory::EntryType::Files;
  return ClipboardHistory::EntryType::Text;
}

} // namespace

ClipboardHistory::ClipboardHistory(QObject *parent) : QObject(parent)
{
  load();
  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardHistory::onClipboardChanged);
}

void ClipboardHistory::onClipboardChanged()
{
  if (m_suppressCapture) {
    m_suppressCapture = false;
    return;
  }

  if (!Settings::value(Settings::Gui::ClipboardHistoryEnabled).toBool())
    return;

  const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
  if (mime == nullptr)
    return;

  Entry entry;
  entry.time = QDateTime::currentDateTime();
  QImage image;

  if (mime->hasUrls()) {
    QStringList paths;
    for (const QUrl &url : mime->urls()) {
      if (url.isLocalFile())
        paths.append(url.toLocalFile());
    }
    if (paths.isEmpty())
      return;
    entry.type = EntryType::Files;
    entry.text = paths.join(QLatin1Char('\n'));
  } else if (mime->hasImage()) {
    image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull())
      return;
    entry.type = EntryType::Image;
    entry.imageSize = image.size();
  } else if (mime->hasText()) {
    const QString text = mime->text();
    if (text.trimmed().isEmpty() || text.size() > kMaxTextBytes)
      return;
    entry.type = EntryType::Text;
    entry.text = text;
  } else {
    return;
  }

  prepend(std::move(entry), image);
}

void ClipboardHistory::prepend(Entry entry, const QImage &image)
{
  // dedupe: same content moves to the top instead of repeating
  for (qsizetype i = 0; i < m_entries.size(); ++i) {
    const Entry &existing = m_entries.at(i);
    if (existing.type != entry.type)
      continue;

    bool same = false;
    if (entry.type == EntryType::Image)
      same = existing.imageSize == entry.imageSize && imageForEntry(existing) == image;
    else
      same = existing.text == entry.text;

    if (same) {
      Entry moved = m_entries.takeAt(i);
      moved.time = entry.time;
      m_entries.prepend(moved);
      save();
      Q_EMIT historyChanged();
      return;
    }
  }

  if (entry.type == EntryType::Image) {
    QDir dir(storageDir());
    if (!dir.mkpath(QStringLiteral(".")))
      return;
    entry.imageFile = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
    if (!image.save(dir.filePath(entry.imageFile), "PNG"))
      return;
  }

  m_entries.prepend(std::move(entry));
  trimToLimit();
  save();
  Q_EMIT historyChanged();
}

void ClipboardHistory::trimToLimit()
{
  const int maxItems = qMax(1, Settings::value(Settings::Gui::ClipboardHistoryMaxItems).toInt());
  while (m_entries.size() > maxItems) {
    deleteImageFile(m_entries.last());
    m_entries.removeLast();
  }
}

void ClipboardHistory::restore(int row)
{
  if (row < 0 || row >= m_entries.size())
    return;

  const Entry &entry = m_entries.at(row);
  QClipboard *clipboard = QGuiApplication::clipboard();

  switch (entry.type) {
  case EntryType::Image: {
    const QImage image = imageForEntry(entry);
    if (!image.isNull()) {
      m_suppressCapture = true;
      clipboard->setImage(image);
    }
    break;
  }
  case EntryType::Files: {
    auto *mime = new QMimeData();
    QList<QUrl> urls;
    const QStringList paths = entry.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &path : paths)
      urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);
    m_suppressCapture = true;
    clipboard->setMimeData(mime);
    break;
  }
  default:
    m_suppressCapture = true;
    clipboard->setText(entry.text);
    break;
  }
}

void ClipboardHistory::removeAt(int row)
{
  if (row < 0 || row >= m_entries.size())
    return;
  deleteImageFile(m_entries.at(row));
  m_entries.removeAt(row);
  save();
  Q_EMIT historyChanged();
}

void ClipboardHistory::clear()
{
  for (const Entry &entry : std::as_const(m_entries))
    deleteImageFile(entry);
  m_entries.clear();
  save();
  Q_EMIT historyChanged();
}

QImage ClipboardHistory::imageForEntry(const Entry &entry) const
{
  if (entry.type != EntryType::Image || entry.imageFile.isEmpty())
    return QImage();
  return QImage(QDir(storageDir()).filePath(entry.imageFile));
}

void ClipboardHistory::deleteImageFile(const Entry &entry) const
{
  if (entry.type == EntryType::Image && !entry.imageFile.isEmpty())
    QFile::remove(QDir(storageDir()).filePath(entry.imageFile));
}

void ClipboardHistory::load()
{
  QFile file(QDir(storageDir()).filePath(kHistoryFile));
  if (!file.open(QIODevice::ReadOnly))
    return;

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isArray())
    return;

  for (const QJsonValue &value : doc.array()) {
    const QJsonObject obj = value.toObject();
    Entry entry;
    entry.type = entryTypeFromString(obj.value(QStringLiteral("type")).toString());
    entry.text = obj.value(QStringLiteral("text")).toString();
    entry.imageFile = obj.value(QStringLiteral("imageFile")).toString();
    entry.imageSize =
        QSize(obj.value(QStringLiteral("imageWidth")).toInt(), obj.value(QStringLiteral("imageHeight")).toInt());
    entry.time = QDateTime::fromString(obj.value(QStringLiteral("time")).toString(), Qt::ISODate);
    m_entries.append(std::move(entry));
  }
}

void ClipboardHistory::save() const
{
  QDir dir(storageDir());
  if (!dir.mkpath(QStringLiteral(".")))
    return;

  QJsonArray array;
  for (const Entry &entry : m_entries) {
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), entryTypeToString(entry.type));
    if (entry.type == EntryType::Image) {
      obj.insert(QStringLiteral("imageFile"), entry.imageFile);
      obj.insert(QStringLiteral("imageWidth"), entry.imageSize.width());
      obj.insert(QStringLiteral("imageHeight"), entry.imageSize.height());
    } else {
      obj.insert(QStringLiteral("text"), entry.text);
    }
    obj.insert(QStringLiteral("time"), entry.time.toString(Qt::ISODate));
    array.append(obj);
  }

  QFile file(dir.filePath(kHistoryFile));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString ClipboardHistory::storageDir() const
{
  return QStringLiteral("%1/clipboard-history").arg(Settings::settingsPath());
}
