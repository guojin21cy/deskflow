/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>

class ClipboardHistory;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

//! Browse, restore, and manage clipboard history entries
class ClipboardHistoryDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ClipboardHistoryDialog(ClipboardHistory *history, QWidget *parent = nullptr);

private:
  void refreshList();
  void copySelected();
  void deleteSelected();
  void clearAll();
  void updateButtons();
  int selectedHistoryRow() const;

  ClipboardHistory *m_history;
  QLineEdit *m_searchEdit;
  QListWidget *m_list;
  QLabel *m_countLabel;
  QCheckBox *m_enableCheckBox;
  QPushButton *m_copyButton;
  QPushButton *m_deleteButton;
  QPushButton *m_clearButton;
};
