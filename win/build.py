#!/usr/bin/python
"""
Build a release of LevelDB binaries.
Command line arguments:
 -test   : run all tests
 -upload : upload the zip 
"""

import os
import os.path
import shutil
import sys
import time
import re

from util import log, run_cmd_throw, test_for_flag, s3UploadFilePublic, import_boto
from util import s3UploadDataPublic, ensure_s3_doesnt_exist, ensure_path_exists
from util import zip_file

# This is version that LevelDB reports. It's in
# http://code.google.com/p/leveldb/source/browse/include/leveldb/db.h
# under kMajorVersion, kMinorVersion
gVersion = "1.2"
# Incrementally increasing revision that identifies my revisions.
# They might happen because of merging new code from LevelDB (they don't always
# update kMinorVersion after changing code) or making changes to my port. 
gRevision = 1

gReleaseNotes = [
  ["1.2 rev 1",
  "first release",
  "based on http://code.google.com/p/leveldb/source/detail?r=299ccedfeca1fb3497978c288e76008a5c08e899"]
]

args = sys.argv[1:]
test   = test_for_flag(args, "-test")
upload = test_for_flag(args, "-upload")
upload = upload or test_for_flag(args, "upload")

def usage():
  print("build.py [-test][-upload]")
  sys.exit(1)

s3_dir = "software/leveldb/rel"

def s3_zip_name():
  return "%s/LevelDB-%s-rev-%d.zip" % (s3_dir, gVersion, gRevision)

dll_file = "libleveldb.dll"
dbbench_exe = "db_bench.exe"
test_exes = ["corruption_test.exe", "arena_test.exe", "coding_test.exe", "env_test.exe", "memenv_test.exe", "version_edit_test.exe", "c_test.exe", "skiplist_test.exe", "version_set_test.exe", "cache_test.exe", "crc32c_test.exe", "dbformat_test.exe", "log_test.exe", "write_batch_test.exe", "table_test.exe"]
#, "filename_test.exe", "db_test.exe"

build_files = test_exes + [dll_file] + [dbbench_exe]

def verify_build_ok(build_dir):
  for f in build_files:
    p = os.path.join(build_dir, f)
    ensure_path_exists(p)
    pdb = os.path.splitext(p)[0] + ".pdb"
    ensure_path_exists(pdb)

def run_tests(build_dir):
  total = len(test_exes)
  curr = 1
  for f in test_exes:
    p = os.path.join(build_dir, f)
    print("Running test %d/%d %s" % (curr, total, p))
    out, err = run_cmd_throw(p)
    print(out + err)
    curr += 1

  p = os.path.join(build_dir, dbbench_exe)
  print("Running %s" % p)
  run_cmd_throw(p)

def main():
  if len(args) != 0:
    usage()
  if upload:
    import_boto()
    ensure_s3_doesnt_exist(s3_zip_name())
  mydir = os.path.dirname(os.path.realpath(__file__))
  os.chdir(mydir)

  build_dir = "rel"
  #shutil.rmtree(build_dir, ignore_errors=True)
  run_cmd_throw("cmd.exe", "/c", "build.bat", "Just32rel")
  verify_build_ok(build_dir)
  if test: run_tests(build_dir)

  build_dir = "rel64bit"
  #shutil.rmtree(build_dir, ignore_errors=True)
  #run_cmd_throw("start", "build.bat", "Just64rel")
  #if test: run_tests(build_dir)

if __name__ == "__main__":
  main()