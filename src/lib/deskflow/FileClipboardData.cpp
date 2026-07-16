/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileClipboardData.h"

#include "base/Log.h"

#include <filesystem>
#include <fstream>
#include <random>

namespace deskflow {

namespace {

void writeUInt32(std::string &buf, uint32_t v)
{
  buf += static_cast<char>((v >> 24) & 0xff);
  buf += static_cast<char>((v >> 16) & 0xff);
  buf += static_cast<char>((v >> 8) & 0xff);
  buf += static_cast<char>(v & 0xff);
}

void writeUInt64(std::string &buf, uint64_t v)
{
  for (int shift = 56; shift >= 0; shift -= 8) {
    buf += static_cast<char>((v >> shift) & 0xff);
  }
}

uint32_t readUInt32(const char *buf)
{
  const auto *b = reinterpret_cast<const unsigned char *>(buf);
  return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

uint64_t readUInt64(const char *buf)
{
  const auto *b = reinterpret_cast<const unsigned char *>(buf);
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | b[i];
  }
  return v;
}

// Build a filesystem path from UTF-8 bytes.  On Windows fs::path(std::string)
// assumes the active code page, so we go through u8string to keep names intact.
std::filesystem::path pathFromUtf8(const std::string &utf8)
{
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

// Encode a filesystem path back to UTF-8 bytes.
std::string pathToUtf8(const std::filesystem::path &path)
{
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

} // namespace

std::string FileClipboardData::marshall(const std::vector<ClipboardFile> &files)
{
  std::string out;

  writeUInt32(out, static_cast<uint32_t>(files.size()));
  for (const auto &file : files) {
    writeUInt32(out, static_cast<uint32_t>(file.name.size()));
    out += file.name;
    writeUInt64(out, static_cast<uint64_t>(file.data.size()));
    out += file.data;
  }

  return out;
}

bool FileClipboardData::unmarshall(const std::string_view &buffer, std::vector<ClipboardFile> &files)
{
  files.clear();

  const char *index = buffer.data();
  const char *const end = index + buffer.size();

  if (end - index < 4) {
    return false;
  }
  const uint32_t count = readUInt32(index);
  index += 4;

  for (uint32_t i = 0; i < count; ++i) {
    // name length + name
    if (end - index < 4) {
      return false;
    }
    const uint32_t nameLen = readUInt32(index);
    index += 4;
    if (nameLen > static_cast<uint64_t>(end - index)) {
      return false;
    }
    ClipboardFile file;
    file.name.assign(index, nameLen);
    index += nameLen;

    // data length + data
    if (end - index < 8) {
      return false;
    }
    const uint64_t dataLen = readUInt64(index);
    index += 8;
    if (dataLen > static_cast<uint64_t>(end - index)) {
      return false;
    }
    file.data.assign(index, static_cast<size_t>(dataLen));
    index += dataLen;

    files.push_back(std::move(file));
  }

  return true;
}

std::string FileClipboardData::sanitizeName(const std::string &path)
{
  // keep only the component after the last path separator (either style)
  std::string::size_type pos = path.find_last_of("/\\");
  std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);

  // reject empty and traversal names
  if (name.empty() || name == "." || name == "..") {
    return "file";
  }

  return name;
}

bool FileClipboardData::readFile(const std::string &path, ClipboardFile &out)
{
  std::ifstream stream(pathFromUtf8(path), std::ios::binary);
  if (!stream) {
    LOG_WARN("clipboard: cannot open file for reading: %s", path.c_str());
    return false;
  }

  std::string data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (stream.bad()) {
    LOG_WARN("clipboard: error while reading file: %s", path.c_str());
    return false;
  }

  out.name = sanitizeName(path);
  out.data = std::move(data);
  return true;
}

bool FileClipboardData::readFiles(const std::vector<std::string> &paths, std::vector<ClipboardFile> &out)
{
  out.clear();

  // sum sizes first so a huge selection is rejected before we read anything
  std::error_code ec;
  uint64_t total = 0;
  for (const auto &path : paths) {
    const auto size = std::filesystem::file_size(pathFromUtf8(path), ec);
    if (ec) {
      ec.clear();
      continue; // unreadable file, skipped during the read pass too
    }
    total += size;
    if (total > kMaxTotalBytes) {
      LOG_WARN("clipboard: file selection too large to copy (%llu bytes)", static_cast<unsigned long long>(total));
      return false;
    }
  }

  for (const auto &path : paths) {
    ClipboardFile file;
    if (readFile(path, file)) {
      out.push_back(std::move(file));
    }
  }

  return !out.empty();
}

std::vector<std::string> FileClipboardData::writeToTempDir(const std::vector<ClipboardFile> &files)
{
  std::vector<std::string> paths;

  namespace fs = std::filesystem;
  std::error_code ec;

  // build a unique directory under the system temp dir
  std::random_device rd;
  const auto suffix = std::to_string(rd()) + std::to_string(rd());
  fs::path dir = fs::temp_directory_path(ec) / ("deskflow-clipboard-" + suffix);
  if (ec || !fs::create_directories(dir, ec) || ec) {
    LOG_WARN("clipboard: cannot create temp dir for pasted files: %s", ec.message().c_str());
    return paths;
  }

  for (const auto &file : files) {
    fs::path target = dir / pathFromUtf8(sanitizeName(file.name));
    const std::string targetUtf8 = pathToUtf8(target);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    if (!stream) {
      LOG_WARN("clipboard: cannot write pasted file: %s", targetUtf8.c_str());
      continue;
    }
    stream.write(file.data.data(), static_cast<std::streamsize>(file.data.size()));
    stream.close();
    if (!stream) {
      LOG_WARN("clipboard: error writing pasted file: %s", targetUtf8.c_str());
      continue;
    }
    paths.push_back(targetUtf8);
  }

  return paths;
}

} // namespace deskflow
