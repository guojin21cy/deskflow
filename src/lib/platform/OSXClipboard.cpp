/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboard.h"

#include "arch/ArchException.h"
#include "base/Log.h"
#include "deskflow/FileClipboardData.h"
#include "platform/OSXClipboardBMPConverter.h"
#include "platform/OSXClipboardHTMLConverter.h"
#include "platform/OSXClipboardTextConverter.h"
#include "platform/OSXClipboardUTF16Converter.h"
#include "platform/OSXClipboardUTF8Converter.h"

//
// OSXClipboard
//

OSXClipboard::OSXClipboard() : m_time(0), m_pboard(nullptr)
{
  m_converters.push_back(new OSXClipboardHTMLConverter);
  m_converters.push_back(new OSXClipboardBMPConverter);
  m_converters.push_back(new OSXClipboardUTF8Converter);
  m_converters.push_back(new OSXClipboardUTF16Converter);
  m_converters.push_back(new OSXClipboardTextConverter);

  OSStatus createErr = PasteboardCreate(kPasteboardClipboard, &m_pboard);
  if (createErr != noErr) {
    LOG_WARN("failed to create clipboard reference: error %i", createErr);
    LOG_ERR("unable to connect to pasteboard, clipboard sharing disabled", createErr);
    m_pboard = nullptr;
    return;
  }

  OSStatus syncErr = PasteboardSynchronize(m_pboard);
  if (syncErr != noErr) {
    LOG_WARN("failed to syncronize clipboard: error %i", syncErr);
  }
}

OSXClipboard::~OSXClipboard()
{
  clearConverters();
}

bool OSXClipboard::empty()
{
  LOG_DEBUG("emptying clipboard");
  if (m_pboard == nullptr)
    return false;

  OSStatus err = PasteboardClear(m_pboard);
  if (err != noErr) {
    LOG_WARN("failed to clear clipboard: error %i", err);
    return false;
  }

  return true;
}

bool OSXClipboard::synchronize()
{
  if (m_pboard == nullptr)
    return false;

  PasteboardSyncFlags flags = PasteboardSynchronize(m_pboard);
  LOG_VERBOSE("flags: %x", flags);

  if (flags & kPasteboardModified) {
    return true;
  }
  return false;
}

void OSXClipboard::add(Format format, const std::string &data)
{
  if (m_pboard == nullptr)
    return;

  if (format == IClipboard::Format::Files) {
    addFiles(data);
    return;
  }

  LOG_DEBUG("add %d bytes to clipboard format: %d", data.size(), format);
  if (format == IClipboard::Format::Text) {
    LOG_DEBUG("format of data to be added to clipboard was kText");
  } else if (format == IClipboard::Format::Bitmap) {
    LOG_DEBUG("format of data to be added to clipboard was kBitmap");
  } else if (format == IClipboard::Format::HTML) {
    LOG_DEBUG("format of data to be added to clipboard was kHTML");
  }

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {

    IOSXClipboardConverter *converter = *index;

    // skip converters for other formats
    if (converter->getFormat() == format) {
      std::string osXData = converter->fromIClipboard(data);
      CFStringRef flavorType = converter->getOSXFormat();
      CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, (uint8_t *)osXData.data(), osXData.size());
      PasteboardItemID itemID = 0;

      if (dataRef) {
        PasteboardPutItemFlavor(m_pboard, itemID, flavorType, dataRef, kPasteboardFlavorNoFlags);

        CFRelease(dataRef);
        LOG_DEBUG("added %d bytes to clipboard format: %d", data.size(), format);
      }
    }
  }
}

bool OSXClipboard::open(Time time) const
{
  if (m_pboard == nullptr)
    return false;

  LOG_DEBUG("opening clipboard");
  m_time = time;
  return true;
}

void OSXClipboard::close() const
{
  LOG_DEBUG("closing clipboard");
  /* not needed */
}

IClipboard::Time OSXClipboard::getTime() const
{
  return m_time;
}

bool OSXClipboard::has(Format format) const
{
  if (m_pboard == nullptr)
    return false;

  if (format == IClipboard::Format::Files) {
    return hasFiles();
  }

  PasteboardItemID item;
  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    IOSXClipboardConverter *converter = *index;
    if (converter->getFormat() == format) {
      PasteboardFlavorFlags flags;
      CFStringRef type = converter->getOSXFormat();

      OSStatus res;

      if ((res = PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags)) == noErr) {
        return true;
      }
    }
  }

  return false;
}

std::string OSXClipboard::get(Format format) const
{
  CFStringRef type;
  PasteboardItemID item;
  std::string result;

  if (m_pboard == nullptr)
    return result;

  if (format == IClipboard::Format::Files) {
    return getFiles();
  }

  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  // find the converter for the first clipboard format we can handle
  IOSXClipboardConverter *converter = nullptr;
  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    converter = *index;

    PasteboardFlavorFlags flags;
    type = converter->getOSXFormat();

    if (converter->getFormat() == format && PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
      break;
    }
    converter = nullptr;
  }

  // if no converter then we don't recognize any formats
  if (converter == nullptr) {
    LOG_DEBUG("unable to find converter for data");
    return result;
  }

  // get the clipboard data.
  CFDataRef buffer = nullptr;
  try {
    OSStatus err = PasteboardCopyItemFlavorData(m_pboard, item, type, &buffer);

    if (err != noErr) {
      throw err;
    }

    result = std::string((char *)CFDataGetBytePtr(buffer), CFDataGetLength(buffer));
  } catch (OSStatus err) {
    LOG_DEBUG("exception thrown in OSXClipboard::get MacError (%d)", err);
  } catch (...) {
    LOG_DEBUG("unknown exception in OSXClipboard::get");
    RETHROW_THREADEXCEPTION
  }

  if (buffer != nullptr)
    CFRelease(buffer);

  return converter->toIClipboard(result);
}

namespace {

const CFStringRef kFileURLType = CFSTR("public.file-url");

// Resolve a pasteboard file-url flavor payload to a POSIX path.
std::string fileURLDataToPath(CFDataRef data)
{
  if (data == nullptr) {
    return std::string();
  }

  CFURLRef url = CFURLCreateWithBytes(
      kCFAllocatorDefault, CFDataGetBytePtr(data), CFDataGetLength(data), kCFStringEncodingUTF8, nullptr
  );
  if (url == nullptr) {
    return std::string();
  }

  std::string path;
  if (CFStringRef fsPath = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle); fsPath != nullptr) {
    if (const char *cstr = CFStringGetCStringPtr(fsPath, kCFStringEncodingUTF8); cstr != nullptr) {
      path = cstr;
    } else {
      // fall back to copying into a buffer sized from the string length
      const CFIndex maxLen = CFStringGetMaximumSizeForEncoding(CFStringGetLength(fsPath), kCFStringEncodingUTF8) + 1;
      std::string buffer(static_cast<size_t>(maxLen), '\0');
      if (CFStringGetCString(fsPath, buffer.data(), maxLen, kCFStringEncodingUTF8)) {
        path = buffer.c_str();
      }
    }
    CFRelease(fsPath);
  }

  CFRelease(url);
  return path;
}

} // namespace

bool OSXClipboard::hasFiles() const
{
  ItemCount count = 0;
  if (PasteboardGetItemCount(m_pboard, &count) != noErr) {
    return false;
  }

  for (ItemCount i = 1; i <= count; ++i) {
    PasteboardItemID item;
    if (PasteboardGetItemIdentifier(m_pboard, static_cast<CFIndex>(i), &item) != noErr) {
      continue;
    }
    PasteboardFlavorFlags flags;
    if (PasteboardGetItemFlavorFlags(m_pboard, item, kFileURLType, &flags) == noErr) {
      return true;
    }
  }

  return false;
}

std::string OSXClipboard::getFiles() const
{
  ItemCount count = 0;
  if (PasteboardGetItemCount(m_pboard, &count) != noErr) {
    return std::string();
  }

  std::vector<std::string> paths;
  for (ItemCount i = 1; i <= count; ++i) {
    PasteboardItemID item;
    if (PasteboardGetItemIdentifier(m_pboard, static_cast<CFIndex>(i), &item) != noErr) {
      continue;
    }

    CFDataRef flavorData = nullptr;
    if (PasteboardCopyItemFlavorData(m_pboard, item, kFileURLType, &flavorData) != noErr) {
      continue;
    }

    const std::string path = fileURLDataToPath(flavorData);
    CFRelease(flavorData);
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

void OSXClipboard::addFiles(const std::string &data)
{
  std::vector<deskflow::ClipboardFile> files;
  if (!deskflow::FileClipboardData::unmarshall(data, files) || files.empty()) {
    LOG_DEBUG("no files to add to clipboard");
    return;
  }

  // materialise the bundled files on disk; the pasteboard references them by URL
  const std::vector<std::string> paths = deskflow::FileClipboardData::writeToTempDir(files);
  if (paths.empty()) {
    LOG_WARN("failed to write pasted files to disk");
    return;
  }

  for (size_t i = 0; i < paths.size(); ++i) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(paths[i].data()), static_cast<CFIndex>(paths[i].size()),
        false
    );
    if (url == nullptr) {
      continue;
    }

    if (CFStringRef urlString = CFURLGetString(url); urlString != nullptr) {
      if (CFDataRef urlData =
              CFStringCreateExternalRepresentation(kCFAllocatorDefault, urlString, kCFStringEncodingUTF8, 0);
          urlData != nullptr) {
        // one pasteboard item per file, each carrying a file-url flavor
        auto itemID = reinterpret_cast<PasteboardItemID>(static_cast<intptr_t>(i + 1));
        PasteboardPutItemFlavor(m_pboard, itemID, kFileURLType, urlData, kPasteboardFlavorNoFlags);
        CFRelease(urlData);
      }
    }

    CFRelease(url);
  }

  LOG_DEBUG("added %zu file(s) to clipboard", paths.size());
}

void OSXClipboard::clearConverters()
{
  if (m_pboard == nullptr)
    return;

  for (ConverterList::iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    delete *index;
  }
  m_converters.clear();
}
