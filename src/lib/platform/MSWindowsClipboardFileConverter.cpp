/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFileConverter.h"

#include "base/Log.h"
#include "deskflow/FileClipboardData.h"

#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>

namespace {

std::wstring utf8ToWide(const std::string &utf8)
{
  if (utf8.empty()) {
    return std::wstring();
  }
  const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (len <= 0) {
    return std::wstring();
  }
  std::wstring wide(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
  return wide;
}

std::string wideToUtf8(const wchar_t *wide, int wideLen)
{
  if (wide == nullptr || wideLen <= 0) {
    return std::string();
  }
  const int len = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return std::string();
  }
  std::string utf8(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, utf8.data(), len, nullptr, nullptr);
  return utf8;
}

} // namespace

IClipboard::Format MSWindowsClipboardFileConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFileConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFileConverter::fromIClipboard(const std::string &data) const
{
  std::vector<deskflow::ClipboardFile> files;
  if (!deskflow::FileClipboardData::unmarshall(data, files) || files.empty()) {
    return nullptr;
  }

  // materialise the bundled files; CF_HDROP references them by path
  const std::vector<std::string> paths = deskflow::FileClipboardData::writeToTempDir(files);
  if (paths.empty()) {
    LOG_WARN("failed to write pasted files to disk");
    return nullptr;
  }

  // build a double-null-terminated list of wide paths
  std::wstring list;
  for (const auto &path : paths) {
    list += utf8ToWide(path);
    list += L'\0';
  }
  list += L'\0';

  const size_t listBytes = list.size() * sizeof(wchar_t);
  const size_t totalBytes = sizeof(DROPFILES) + listBytes;

  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, totalBytes);
  if (handle == nullptr) {
    LOG_WARN("failed to allocate CF_HDROP data");
    return nullptr;
  }

  auto *drop = static_cast<DROPFILES *>(GlobalLock(handle));
  if (drop == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  drop->pFiles = sizeof(DROPFILES);
  drop->pt.x = 0;
  drop->pt.y = 0;
  drop->fNC = FALSE;
  drop->fWide = TRUE;
  memcpy(reinterpret_cast<uint8_t *>(drop) + sizeof(DROPFILES), list.data(), listBytes);

  GlobalUnlock(handle);
  return handle;
}

std::string MSWindowsClipboardFileConverter::toIClipboard(HANDLE data) const
{
  auto hDrop = static_cast<HDROP>(data);
  if (hDrop == nullptr) {
    return std::string();
  }

  const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
  if (count == 0) {
    return std::string();
  }

  std::vector<std::string> paths;
  for (UINT i = 0; i < count; ++i) {
    const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
    if (len == 0) {
      continue;
    }

    std::wstring widePath(static_cast<size_t>(len) + 1, L'\0');
    const UINT copied = DragQueryFileW(hDrop, i, widePath.data(), len + 1);
    if (copied == 0) {
      continue;
    }
    widePath.resize(copied);

    const std::string path = wideToUtf8(widePath.data(), static_cast<int>(widePath.size()));
    if (!path.empty()) {
      paths.push_back(path);
    }
  }

  std::vector<deskflow::ClipboardFile> files;
  if (!deskflow::FileClipboardData::readFiles(paths, files)) {
    return std::string();
  }

  LOG_DEBUG("read %zu file(s) from clipboard", files.size());
  return deskflow::FileClipboardData::marshall(files);
}
