/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryDialog.h"

#include "common/Settings.h"
#include "gui/core/ClipboardHistory.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

const int kHistoryRowRole = Qt::UserRole;
const QSize kThumbnailSize(48, 36);

QString elidedSnippet(const QString &text)
{
  QString snippet = text.simplified();
  if (snippet.size() > 100) {
    snippet.truncate(100);
    snippet.append(QStringLiteral("…"));
  }
  return snippet;
}

} // namespace

ClipboardHistoryDialog::ClipboardHistoryDialog(ClipboardHistory *history, QWidget *parent)
    : QDialog(parent),
      m_history(history),
      m_searchEdit(new QLineEdit(this)),
      m_list(new QListWidget(this)),
      m_countLabel(new QLabel(this)),
      m_enableCheckBox(new QCheckBox(tr("Save history"), this)),
      m_copyButton(new QPushButton(tr("Copy"), this)),
      m_deleteButton(new QPushButton(tr("Delete"), this)),
      m_clearButton(new QPushButton(tr("Clear all"), this))
{
  setWindowTitle(tr("Clipboard History"));
  resize(480, 520);

  m_searchEdit->setPlaceholderText(tr("Search history…"));
  m_searchEdit->setClearButtonEnabled(true);

  m_list->setIconSize(kThumbnailSize);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setUniformItemSizes(false);
  m_list->setWordWrap(false);
  m_list->setTextElideMode(Qt::ElideRight);

  m_copyButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
  m_deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
  m_clearButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-all")));

  m_enableCheckBox->setChecked(Settings::value(Settings::Gui::ClipboardHistoryEnabled).toBool());
  m_enableCheckBox->setToolTip(tr("When unchecked, new clipboard changes are not recorded"));

  auto *searchRow = new QHBoxLayout();
  searchRow->addWidget(m_searchEdit, 1);
  searchRow->addWidget(m_countLabel);

  auto *buttonRow = new QHBoxLayout();
  buttonRow->addWidget(m_enableCheckBox);
  buttonRow->addStretch(1);
  buttonRow->addWidget(m_copyButton);
  buttonRow->addWidget(m_deleteButton);
  buttonRow->addWidget(m_clearButton);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(searchRow);
  layout->addWidget(m_list, 1);
  layout->addLayout(buttonRow);

  connect(m_searchEdit, &QLineEdit::textChanged, this, &ClipboardHistoryDialog::refreshList);
  connect(m_list, &QListWidget::itemSelectionChanged, this, &ClipboardHistoryDialog::updateButtons);
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { copySelected(); });
  connect(m_copyButton, &QPushButton::clicked, this, &ClipboardHistoryDialog::copySelected);
  connect(m_deleteButton, &QPushButton::clicked, this, &ClipboardHistoryDialog::deleteSelected);
  connect(m_clearButton, &QPushButton::clicked, this, &ClipboardHistoryDialog::clearAll);
  connect(m_enableCheckBox, &QCheckBox::toggled, this, [](bool checked) {
    Settings::setValue(Settings::Gui::ClipboardHistoryEnabled, checked);
  });
  connect(m_history, &ClipboardHistory::historyChanged, this, &ClipboardHistoryDialog::refreshList);

  refreshList();
}

void ClipboardHistoryDialog::refreshList()
{
  const QString filter = m_searchEdit->text();
  m_list->clear();

  const auto &entries = m_history->entries();
  for (qsizetype row = 0; row < entries.size(); ++row) {
    const ClipboardHistory::Entry &entry = entries.at(row);

    QString snippet;
    QIcon icon;
    QString typeName;

    switch (entry.type) {
    case ClipboardHistory::EntryType::Image: {
      typeName = tr("Image");
      snippet = tr("Image %1 × %2").arg(entry.imageSize.width()).arg(entry.imageSize.height());
      const QImage image = m_history->imageForEntry(entry);
      if (!image.isNull())
        icon = QIcon(QPixmap::fromImage(image.scaled(kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      else
        icon = style()->standardIcon(QStyle::SP_FileIcon);
      break;
    }
    case ClipboardHistory::EntryType::Files: {
      typeName = tr("Files");
      const QStringList paths = entry.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
      snippet = paths.isEmpty() ? tr("Files") : QFileInfo(paths.first()).fileName();
      if (paths.size() > 1)
        snippet.append(tr(" +%1 more").arg(paths.size() - 1));
      icon = style()->standardIcon(QStyle::SP_DirIcon);
      break;
    }
    default:
      typeName = tr("Text");
      snippet = elidedSnippet(entry.text);
      icon = style()->standardIcon(QStyle::SP_FileIcon);
      break;
    }

    if (!filter.isEmpty() && !snippet.contains(filter, Qt::CaseInsensitive) &&
        !entry.text.contains(filter, Qt::CaseInsensitive)) {
      continue;
    }

    const QString timeText = QLocale().toString(entry.time, QLocale::ShortFormat);
    auto *item = new QListWidgetItem(icon, QStringLiteral("%1\n%2 · %3").arg(snippet, typeName, timeText));
    item->setData(kHistoryRowRole, static_cast<int>(row));
    if (entry.type == ClipboardHistory::EntryType::Text)
      item->setToolTip(entry.text.left(1000));
    else if (entry.type == ClipboardHistory::EntryType::Files)
      item->setToolTip(entry.text);
    m_list->addItem(item);
  }

  m_countLabel->setText(tr("%n item(s)", "", static_cast<int>(entries.size())));
  updateButtons();
}

int ClipboardHistoryDialog::selectedHistoryRow() const
{
  const auto items = m_list->selectedItems();
  if (items.isEmpty())
    return -1;
  return items.first()->data(kHistoryRowRole).toInt();
}

void ClipboardHistoryDialog::copySelected()
{
  const int row = selectedHistoryRow();
  if (row >= 0)
    m_history->restore(row);
}

void ClipboardHistoryDialog::deleteSelected()
{
  const int row = selectedHistoryRow();
  if (row >= 0)
    m_history->removeAt(row);
}

void ClipboardHistoryDialog::clearAll()
{
  if (m_history->entries().isEmpty())
    return;

  const auto reply = QMessageBox::question(
      this, tr("Clear clipboard history"), tr("Remove all clipboard history entries? This cannot be undone.")
  );
  if (reply == QMessageBox::Yes)
    m_history->clear();
}

void ClipboardHistoryDialog::updateButtons()
{
  const bool hasSelection = selectedHistoryRow() >= 0;
  m_copyButton->setEnabled(hasSelection);
  m_deleteButton->setEnabled(hasSelection);
  m_clearButton->setEnabled(!m_history->entries().isEmpty());
}
