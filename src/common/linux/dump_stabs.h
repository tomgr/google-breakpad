// -*- mode: C++ -*-

// Copyright (c) 2009, Google Inc.
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

// dump_stabs.h: A StabsHandler that populates a Module.

#ifndef COMMON_LINUX_DUMP_STABS_H__
#define COMMON_LINUX_DUMP_STABS_H__

#include <stdint.h>

#include <string>
#include <vector>

#include "common/linux/module.h"
#include "common/linux/stabs_reader.h"

namespace google_breakpad {

using std::string;
using std::vector;

// A DumpStabsHandler is a handler that receives parsed STABS
// debugging information from a StabsReader, and uses that to populate
// a Module. (All classes are in the google_breakpad namespace.) A
// Module represents the contents of a Breakpad symbol file, and knows
// how to write itself out as such. A DumpStabsHandler thus acts as
// the bridge between STABS and Breakpad data.
class DumpStabsHandler: public google_breakpad::StabsHandler {
 public:
  // Receive parsed debugging information from a StabsReader, and
  // store it all in MODULE.
  DumpStabsHandler(Module *module) :
      module_(module),
      comp_unit_base_address_(0),
      current_function_(NULL),
      current_source_file_(NULL),
      current_source_file_name_(NULL) { }

  // The standard StabsHandler virtual member functions.
  bool StartCompilationUnit(const char *name, uint64_t address,
                            const char *build_directory);
  bool EndCompilationUnit(uint64_t address);
  bool StartFunction(const string &name, uint64_t address);
  bool EndFunction(uint64_t address);
  bool Line(uint64_t address, const char *name, int number);
  void Warning(const char *format, ...);

  // Do any final processing necessary to make module_ contain all the
  // data provided by the STABS reader.
  //
  // Because STABS does not provide reliable size information for
  // functions and lines, we need to make a pass over the data after
  // processing all the STABS to compute those sizes.  We take care of
  // that here.
  void Finalize();

 private:

  // An arbitrary, but very large, size to use for functions whose
  // size we can't compute properly.
  static const uint64_t kFallbackSize = 0x10000000;

  // The module we're contributing debugging info to.
  Module *module_;

  // The functions we've generated so far.  We don't add these to
  // module_ as we parse them.  Instead, we wait until we've computed
  // their ending address, and their lines' ending addresses.
  //
  // We could just stick them in module_ from the outset, but if
  // module_ already contains data gathered from other debugging
  // formats, that would complicate the size computation.
  vector<Module::Function *> functions_;

  // Boundary addresses.  STABS doesn't necessarily supply sizes for
  // functions and lines, so we need to compute them ourselves by
  // finding the next object.
  vector<Module::Address> boundaries_;

  // The base address of the current compilation unit.  We use this to
  // recognize functions we should omit from the symbol file.  (If you
  // know the details of why we omit these, please patch this
  // comment.)
  Module::Address comp_unit_base_address_;

  // The function we're currently contributing lines to.
  Module::Function *current_function_;

  // The last Module::File we got a line number in.
  Module::File *current_source_file_;

  // The pointer in the .stabstr section of the name that
  // current_source_file_ is built from.  This allows us to quickly
  // recognize when the current line is in the same file as the
  // previous one (which it usually is).
  const char *current_source_file_name_;
};

} // namespace google_breakpad

#endif // COMMON_LINUX_DUMP_STABS_H__
