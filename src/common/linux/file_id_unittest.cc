// Copyright (c) 2010, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Unit tests for FileID

#include <elf.h>
#include <stdlib.h>

#include "common/linux/file_id.h"
#include "common/linux/synth_elf.h"
#include "common/test_assembler.h"
#include "breakpad_googletest_includes.h"

using namespace google_breakpad;
using google_breakpad::synth_elf::BuildIDNote;
using google_breakpad::synth_elf::ELF;
using google_breakpad::test_assembler::kLittleEndian;
using google_breakpad::test_assembler::Section;

TEST(FileIDStripTest, StripSelf) {
  // Calculate the File ID of this binary using
  // FileID::ElfFileIdentifier, then make a copy of this binary,
  // strip it, and ensure that the result is the same.
  char exe_name[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", exe_name, PATH_MAX - 1);
  ASSERT_NE(len, -1);
  exe_name[len] = '\0';

  // copy our binary to a temp file, and strip it
  char templ[] = "/tmp/file-id-unittest-XXXXXX";
  mktemp(templ);
  char cmdline[4096];
  sprintf(cmdline, "cp \"%s\" \"%s\"", exe_name, templ);
  ASSERT_EQ(system(cmdline), 0);
  sprintf(cmdline, "strip \"%s\"", templ);
  ASSERT_EQ(system(cmdline), 0);

  uint8_t identifier1[sizeof(MDGUID)];
  uint8_t identifier2[sizeof(MDGUID)];
  FileID fileid1(exe_name);
  EXPECT_TRUE(fileid1.ElfFileIdentifier(identifier1));
  FileID fileid2(templ);
  EXPECT_TRUE(fileid2.ElfFileIdentifier(identifier2));
  char identifier_string1[37];
  char identifier_string2[37];
  FileID::ConvertIdentifierToString(identifier1, identifier_string1,
                                    37);
  FileID::ConvertIdentifierToString(identifier2, identifier_string2,
                                    37);
  EXPECT_STREQ(identifier_string1, identifier_string2);
  unlink(templ);
}

class FileIDTest : public testing::Test {
public:
  void GetElfContents(ELF& elf) {
    string contents;
    ASSERT_TRUE(elf.GetContents(&contents));
    ASSERT_LT(0, contents.size());

    elfdata_v.clear();
    elfdata_v.insert(elfdata_v.begin(), contents.begin(), contents.end());
    elfdata = &elfdata_v[0];
  }

  vector<uint8_t> elfdata_v;
  uint8_t* elfdata;
};

TEST_F(FileIDTest, ElfClass) {
  uint8_t identifier[sizeof(MDGUID)];
  const char expected_identifier_string[] =
      "80808080-8080-0000-0000-008080808080";
  char identifier_string[sizeof(expected_identifier_string)];
  const size_t kTextSectionSize = 128;

  ELF elf32(EM_386, ELFCLASS32, kLittleEndian);
  Section text32(kLittleEndian);
  for (size_t i = 0; i < kTextSectionSize; ++i) {
    text32.D8(i * 3);
  }
  elf32.AddSection(".text", text32, SHT_PROGBITS);
  elf32.Finish();
  GetElfContents(elf32);

  EXPECT_TRUE(FileID::ElfFileIdentifierFromMappedFile(elfdata, identifier));

  FileID::ConvertIdentifierToString(identifier, identifier_string,
                                    sizeof(identifier_string));
  EXPECT_STREQ(expected_identifier_string, identifier_string);

  memset(identifier, 0, sizeof(identifier));
  memset(identifier_string, 0, sizeof(identifier_string));

  ELF elf64(EM_X86_64, ELFCLASS64, kLittleEndian);
  Section text64(kLittleEndian);
  for (size_t i = 0; i < kTextSectionSize; ++i) {
    text64.D8(i * 3);
  }
  elf64.AddSection(".text", text64, SHT_PROGBITS);
  elf64.Finish();
  GetElfContents(elf64);

  EXPECT_TRUE(FileID::ElfFileIdentifierFromMappedFile(elfdata, identifier));

  FileID::ConvertIdentifierToString(identifier, identifier_string,
                                    sizeof(identifier_string));
  EXPECT_STREQ(expected_identifier_string, identifier_string);
}

TEST_F(FileIDTest, BuildID) {
  const uint8_t kExpectedIdentifier[sizeof(MDGUID)] =
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  char expected_identifier_string[] =
    "00000000-0000-0000-0000-000000000000";
  FileID::ConvertIdentifierToString(kExpectedIdentifier,
                                    expected_identifier_string,
                                    sizeof(expected_identifier_string));

  uint8_t identifier[sizeof(MDGUID)];
  char identifier_string[sizeof(expected_identifier_string)];

  ELF elf32(EM_386, ELFCLASS32, kLittleEndian);
  Section text(kLittleEndian);
  text.Append(4096, 0);
  elf32.AddSection(".text", text, SHT_PROGBITS);
  BuildIDNote::AppendSection(elf32,
                             kExpectedIdentifier,
                             sizeof(kExpectedIdentifier));
  elf32.Finish();
  GetElfContents(elf32);

  EXPECT_TRUE(FileID::ElfFileIdentifierFromMappedFile(elfdata, identifier));

  FileID::ConvertIdentifierToString(identifier, identifier_string,
                                    sizeof(identifier_string));
  EXPECT_STREQ(expected_identifier_string, identifier_string);

  memset(identifier, 0, sizeof(identifier));
  memset(identifier_string, 0, sizeof(identifier_string));

  ELF elf64(EM_X86_64, ELFCLASS64, kLittleEndian);
  // Re-use empty text section from previous test
  elf64.AddSection(".text", text, SHT_PROGBITS);
  BuildIDNote::AppendSection(elf64,
                             kExpectedIdentifier,
                             sizeof(kExpectedIdentifier));
  elf64.Finish();
  GetElfContents(elf64);

  EXPECT_TRUE(FileID::ElfFileIdentifierFromMappedFile(elfdata, identifier));

  FileID::ConvertIdentifierToString(identifier, identifier_string,
                                    sizeof(identifier_string));
  EXPECT_STREQ(expected_identifier_string, identifier_string);
}
