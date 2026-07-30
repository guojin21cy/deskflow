/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileClipboardDataTests.h"

#include "deskflow/FileClipboardData.h"

#include <QTemporaryFile>

using deskflow::ClipboardFile;
using deskflow::FileClipboardData;

void FileClipboardDataTests::roundTripSingleFile()
{
  std::vector<ClipboardFile> files{{"hello.txt", "hello world"}};

  std::vector<ClipboardFile> out;
  QVERIFY(FileClipboardData::unmarshall(FileClipboardData::marshall(files), out));

  QCOMPARE(out.size(), static_cast<size_t>(1));
  QCOMPARE(QString::fromStdString(out[0].name), QString("hello.txt"));
  QCOMPARE(QString::fromStdString(out[0].data), QString("hello world"));
}

void FileClipboardDataTests::roundTripMultipleFiles()
{
  std::vector<ClipboardFile> files{
      {"a.txt", "aaa"},
      {"b.bin", ""},
      {"c.dat", "some longer content here"},
  };

  std::vector<ClipboardFile> out;
  QVERIFY(FileClipboardData::unmarshall(FileClipboardData::marshall(files), out));

  QCOMPARE(out.size(), static_cast<size_t>(3));
  QCOMPARE(QString::fromStdString(out[1].name), QString("b.bin"));
  QVERIFY(out[1].data.empty());
  QCOMPARE(QString::fromStdString(out[2].data), QString("some longer content here"));
}

void FileClipboardDataTests::roundTripEmptyList()
{
  std::vector<ClipboardFile> out{{"stale", "stale"}};
  QVERIFY(FileClipboardData::unmarshall(FileClipboardData::marshall({}), out));
  QVERIFY(out.empty());
}

void FileClipboardDataTests::roundTripBinaryContent()
{
  std::string binary;
  for (int i = 0; i < 256; ++i) {
    binary += static_cast<char>(i);
  }
  std::vector<ClipboardFile> files{{"blob", binary}};

  std::vector<ClipboardFile> out;
  QVERIFY(FileClipboardData::unmarshall(FileClipboardData::marshall(files), out));
  QCOMPARE(out.size(), static_cast<size_t>(1));
  QCOMPARE(out[0].data, binary);
}

void FileClipboardDataTests::unmarshallRejectsTruncatedHeader()
{
  std::vector<ClipboardFile> out;
  QVERIFY(!FileClipboardData::unmarshall(std::string("\x00\x00", 2), out));
}

void FileClipboardDataTests::unmarshallRejectsTruncatedName()
{
  // count=1, nameLen=10 but no name bytes follow
  std::string buf;
  buf += std::string("\x00\x00\x00\x01", 4);
  buf += std::string("\x00\x00\x00\x0a", 4);
  std::vector<ClipboardFile> out;
  QVERIFY(!FileClipboardData::unmarshall(buf, out));
}

void FileClipboardDataTests::unmarshallRejectsTruncatedData()
{
  // count=1, name="x", dataLen=5 but no data bytes follow
  std::string buf;
  buf += std::string("\x00\x00\x00\x01", 4);
  buf += std::string("\x00\x00\x00\x01", 4);
  buf += "x";
  buf += std::string("\x00\x00\x00\x00\x00\x00\x00\x05", 8);
  std::vector<ClipboardFile> out;
  QVERIFY(!FileClipboardData::unmarshall(buf, out));
}

void FileClipboardDataTests::unmarshallRejectsOversizedLength()
{
  // count=1, nameLen=0xffffffff which exceeds the buffer
  std::string buf;
  buf += std::string("\x00\x00\x00\x01", 4);
  buf += std::string("\xff\xff\xff\xff", 4);
  std::vector<ClipboardFile> out;
  QVERIFY(!FileClipboardData::unmarshall(buf, out));
}

void FileClipboardDataTests::sanitizeNameStripsDirectories()
{
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("/home/user/report.pdf")), QString("report.pdf"));
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("C:\\Users\\me\\report.pdf")), QString("report.pdf"));
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("plain.txt")), QString("plain.txt"));
}

void FileClipboardDataTests::sanitizeNameRejectsTraversal()
{
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("..")), QString("file"));
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("../../etc/passwd")), QString("passwd"));
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("")), QString("file"));
  QCOMPARE(QString::fromStdString(FileClipboardData::sanitizeName("foo/")), QString("file"));
}

void FileClipboardDataTests::writeThenReadRoundTrip()
{
  std::vector<ClipboardFile> files{{"note.txt", "line one\nline two"}, {"data.bin", std::string("\x00\x01\x02", 3)}};

  const auto paths = FileClipboardData::writeToTempDir(files);
  QCOMPARE(paths.size(), static_cast<size_t>(2));

  std::vector<ClipboardFile> out;
  QVERIFY(FileClipboardData::readFiles(paths, out));
  QCOMPARE(out.size(), static_cast<size_t>(2));
  QCOMPARE(QString::fromStdString(out[0].name), QString("note.txt"));
  QCOMPARE(out[0].data, files[0].data);
  QCOMPARE(out[1].data, files[1].data);
}

void FileClipboardDataTests::readFilesSkipsMissing()
{
  std::vector<ClipboardFile> out;
  QVERIFY(!FileClipboardData::readFiles({"/deskflow/does/not/exist/a", "/deskflow/does/not/exist/b"}, out));
  QVERIFY(out.empty());
}

void FileClipboardDataTests::readFilesRejectsSelectionOverLimit()
{
  QTemporaryFile file;
  QVERIFY(file.open());
  QCOMPARE(file.write("data"), 4);
  file.close();

  std::vector<ClipboardFile> out{{"stale", "stale"}};
  QVERIFY(!FileClipboardData::readFiles({file.fileName().toStdString()}, out, 3));
  QVERIFY(out.empty());
}

QTEST_MAIN(FileClipboardDataTests)
