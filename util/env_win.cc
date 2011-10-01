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

#define DIR_SEP_CHAR L'\\'
#define DIR_SEP_STR L"\\"

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

char *ToUtf8(const WCHAR *s)
{
    int requiredBufSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *res = (char*)malloc(sizeof(char) * requiredBufSize);
    if (!res)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, res, requiredBufSize, NULL, NULL);
    return res;
}

static size_t WstrLen(const WCHAR *s)
{
    if (NULL == s)
        return 0;
    return wcslen(s);
}

static WCHAR *WstrJoin(const WCHAR *s1, const WCHAR *s2, const WCHAR *s3=NULL)
{
    size_t s1Len = WstrLen(s1);
    size_t s2Len = WstrLen(s2);
    size_t s3Len = WstrLen(s3);
    size_t len =s1Len + s2Len + s3Len + 1;
    WCHAR *res = (WCHAR*)malloc(sizeof(WCHAR) * len);
    if (!res)
        return NULL;
    WCHAR *tmp = res;
    if (s1 != NULL) {
        memcpy(tmp, s1, s1Len * sizeof(WCHAR));
        tmp += s1Len;
    }
    if (s2 != NULL) {
        memcpy(tmp, s2, s2Len * sizeof(WCHAR));
        tmp += s2Len;
    }
    if (s3 != NULL) {
        memcpy(tmp, s3, s3Len * sizeof(WCHAR));
        tmp += s3Len;
    }
    *tmp = 0;
    return res;
}

static bool WstrEndsWith(const WCHAR *s1, WCHAR c)
{
    size_t len = WstrLen(s1);
    return ((len > 0) && (s1[len-1] == c));
}

static WCHAR *PathJoin(const WCHAR *s1, const WCHAR *s2)
{
    if (WstrEndsWith(s1, DIR_SEP_CHAR))
        return WstrJoin(s1, s2);
    return WstrJoin(s1, DIR_SEP_STR, s2);
}

// Return true if s is "." or "..", which are 2 directories
// we should skip when enumerating a directory
static bool SkipDir(const WCHAR *s)
{
    if (*s == L'.') {
      if (s[1] == 0)
        return true;
      return ((s[1] == '.') && (s[2] == 0));
    }
    return false;
}

static BOOL WinLockFile(HANDLE file)
{
  return LockFile(file, 0, 0, 0, 1);
}

static BOOL WinUnlockFile(HANDLE file)
{
  return UnlockFile(file, 0, 0, 0, 1);
}

class WinFileLock : public FileLock {
 public:
  HANDLE file_;
};

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
    WCHAR *dirName = ToWcharPermissive(dir.c_str());
    if (dirName == NULL)
      return Status::InvalidArgument("Invalid file name");
    WCHAR *pattern = PathJoin(dirName, L"*");
    free(dirName);
    if (NULL == pattern)
      return Status::InvalidArgument("Invalid file name");
    WIN32_FIND_DATAW fileData;
    HANDLE h = FindFirstFileW(pattern, &fileData);
    free(pattern);
    if (INVALID_HANDLE_VALUE == h) {
        if (ERROR_FILE_NOT_FOUND == GetLastError())
          return Status::OK();
        return IOError(dir);
    }
    for (;;) {
        WCHAR *s = fileData.cFileName;
        if (!SkipDir(s)) {
            char *s2 = ToUtf8(s);
            result->push_back(s2);
            free(s2);
        }
        if (FALSE == FindNextFileW(h, &fileData))
            break;
    }
    FindClose(h);
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    WCHAR *filePath = ToWcharPermissive(fname.c_str());
    if (filePath == NULL)
        return Status::InvalidArgument("Invalid file name");

    BOOL ok = DeleteFileW(filePath);
    free(filePath);
    if (!ok) {
        DWORD err = GetLastError();
        if ((ERROR_PATH_NOT_FOUND == err) || (ERROR_FILE_NOT_FOUND == err))
          return Status::OK();
      return IOError(fname);
    }
    return Status::OK();
  }

  virtual Status CreateDir(const std::string& name) {
    // TODO: implement recursive creation?
    WCHAR *dir = ToWcharPermissive(name.c_str());
    if (dir == NULL)
      return Status::InvalidArgument("Invalid file name");
    BOOL ok = CreateDirectoryW(dir, NULL);
    free(dir);
    if (!ok) {
        if (ERROR_ALREADY_EXISTS == GetLastError())
          return Status::OK();
        return IOError(name);
    }
    return Status::OK();
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
    WCHAR *srcW = ToWcharPermissive(src.c_str());
    WCHAR *targetW = ToWcharPermissive(target.c_str());
    if ((srcW == NULL) || (targetW == NULL)) {
      free(srcW);
      free(targetW);
      return Status::InvalidArgument("Invalid file name");
    }
    BOOL ok = MoveFileExW(srcW, targetW, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        return IOError(src);
    return Status::OK();
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;
    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(fileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)fileName);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    if (!WinLockFile(h)) {
        CloseHandle(h);
        return IOError("lock " + fname);
    }
    WinFileLock* my_lock = new WinFileLock;
    my_lock->file_ = h;
    *lock = my_lock;
    return Status::OK();
  }

  virtual Status UnlockFile(FileLock* lock) {
    WinFileLock* my_lock = reinterpret_cast<WinFileLock*>(lock);
    BOOL ok = WinUnlockFile(my_lock->file_);
    CloseHandle(my_lock->file_);
    delete my_lock;
    if (!ok)
        return IOError("unlock");
    return Status::OK();
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
 // TODO: ensure only once
  if (NULL == default_env)
    InitDefaultEnv();
  return default_env;
}

}
