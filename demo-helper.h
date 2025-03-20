// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#ifndef DEMO_HELPER_H_
#define DEMO_HELPER_H_
#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WE_HAVE_TCMALLOC
#include <gperftools/malloc_extension.h>
#endif

inline
std::string ReadFileToString(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    fprintf(stderr, "failed to open: %s\n", filename.c_str());
    abort();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

inline
std::string ReadRomanHistoryText() {
  std::string filename = "the-history-of-the-decline-and-fall-of-the-roman-empire.txt";
  std::string s = ReadFileToString(filename);
  printf("got file `%s' into a string. size = %zu\n", filename.c_str(), s.size());
  return s;
}

class DemoHelper {
public:
  DemoHelper(bool sampling_enabled,
             std::string heap_sample_file)
    : sampling_enabled_(sampling_enabled),
      heap_sample_file_(heap_sample_file) {
  }

  ~DemoHelper() {
    DumpHeapSampleNow();
  }

  void DumpHeapSampleNow() {
    if (heap_sample_dumped_) {
      return;
    }
    heap_sample_dumped_ = true;
    (void)sampling_enabled_; // "use" the field and silence clang warning

#ifdef WE_HAVE_TCMALLOC
    if (sampling_enabled_) {
      std::string sample;
      MallocExtension::instance()->GetHeapSample(&sample);
      {
        std::ofstream f(heap_sample_file_, std::ios_base::binary);
        if (!f.is_open()) {
          abort();
        }
        f << sample;
      }
      printf("Wrote heap-sample file. Run pprof --http=: %s to view\n",
             heap_sample_file_.c_str());
    }


    constexpr int kBufSize = 2 << 20;
    auto buf = std::unique_ptr<char[]>(new char[kBufSize]);
    MallocExtension::instance()->GetStats(buf.get(), kBufSize);
    printf("\nHere are tcmalloc stats:\n%s\n", buf.get());
#endif  // WE_HAVE_TCMALLOC
  }

private:
  bool sampling_enabled_;
  bool heap_sample_dumped_ = false;
  const std::string heap_sample_file_;
};

inline
DemoHelper MaybeSetupHeapSampling(std::optional<std::string_view> heap_sample_path,
                                         size_t sampling_period = 512 << 10) {
#ifdef WE_HAVE_TCMALLOC
  if (heap_sample_path.has_value()) {
    // Enable heap sampling and sample roughly every 512k bytes of allocations.
    bool ok = MallocExtension::instance()->SetNumericProperty(
      "tcmalloc.sample_parameter",
      sampling_period);
    if (!ok) {
      fprintf(stderr, "WARNING: Your tcmalloc or tcmalloc-like library failed to "
              "handle SetNumericProperty(\"tcmalloc.sample_parameter\",...)\n");
      fprintf(stderr, "Heap sampling is likely disabled, so expect empty heap sample file\n");
    }

    return DemoHelper{true, std::string{std::move(heap_sample_path).value()}};
  }
#endif
  return DemoHelper{false, ""};
}

inline
DemoHelper MaybeSetupHeapSampling(int argc, char** argv) {
#ifdef WE_HAVE_TCMALLOC
  if (argc > 1) {
    if (argc > 2) {
      printf("I only accept file name of heap sample to write to\n");
      exit(1);
    }
    return MaybeSetupHeapSampling(argv[1]);
  }
#endif
  return MaybeSetupHeapSampling({});
}

inline
void PrintOccurenceContext(std::string_view text, size_t off) {
  std::string context{text.substr(off - 32, 64)};
  std::replace(context.begin(), context.end(), '\n', ' ');
  std::replace(context.begin(), context.end(), '\t', ' ');
  while ((static_cast<uint8_t>(context[context.size() - 1]) & 0x80) != 0) {
    context.resize(context.size() - 1);
  }
  while ((static_cast<uint8_t>(context[0]) & 0x80) != 0) {
    context.erase(0, 1);
  }
  printf("%s\n", context.c_str());
}

struct AtomicFlag {
  std::atomic<bool> value{false};

  operator bool() const {
    return value.load(std::memory_order_relaxed);
  }
};

class SignalHelper {
public:
  class Cleanup {
  public:
    ~Cleanup() {
      cleanup_();
    }
    explicit Cleanup(std::function<void()>&& cleanup) : cleanup_(std::move(cleanup)) {}

    Cleanup(const Cleanup& other) = delete;
    Cleanup& operator=(const Cleanup& other) = delete;
  private:
    const std::function<void()> cleanup_;
  };

  static Cleanup OnSIGINT(std::function<bool()>&& body) {
    struct State {
      sem_t sigint_sem;
      std::mutex handlers_lock;
      std::thread thread;
      std::list<std::function<bool()>> handlers;
      bool sem_init_failed = false; // Damned OSX....

      State() {
        if (sem_init(&sigint_sem, 0, 0) != 0) {
          perror("sem_init");
          fprintf(stderr, "SIGINT won't be intercepted\n");
          sem_init_failed = true;
          return;
        }

        thread = std::thread([&] () {
          for (;;) {
            (void)sem_wait(&sigint_sem);
            std::lock_guard l(handlers_lock);

            bool at_least_once = false;
            for (std::function<bool()>& fn : handlers) {
              if (!fn) { continue; }
              at_least_once = true;
              if (!fn()) {
                fn = {};
              }
            }

            if (!at_least_once) {
              signal(SIGINT, SIG_DFL);
              raise(SIGINT);
              abort();
            }
          }
        });
      }
    };
    static State* state;
    static bool setup_done = ([] () -> bool {
      state = new State;
      if (!state->sem_init_failed) {
        signal(SIGINT, +[] (int dummy) -> void {
          int save_errno = errno;
          sem_post(&state->sigint_sem);
          errno = save_errno;
        });
      }
      return true;
    })();
    (void)setup_done;

    auto iter = state->handlers.insert(state->handlers.end(), std::move(body));
    return Cleanup([iter] () {
      std::lock_guard l(state->handlers_lock);
      state->handlers.erase(iter);
    });
  }

  static Cleanup OnSIGINT(AtomicFlag* flag) {
    return OnSIGINT([flag] () -> bool {
      flag->value.store(true);
      return false;
    });
  }
};

#endif  // DEMO_HELPER_H_
