/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deskflow {

//! A single file carried on the clipboard
struct ClipboardFile
{
  std::string name; //!< base file name (no directory components)
  std::string data; //!< raw file contents
};

//! Serialises files for the IClipboard::Format::Files clipboard format
/*!
The bundle embeds both file names and contents so that the existing
clipboard synchronisation machinery can carry files between screens.

Wire format (all integers big-endian, matching IClipboard marshalling):
\code
[4 bytes]  file count
  repeated for each file:
  [4 bytes]  name length n
  [n bytes]  UTF-8 file name
  [8 bytes]  data length m
  [m bytes]  file contents
\endcode
*/
class FileClipboardData
{
public:
  //! Pack \p files into a single buffer.
  static std::string marshall(const std::vector<ClipboardFile> &files);

  //! Unpack \p buffer into \p files.  Returns false if the buffer is malformed.
  static bool unmarshall(const std::string_view &buffer, std::vector<ClipboardFile> &files);

  //! Reduce \p path to a safe base file name.
  /*!
  Strips any directory components (both '/' and '\\' separators) and
  rejects traversal names.  Returns "file" when nothing usable remains,
  so a bundle can never write outside its target directory.
  */
  static std::string sanitizeName(const std::string &path);

  //! Upper bound on the total bytes read by readFiles().
  /*!
  A safety valve so that copying very large files cannot exhaust memory
  before the (smaller, configurable) clipboard size limit rejects the
  transfer downstream.
  */
  static constexpr uint64_t kMaxTotalBytes = 2ull * 1024 * 1024 * 1024; // 2 GiB

  //! Read a file from disk into a ClipboardFile.
  /*!
  \p path is UTF-8 encoded.  Sets \p out.name to the sanitised base name
  of \p path and \p out.data to the whole file contents.  Returns false
  if the file cannot be read.
  */
  static bool readFile(const std::string &path, ClipboardFile &out);

  //! Read several files, guarding against excessive total size.
  /*!
  \p paths are UTF-8 encoded.  Files that cannot be read are skipped.
  Returns false (and clears \p out) if the combined size exceeds
  \p maxTotalBytes or kMaxTotalBytes, or if no file could be read.
  */
  static bool readFiles(
      const std::vector<std::string> &paths, std::vector<ClipboardFile> &out, uint64_t maxTotalBytes = kMaxTotalBytes
  );

  //! Write \p files to a fresh unique directory under the system temp dir.
  /*!
  Each file is written under its sanitised name.  Returns the absolute,
  UTF-8 encoded paths of the files written, in the same order as \p files;
  the returned vector is empty if the target directory could not be created.
  */
  static std::vector<std::string> writeToTempDir(const std::vector<ClipboardFile> &files);
};

} // namespace deskflow
