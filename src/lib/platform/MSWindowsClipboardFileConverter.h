/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert to/from the CF_HDROP (file drop) clipboard format
/*!
Bridges Windows file copy/paste and the IClipboard::Format::Files bundle.
On paste (fromIClipboard) the bundled files are written to a temporary
directory and referenced by a DROPFILES structure; on copy (toIClipboard)
the dropped files are read from disk and packed into a bundle.
*/
class MSWindowsClipboardFileConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardFileConverter() = default;
  ~MSWindowsClipboardFileConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &data) const override;
  std::string toIClipboard(HANDLE data) const override;
};
