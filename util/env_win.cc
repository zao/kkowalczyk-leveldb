#include <Windows.h>
#include <stdio.h>
#include <string.h>

// Undo Windows headers which #defines DeleteFile as DeleteFileA or DeleteFileW
#ifdef DeleteFile
#undef DeleteFile
#endif

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/win_logger.h"

// Implementation note: to properly support file names on Windows we should
// be using Unicode (WCHAR) strings. To accomodate existing interface which
// uses std::string, we use the following convention:
// * all filenames that we return (e.g. from GetTestDirectory()) are
//   utf8-encoded
// * we'll try to interpret all input file names as if they're
//   utf8-encoded. If they're not valid utf8 strings, we'll try
//   to interpret them according to a current code page
// This just works for names that don't use characters outside ascii
// and for those that do, the caller needs to be aware of this convention
// whenever it percolates up to the user-level API.

namespace leveldb {

static Status IOError(const std::string& context, DWORD winerr = (DWORD)-1) {
  if ((DWORD)-1 == winerr)
    winerr = GetLastError();
  // TODO: convert winerr to a string
  return Status::IOError(context);
}

class WinSequentialFile: public SequentialFile {
private:
  std::string filename_;
  HANDLE file_;

public:
  WinSequentialFile(const std::string& fname, HANDLE f)
      : filename_(fname), file_(f) { }
  virtual ~WinSequentialFile() { CloseHandle(file_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    DWORD nToRead = n;
    DWORD nDidRead = 0;
    BOOL ok = ReadFile(file_, (void*)scratch, nToRead, &nDidRead, NULL);
    *result = Slice(scratch, nDidRead);
    if (!ok) {
        // We leave status as ok if we hit the end of the file
        if (GetLastError() != ERROR_HANDLE_EOF) {
            return IOError(filename_);
        }
    }
    return Status::OK();
  }

  virtual Status Skip(uint64_t n) {
    LARGE_INTEGER pos;
    pos.QuadPart = n;
    DWORD res = SetFilePointerEx(file_, pos, NULL, FILE_CURRENT);
    if (res == 0)
        return IOError(filename_);
    return Status::OK();
  }
};

class WinRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  HANDLE file_;

 public:
  WinRandomAccessFile(const std::string& fname, HANDLE file)
      : filename_(fname), file_(file) { }
  virtual ~WinRandomAccessFile() { CloseHandle(file_); }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    DWORD res = SetFilePointerEx(file_, pos, NULL, FILE_BEGIN);
    if (res == 0) {
        *result = Slice(scratch, 0);
        return IOError(filename_);
    }

    DWORD nToRead = n;
    DWORD nDidRead = 0;
    BOOL ok = ReadFile(file_, (void*)scratch, nToRead, &nDidRead, NULL);
    *result = Slice(scratch, nDidRead);
    if (!ok) {
        // We leave status as ok if we hit the end of the file
        if (GetLastError() != ERROR_HANDLE_EOF) {
            return IOError(filename_);
        }
    }
    return Status::OK();
  }
};

namespace {

WCHAR *ToWcharFromCodePage(const char *src, UINT cp)
{
  int requiredBufSize = MultiByteToWideChar(cp, 0, src, -1, NULL, 0);
  if (0 == requiredBufSize) // indicates an error
    return NULL;
  WCHAR *res = reinterpret_cast<WCHAR*>(malloc(sizeof(WCHAR) * requiredBufSize));
  if (!res)
    return NULL;
  MultiByteToWideChar(cp, 0, src, -1, res, requiredBufSize);
  return res;
}

// try to convert to WCHAR string trying most common code pages
// to be as permissive as we can be
WCHAR *ToWcharPermissive(const char *s)
{
  WCHAR *ws = ToWcharFromCodePage(s, CP_UTF8);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_ACP);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_OEMCP);
  return ws;
}

class WinEnv : public Env {
 public:
  WinEnv();
  virtual ~WinEnv() {
    fprintf(stderr, "Destroying Env::Default()\n");
    //exit(1);
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    *result = NULL;
    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)fileName);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinSequentialFile(fname, h);
    return Status::OK();
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                   RandomAccessFile** result) {
    *result = NULL;
    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)fileName);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinRandomAccessFile(fname, h);
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                 WritableFile** result) {
    /*Status s;
    try {
      // will create a new empty file to write to
      *result = new BoostFile(fname);
    }
    catch (const std::exception & e) {
      s = Status::IOError(fname, e.what());
    }

    return s; */
    return IOError(fname, 1);
  }

  virtual bool FileExists(const std::string& fname) {
    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL)
        return false;

    WIN32_FILE_ATTRIBUTE_DATA   fileInfo;
    BOOL res = GetFileAttributesExW(fileName, GetFileExInfoStandard, &fileInfo);
    free((void*)fileName);
    if (0 == res)
        return false;

    if ((fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;
    return true;
  }

  virtual Status GetChildren(const std::string& dir,
               std::vector<std::string>* result) {
    result->clear();
    /*
    boost::system::error_code ec;
    boost::filesystem::directory_iterator current(dir, ec);
    if (ec) {
      return Status::IOError(dir, ec.message());
    }

    boost::filesystem::directory_iterator end;

    for(; current != end; ++current) {
      result->push_back(current->path().filename().generic_string());
    }

    return Status::OK(); */
    return IOError("", 1);
  }

  virtual Status DeleteFile(const std::string& fname) {
    // TODO: implement me
    return IOError(fname, 1);
  }

  virtual Status CreateDir(const std::string& name) {
    // TODO: implement me
    return IOError(name, 1);
  }

  virtual Status DeleteDir(const std::string& name) {
    // TODO: implement me
    return IOError(name, 1);
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
    // TODO: implement me
    return IOError(fname, 1);
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    return IOError(src, 1);
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    // TODO: implement me
    return IOError(fname, 1);
  }

  virtual Status UnlockFile(FileLock* lock) {
    // TODO: implement me
    return IOError("", 1);
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual Status GetTestDirectory(std::string* result) {
    // TODO: implement me
    return IOError("", 1);
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    // TODO: implement me
    return IOError(fname, 1);
  }

  virtual uint64_t NowMicros() {
    // TODO: implement me
    return 0;
  }

  virtual void SleepForMicroseconds(int micros) {
    // TODO: implement me
  }

private:
  size_t page_size_;
  bool started_bgthread_;
};

// TODO: query Windows for it
size_t getpagesize()
{
    return 4096;
}

WinEnv::WinEnv() : page_size_(getpagesize()),
                       started_bgthread_(false) {
}

void WinEnv::Schedule(void (*function)(void*), void* arg) {
  // TODO: implement me
}

void WinEnv::StartThread(void (*function)(void* arg), void* arg) {
  // TODO: implement me
}

}

static Env* default_env;
static void InitDefaultEnv() { default_env = new WinEnv(); }

Env* Env::Default() {
  InitDefaultEnv(); // TODO: ensure only once
  return default_env;
}

}
