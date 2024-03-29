// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <ctype.h>
#include <stdio.h>
#include "db/filename.h"
#include "db/dbformat.h"
#include "leveldb/env.h"
#include "util/logging.h"

namespace leveldb {
#if defined(LEVELDB_PLATFORM_WINDOWS)
const std::string path_sep_str = "\\";
#else
const std::string path_sep_str = "/";
#endif

static bool StringEndsWith(const std::string s, const std::string end)
{
    size_t pos = s.find_last_of(end);
    size_t expected_pos = s.size() - 1;
    return pos == expected_pos;
}

std::string PathJoin(const std::string dir, const std::string rest)
{
  if (StringEndsWith(dir, path_sep_str))
      return dir + rest;
  return dir + path_sep_str + rest;
}

static std::string MakeFileName(const std::string& name, uint64_t number,
                                const char* suffix) {
  char buf[100];
  snprintf(buf, sizeof(buf), "%06llu.%s",
           static_cast<unsigned long long>(number),
           suffix);
  return PathJoin(name, buf);
}

std::string LogFileName(const std::string& name, uint64_t number) {
  assert(number > 0);
  return MakeFileName(name, number, "log");
}

std::string TableFileName(const std::string& name, uint64_t number) {
  assert(number > 0);
  return MakeFileName(name, number, "sst");
}

std::string DescriptorFileName(const std::string& dbname, uint64_t number) {
  assert(number > 0);
  char buf[100];
  snprintf(buf, sizeof(buf), "MANIFEST-%06llu",
           static_cast<unsigned long long>(number));
  return PathJoin(dbname, buf);
}

std::string CurrentFileName(const std::string& dbname) {
  return PathJoin(dbname, "CURRENT");
}

std::string LockFileName(const std::string& dbname) {
  return PathJoin(dbname, "LOCK");
}

std::string TempFileName(const std::string& dbname, uint64_t number) {
  assert(number > 0);
  return MakeFileName(dbname, number, "dbtmp");
}

std::string InfoLogFileName(const std::string& dbname) {
  return PathJoin(dbname, "LOG");
}

// Return the name of the old info log file for "dbname".
std::string OldInfoLogFileName(const std::string& dbname) {
  return PathJoin(dbname, "LOG.old");
}


// Owned filenames have the form:
//    dbname/CURRENT
//    dbname/LOCK
//    dbname/LOG
//    dbname/LOG.old
//    dbname/MANIFEST-[0-9]+
//    dbname/[0-9]+.(log|sst)
bool ParseFileName(const std::string& fname,
                   uint64_t* number,
                   FileType* type) {
  Slice rest(fname);
  if (rest == "CURRENT") {
    *number = 0;
    *type = kCurrentFile;
  } else if (rest == "LOCK") {
    *number = 0;
    *type = kDBLockFile;
  } else if (rest == "LOG" || rest == "LOG.old") {
    *number = 0;
    *type = kInfoLogFile;
  } else if (rest.starts_with("MANIFEST-")) {
    rest.remove_prefix(strlen("MANIFEST-"));
    uint64_t num;
    if (!ConsumeDecimalNumber(&rest, &num)) {
      return false;
    }
    if (!rest.empty()) {
      return false;
    }
    *type = kDescriptorFile;
    *number = num;
  } else {
    // Avoid strtoull() to keep filename format independent of the
    // current locale
    uint64_t num;
    if (!ConsumeDecimalNumber(&rest, &num)) {
      return false;
    }
    Slice suffix = rest;
    if (suffix == Slice(".log")) {
      *type = kLogFile;
    } else if (suffix == Slice(".sst")) {
      *type = kTableFile;
    } else if (suffix == Slice(".dbtmp")) {
      *type = kTempFile;
    } else {
      return false;
    }
    *number = num;
  }
  return true;
}

Status SetCurrentFile(Env* env, const std::string& dbname,
                      uint64_t descriptor_number) {
  // Remove leading "dbname/" and add newline to manifest file name
  std::string manifest = DescriptorFileName(dbname, descriptor_number);
  Slice contents = manifest;
  assert(contents.starts_with(dbname + path_sep_str));
  contents.remove_prefix(dbname.size() + 1);
  std::string tmp = TempFileName(dbname, descriptor_number);
  Status s = WriteStringToFile(env, contents.ToString() + "\n", tmp);
  if (s.ok()) {
    s = env->RenameFile(tmp, CurrentFileName(dbname));
  }
  if (!s.ok()) {
    env->DeleteFile(tmp);
  }
  return s;
}

}  // namespace leveldb
