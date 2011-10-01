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

namespace leveldb {

// TODO: use native windows error codes and error messages
static Status IOError(const std::string& context, int err_number) {
  return Status::IOError(context, strerror(err_number));
}

class WinSequentialFile: public SequentialFile {
private:
  std::string filename_;
  HANDLE file_;

public:
    // TODO: remember file name
  WinSequentialFile(const std::string& fname, HANDLE f)
      : filename_(fname), file_(f) { }
  virtual ~WinSequentialFile() { CloseHandle(file_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    Status s;
    s = IOError(filename_, 1);
    /*
    size_t r = fread_unlocked(scratch, 1, n, file_);
    *result = Slice(scratch, r);
    if (r < n) {
      if (feof(file_)) {
        // We leave status as ok if we hit the end of the file
      } else {
        // A partial read with an error: return a non-ok status
        s = IOError(filename_, errno);
      }
    }*/
    return s;
  }

  virtual Status Skip(uint64_t n) {
    /*
    if (fseek(file_, n, SEEK_CUR)) {
      return IOError(filename_, errno);
    }
    return Status::OK(); */
    return IOError(filename_, 1);
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
    Status s;
    s = IOError(filename_, 1);
    /*
    ssize_t r = pread(fd_, scratch, n, static_cast<off_t>(offset));
    *result = Slice(scratch, (r < 0) ? 0 : r);
    if (r < 0) {
      // An error: return a non-ok status
      s = IOError(filename_, errno);
    }*/
    return s;
  }
};

namespace {

class WinEnv : public Env {
 public:
  WinEnv();
  virtual ~WinEnv() {
    fprintf(stderr, "Destroying Env::Default()\n");
    exit(1);
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    /*
    FILE* f = fopen(fname.c_str(), "r");
    if (f == NULL) {
      *result = NULL;
      return IOError(fname, errno);
    } else {
      *result = new WinSequentialFile(fname, f);
      return Status::OK();
    }
    */
    return IOError(fname, 1);
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                   RandomAccessFile** result) {
    /*
#ifdef WIN32
    int fd = _open(fname.c_str(), _O_RDONLY | _O_RANDOM | _O_BINARY);
#else
    int fd = open(fname.c_str(), O_RDONLY);
#endif
    if (fd < 0) {
      *result = NULL;
      return Status::IOError(fname, strerror(errno));
    }
    *result = new PosixRandomAccessFile(fname, fd);
    return Status::OK(); */
    return IOError(fname, 1);
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
    return false;
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
