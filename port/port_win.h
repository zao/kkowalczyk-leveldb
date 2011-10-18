// LevelDB Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the University of California, Berkeley nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef STORAGE_LEVELDB_PORT_PORT_WIN_H_
#define STORAGE_LEVELDB_PORT_PORT_WIN_H_

#define snprintf _snprintf

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef small
#undef small
#endif
// Undo #define DeleteFile as DeleteFileA or DeleteFileW 
// done in Windows headers, which conflicts with
// Env::DeleteFile() method.
#ifdef DeleteFile
#undef DeleteFile
#endif

#include <string>

#include <stdint.h>

namespace leveldb {
namespace port {

// Windows is little endian (for now :p)
static const bool kLittleEndian = true;

// 1 is implementation from bgrainger
// 2 is implementation from main repo with my fix
// 3 is based on condition_variable_win.cc from chrome
// (http://src.chromium.org/viewvc/chrome/trunk/src/base/synchronization/)
#define COND_VAR_IMPL 3

#if COND_VAR_IMPL == 1
// A Mutex represents an exclusive lock.
class Mutex {
 public:
  Mutex();
  ~Mutex();

  // Lock the mutex.  Waits until other lockers have exited.
  // Will deadlock if the mutex is already locked by this thread.
  void Lock();

  // Unlock the mutex.
  // REQUIRES: This mutex was locked by this thread.
  void Unlock();

  // Optionally crash if this thread does not hold this mutex.
  // The implementation must be fast, especially if NDEBUG is
  // defined.  The implementation is allowed to skip all checks.
  void AssertHeld();

 private:
  friend class CondVar;

  HANDLE mutex_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

class CondVar {
 public:
  explicit CondVar(Mutex* mu);
  ~CondVar();

  // Atomically release *mu and block on this condition variable until
  // either a call to SignalAll(), or a call to Signal() that picks
  // this thread to wakeup.
  // REQUIRES: this thread holds *mu
  void Wait();

  // If there are some threads waiting, wake up at least one of them.
  void Signal();

  // Wake up all waiting threads.
  void SignalAll();

private:
  Mutex * mu_;

  // Windows XP doesn't have condition variables, so we will implement our own condition variable with a semaphore
  // implementation as described in a paper written by Douglas C. Schmidt and Irfan Pyarali.
  Mutex wait_mtx_;
  long waiting_;
  
  HANDLE sema_;
  HANDLE event_;

  bool broadcasted_;
};
#endif

#if COND_VAR_IMPL == 2
class CondVar;

class Mutex {
 public:
  Mutex() {
    ::InitializeCriticalSection(&cs_);
  }

  ~Mutex(){
    ::DeleteCriticalSection(&cs_);
  }

  void Lock() {
    ::EnterCriticalSection(&cs_);
  }

  void Unlock() {
     ::LeaveCriticalSection(&cs_);
  }

  void AssertHeld() {
    
  }

 private:
  friend class CondVar;
  // critical sections are more efficient than mutexes
  // but they are not recursive and can only be used to synchronize threads within the same process
  // we use opaque void * to avoid including windows.h in port_win.h
  CRITICAL_SECTION cs_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

// the Win32 API offers a dependable condition variable mechanism, but only starting with
// Windows 2008 and Vista
// no matter what we will implement our own condition variable with a semaphore
// implementation as described in a paper written by Andrew D. Birrell in 2003
class CondVar {
 public:
  explicit CondVar(Mutex* mu);
  ~CondVar();
  void Wait();
  void Signal();
  void SignalAll();
 private:
  Mutex* mu_;
  
  Mutex wait_mtx_;
  long waiting_;
  
  void * sem1_;
  void * sem2_;  
};
#endif

#if COND_VAR_IMPL == 3

typedef int64_t int64;

// based on lock_impl_win.cc from chrome
// ()
class Mutex {
 public:
  Mutex() {
      // The second parameter is the spin count, for short-held locks it avoid the
      // contending thread from going to sleep which helps performance greatly.
      ::InitializeCriticalSectionAndSpinCount(&os_lock_, 2000);
  }

  ~Mutex(){
    ::DeleteCriticalSection(&os_lock_);
  }

  void Lock() {
    ::EnterCriticalSection(&os_lock_);
  }

  void Unlock() {
     ::LeaveCriticalSection(&os_lock_);
  }

  void AssertHeld() {
    
  }
  void AssertAcquired() {
  }

private:
  friend class CondVar;
  // critical sections are more efficient than mutexes
  // but they are not recursive and can only be used to synchronize threads within the same process
  // we use opaque void * to avoid including windows.h in port_win.h
  CRITICAL_SECTION os_lock_;

  // No copying
  Mutex(const Mutex&);
  void operator=(const Mutex&);
};

// A helper class that acquires the given Lock while the AutoLock is in scope.
class AutoLock {
 public:
  explicit AutoLock(Mutex& lock) : lock_(lock) {
    lock_.Lock();
  }

  ~AutoLock() {
    lock_.AssertAcquired();
    lock_.Unlock();
  }

 private:
  Mutex& lock_;
  //DISALLOW_COPY_AND_ASSIGN(AutoLock);
};

// AutoUnlock is a helper that will Release() the |lock| argument in the
// constructor, and re-Acquire() it in the destructor.
class AutoUnlock {
 public:
  explicit AutoUnlock(Mutex& lock) : lock_(lock) {
    // We require our caller to have the lock.
    lock_.AssertAcquired();
    lock_.Unlock();
  }

  ~AutoUnlock() {
    lock_.Lock();
  }

 private:
  Mutex& lock_;
  //DISALLOW_COPY_AND_ASSIGN(AutoUnlock);
};

class Time {
 public:
  static const int64 kMillisecondsPerSecond = 1000;
  static const int64 kMicrosecondsPerMillisecond = 1000;
  static const int64 kMicrosecondsPerSecond = kMicrosecondsPerMillisecond *
                                              kMillisecondsPerSecond;
  static const int64 kMicrosecondsPerMinute = kMicrosecondsPerSecond * 60;
  static const int64 kMicrosecondsPerHour = kMicrosecondsPerMinute * 60;
  static const int64 kMicrosecondsPerDay = kMicrosecondsPerHour * 24;
  static const int64 kMicrosecondsPerWeek = kMicrosecondsPerDay * 7;
  static const int64 kNanosecondsPerMicrosecond = 1000;
  static const int64 kNanosecondsPerSecond = kNanosecondsPerMicrosecond *
                                             kMicrosecondsPerSecond;
};

class TimeDelta {
 public:
  TimeDelta() : delta_(0) {
  }

  // Converts units of time to TimeDeltas.
  static TimeDelta FromDays(int64 days);
  static TimeDelta FromHours(int64 hours);
  static TimeDelta FromMinutes(int64 minutes);
  static TimeDelta FromSeconds(int64 secs);
  static TimeDelta FromMilliseconds(int64 ms);
  static TimeDelta FromMicroseconds(int64 us);

  // Converts an integer value representing TimeDelta to a class. This is used
  // when deserializing a |TimeDelta| structure, using a value known to be
  // compatible. It is not provided as a constructor because the integer type
  // may be unclear from the perspective of a caller.
  static TimeDelta FromInternalValue(int64 delta) {
    return TimeDelta(delta);
  }

  // Returns the internal numeric value of the TimeDelta object. Please don't
  // use this and do arithmetic on it, as it is more error prone than using the
  // provided operators.
  // For serializing, use FromInternalValue to reconstitute.
  int64 ToInternalValue() const {
    return delta_;
  }

  // Returns the time delta in some unit. The F versions return a floating
  // point value, the "regular" versions return a rounded-down value.
  //
  // InMillisecondsRoundedUp() instead returns an integer that is rounded up
  // to the next full millisecond.
  int InDays() const;
  int InHours() const;
  int InMinutes() const;
  double InSecondsF() const;
  int64 InSeconds() const;
  double InMillisecondsF() const;
  int64 InMilliseconds() const;
  int64 InMillisecondsRoundedUp() const;
  int64 InMicroseconds() const;

  TimeDelta& operator=(TimeDelta other) {
    delta_ = other.delta_;
    return *this;
  }

  // Computations with other deltas.
  TimeDelta operator+(TimeDelta other) const {
    return TimeDelta(delta_ + other.delta_);
  }
  TimeDelta operator-(TimeDelta other) const {
    return TimeDelta(delta_ - other.delta_);
  }

  TimeDelta& operator+=(TimeDelta other) {
    delta_ += other.delta_;
    return *this;
  }
  TimeDelta& operator-=(TimeDelta other) {
    delta_ -= other.delta_;
    return *this;
  }
  TimeDelta operator-() const {
    return TimeDelta(-delta_);
  }

  // Computations with ints, note that we only allow multiplicative operations
  // with ints, and additive operations with other deltas.
  TimeDelta operator*(int64 a) const {
    return TimeDelta(delta_ * a);
  }
  TimeDelta operator/(int64 a) const {
    return TimeDelta(delta_ / a);
  }
  TimeDelta& operator*=(int64 a) {
    delta_ *= a;
    return *this;
  }
  TimeDelta& operator/=(int64 a) {
    delta_ /= a;
    return *this;
  }
  int64 operator/(TimeDelta a) const {
    return delta_ / a.delta_;
  }

  // Comparison operators.
  bool operator==(TimeDelta other) const {
    return delta_ == other.delta_;
  }
  bool operator!=(TimeDelta other) const {
    return delta_ != other.delta_;
  }
  bool operator<(TimeDelta other) const {
    return delta_ < other.delta_;
  }
  bool operator<=(TimeDelta other) const {
    return delta_ <= other.delta_;
  }
  bool operator>(TimeDelta other) const {
    return delta_ > other.delta_;
  }
  bool operator>=(TimeDelta other) const {
    return delta_ >= other.delta_;
  }

 private:
  friend class Time;
  friend class TimeTicks;
  friend TimeDelta operator*(int64 a, TimeDelta td);

  // Constructs a delta given the duration in microseconds. This is private
  // to avoid confusion by callers with an integer constructor. Use
  // FromSeconds, FromMilliseconds, etc. instead.
  explicit TimeDelta(int64 delta_us) : delta_(delta_us) {
  }

  // Delta in microseconds.
  int64 delta_;
};
// Inline the TimeDelta factory methods, for fast TimeDelta construction.

// static
inline TimeDelta TimeDelta::FromDays(int64 days) {
  return TimeDelta(days * Time::kMicrosecondsPerDay);
}

// static
inline TimeDelta TimeDelta::FromHours(int64 hours) {
  return TimeDelta(hours * Time::kMicrosecondsPerHour);
}

// static
inline TimeDelta TimeDelta::FromMinutes(int64 minutes) {
  return TimeDelta(minutes * Time::kMicrosecondsPerMinute);
}

// static
inline TimeDelta TimeDelta::FromSeconds(int64 secs) {
  return TimeDelta(secs * Time::kMicrosecondsPerSecond);
}

// static
inline TimeDelta TimeDelta::FromMilliseconds(int64 ms) {
  return TimeDelta(ms * Time::kMicrosecondsPerMillisecond);
}

// static
inline TimeDelta TimeDelta::FromMicroseconds(int64 us) {
  return TimeDelta(us);
}

inline TimeDelta operator*(int64 a, TimeDelta td) {
  return TimeDelta(a * td.delta_);
}

// based on condition_variable_win.cc from chrome
// (http://src.chromium.org/viewvc/chrome/trunk/src/base/synchronization/condition_variable_win.cc?revision=70363&view=markup)
class CondVar {
 public:
  explicit CondVar(Mutex* user_lock);
  ~CondVar();

  void Wait() {
      // Default to "wait forever" timing, which means have to get a Signal()
      // or Broadcast() to come out of this wait state.
      TimedWait(TimeDelta::FromMilliseconds(INFINITE));
  }

  void TimedWait(const TimeDelta& max_time);
  void Signal();
  void SignalAll();
 private:
   // Define Event class that is used to form circularly linked lists.
   // The list container is an element with NULL as its handle_ value.
   // The actual list elements have a non-zero handle_ value.
   // All calls to methods MUST be done under protection of a lock so that links
   // can be validated.  Without the lock, some links might asynchronously
   // change, and the assertions would fail (as would list change operations).
   class Event {
    public:
     // Default constructor with no arguments creates a list container.
     Event();
     ~Event();
   
     // InitListElement transitions an instance from a container, to an element.
     void InitListElement();
   
     // Methods for use on lists.
     bool IsEmpty() const;
     void PushBack(Event* other);
     Event* PopFront();
     Event* PopBack();
   
     // Methods for use on list elements.
     // Accessor method.
     HANDLE handle() const;
     // Pull an element from a list (if it's in one).
     Event* Extract();
   
     // Method for use on a list element or on a list.
     bool IsSingleton() const;
   
    private:
     // Provide pre/post conditions to validate correct manipulations.
     bool ValidateAsDistinct(Event* other) const;
     bool ValidateAsItem() const;
     bool ValidateAsList() const;
     bool ValidateLinks() const;
   
     HANDLE handle_;
     Event* next_;
     Event* prev_;
     //DISALLOW_COPY_AND_ASSIGN(Event);
   };
   
   // Note that RUNNING is an unlikely number to have in RAM by accident.
   // This helps with defensive destructor coding in the face of user error.
   enum RunState { SHUTDOWN = 0, RUNNING = 64213 };
   
   // Internal implementation methods supporting Wait().
   Event* GetEventForWaiting();
   void RecycleEvent(Event* used_event);
   
   RunState run_state_;
   
   // Private critical section for access to member data.
   Mutex internal_lock_;
   
   // Lock that is acquired before calling Wait().
   Mutex& user_lock_;
   
   // Events that threads are blocked on.
   Event waiting_list_;
   
   // Free list for old events.
   Event recycling_list_;
   int recycling_list_size_;
   
   // The number of allocated, but not yet deleted events.
   int allocation_counter_;
};
#endif

#if 1
// Storage for a lock-free pointer
class AtomicPointer {
 private:
  void * rep_;
 public:
  AtomicPointer() : rep_(nullptr) { }
  explicit AtomicPointer(void* v) {
      Release_Store(v);
  }
  void* Acquire_Load() const {
    void * p = nullptr;
    InterlockedExchangePointer(&p, rep_);
    return p;
  }

  void Release_Store(void* v) {
    InterlockedExchangePointer(&rep_, v);
  }

  void* NoBarrier_Load() const {
    return rep_;
  }

  void NoBarrier_Store(void* v) {
    rep_ = v;
  }
};
#endif

inline bool Snappy_Compress(const char* input, size_t length,
                            ::std::string* output) {
#ifdef SNAPPY
  output->resize(snappy::MaxCompressedLength(length));
  size_t outlen;
  snappy::RawCompress(input, length, &(*output)[0], &outlen);
  output->resize(outlen);
  return true;
#endif

  return false;
}

inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
                                         size_t* result) {
#ifdef SNAPPY
  return snappy::GetUncompressedLength(input, length, result);
#else
  return false;
#endif
}

inline bool Snappy_Uncompress(const char* input, size_t length,
                              char* output) {
#ifdef SNAPPY
  return snappy::RawUncompress(input, length, output);
#else
  return false;
#endif
}

inline bool GetHeapProfile(void (*func)(void*, const char*, int), void* arg) {
  return false;
}

}
}

#if 0
#define COMPILER_MSVC 1
#define ARCH_CPU_X86_FAMILY 1
#include "atomic_pointer.h"
#endif

#endif  // STORAGE_LEVELDB_PORT_PORT_WIN_H_
