#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <deque>

// Undo Windows headers which #defines DeleteFile as DeleteFileA or DeleteFileW,
// which conflicts with Env::DeleteFile() method.
// Needs to be done before including leveldb/env.h
#ifdef DeleteFile
#undef DeleteFile
#endif

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "port/port_win.h"
#include "util/logging.h"
#include "util/win_logger.h"

// To properly support file names on Windows we should be using Unicode
// (WCHAR) strings. To accomodate existing interface which uses std::string,
// we use the following convention:
// * all filenames that we return (e.g. from GetTestDirectory()) are
//   utf8-encoded
// * we'll try to interpret all input file names as if they're
//   utf8-encoded. If they're not valid utf8 strings, we'll try
//   to interpret them according to a current code page
// This just works for names that don't use characters outside ascii
// and for those that do, the caller needs to be aware of this convention
// whenever it percolates up to the user-level API.

namespace leveldb {

static Status IOError(const std::string& context, DWORD err = (DWORD)-1) {
  char *err_msg = NULL;
  Status s;
  if ((DWORD)-1 == err)
    err = GetLastError();
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&err_msg, 0, NULL);
    if (!err_msg) 
        return Status::IOError(context);
    s = Status::IOError(context, err_msg);
    LocalFree(err_msg);
    return s;
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

class WinWritableFile : public WritableFile {
private:
  std::string name_;
  HANDLE file_;

public:
  WinWritableFile(std::string name, HANDLE h) : name_(name), file_(h) {
  }
  ~WinWritableFile() {
    Close();
  }

  virtual Status Append(const Slice& data) {
    DWORD nToWrite = data.size();
    DWORD nWritten = 0;
    BOOL ok = WriteFile(file_, data.data(), nToWrite, &nWritten, NULL);
    if (!ok)
        return IOError(name_ + "Append: cannot write");
    return Status::OK();
  }

  virtual Status Close() {
    if (INVALID_HANDLE_VALUE != file_) {
      Flush();
      CloseHandle(file_);
      file_ = INVALID_HANDLE_VALUE;
    }
    return Status::OK();
  }

  virtual Status Flush() {
    FlushFileBuffers(file_);
    return Status::OK();
  }

  virtual Status Sync() {
    return Flush();
  }
};

namespace {

#define DIR_SEP_CHAR L'\\'
#define DIR_SEP_STR L"\\"

WCHAR *ToWcharFromCodePage(const char *src, UINT cp) {
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
WCHAR *ToWcharPermissive(const char *s) {
  WCHAR *ws = ToWcharFromCodePage(s, CP_UTF8);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_ACP);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_OEMCP);
  return ws;
}

char *ToUtf8(const WCHAR *s) {
    int requiredBufSize = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *res = (char*)malloc(sizeof(char) * requiredBufSize);
    if (!res)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, res, requiredBufSize, NULL, NULL);
    return res;
}

static size_t WstrLen(const WCHAR *s) {
    if (NULL == s)
        return 0;
    return wcslen(s);
}

static WCHAR *WstrJoin(const WCHAR *s1, const WCHAR *s2, const WCHAR *s3=NULL) {
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

static bool WstrEndsWith(const WCHAR *s1, WCHAR c) {
    size_t len = WstrLen(s1);
    return ((len > 0) && (s1[len-1] == c));
}

static WCHAR *WstrPathJoin(const WCHAR *s1, const WCHAR *s2) {
    if (WstrEndsWith(s1, DIR_SEP_CHAR))
        return WstrJoin(s1, s2);
    return WstrJoin(s1, DIR_SEP_STR, s2);
}

// Return true if s is "." or "..", which are 2 directories
// we should skip when enumerating a directory
static bool SkipDir(const WCHAR *s) {
    if (*s == L'.') {
      if (s[1] == 0)
        return true;
      return ((s[1] == '.') && (s[2] == 0));
    }
    return false;
}

static BOOL WinLockFile(HANDLE file) {
  return LockFile(file, 0, 0, 0, 1);
}

static BOOL WinUnlockFile(HANDLE file) {
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
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)fileName);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinRandomAccessFile(fname, h);
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                 WritableFile** result) {
    *result = NULL;
    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL)
      return Status::InvalidArgument("Invalid file name");
    HANDLE h = CreateFileW(fileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)fileName);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinWritableFile(fname, h);
    return Status::OK();
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
    WCHAR *pattern = WstrPathJoin(dirName, L"*");
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
    WCHAR *dir = ToWcharPermissive(name.c_str());
    if (dir == NULL)
      return Status::InvalidArgument("Invalid file name");
    BOOL ok = RemoveDirectoryW(dir);
    free(dir);
    if (!ok)
        return IOError(name);
    return Status::OK();
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {

    WCHAR *fileName = ToWcharPermissive(fname.c_str());
    if (fileName == NULL)
      return Status::InvalidArgument("Invalid file name");
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,  
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,  NULL); 
    free(fileName);
    if (h == INVALID_HANDLE_VALUE)
      return  IOError(fname);

    // Not using GetFileAttributesEx() as it doesn't interact well with symlinks, etc.
    LARGE_INTEGER lsize;
    BOOL ok = GetFileSizeEx(h, &lsize);
    CloseHandle(h);
    if (!ok)
      return  IOError(fname);

    *size = static_cast<uint64_t>(lsize.QuadPart);
    return Status::OK();
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
    free(srcW);
    free(targetW);
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
    WCHAR buf[MAX_PATH];
    DWORD res = GetTempPathW(MAX_PATH, buf);
    if (0 == res) {
      return IOError("Can't get test directory");
    }
    char *s = ToUtf8(buf);
    if (!s) {
      return IOError("Can't get test directory");
    }
    *result = std::string(s);
    free(s);
    return Status::OK();
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    *result = NULL;
    FILE* f = fopen(fname.c_str(), "wt");
    if (f == NULL)
      return Status::IOError(fname, strerror(errno));
    *result = new WinLogger(f);
    return Status::OK();
  }

  virtual uint64_t NowMicros() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime; // could use memcpy here!
    uli.HighPart = ft.dwHighDateTime;
    uint64_t micros = uli.QuadPart / 10;
    return micros;
  }

  virtual void SleepForMicroseconds(int micros) {
    // TODO: is there a higher precision call?
    ::Sleep(micros / 1000);
  }

private:
  // BGThread() is the body of the background thread
  void BGThread();

  static DWORD WINAPI BGThreadWrapper(void* arg) {
    (reinterpret_cast<WinEnv*>(arg))->BGThread();
    return NULL;
  }

  leveldb::port::Mutex mu_;
  leveldb::port::CondVar bgsignal_;
  HANDLE bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;
};


WinEnv::WinEnv() : bgthread_(NULL), bgsignal_(&mu_) {
}

void WinEnv::Schedule(void (*function)(void*), void* arg) {
  mu_.Lock();

  // Start background thread if necessary
  if (NULL == bgthread_) {
    bgthread_ = CreateThread(NULL, 0, &WinEnv::BGThreadWrapper, this, 0, NULL);
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;

  mu_.Unlock();

  bgsignal_.Signal();
}

void WinEnv::BGThread() {
  while (true) {
    // Wait until there is an item that is ready to run
    mu_.Lock();

    while (queue_.empty()) {
      bgsignal_.Wait();
    }

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    mu_.Unlock();
    (*function)(arg);
  }
}

namespace {
struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
  HANDLE threadHandle;
};
}

static DWORD WINAPI StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  CloseHandle(state->threadHandle);
  delete state;
  return 0;
}

void WinEnv::StartThread(void (*function)(void* arg), void* arg) {
  StartThreadState* state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  state->threadHandle = CreateThread(NULL, 0, &StartThreadWrapper, state, 0, NULL);
}
}

static Env* default_env;
static void InitDefaultEnv() { default_env = new WinEnv(); }
static leveldb::port::Mutex default_env_mutex;

Env* Env::Default() {
  default_env_mutex.Lock();
  if (NULL == default_env)
    InitDefaultEnv();
  default_env_mutex.Unlock();
  return default_env;
}

}
