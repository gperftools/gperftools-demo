// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <algorithm>  // For std::sort, std::reverse, std::min
#include <array>      // For std::array
#include <atomic>     // For std::atomic
#include <chrono>     // For timing and sleep
#include <condition_variable> // For std::condition_variable
#include <coroutine>  // Coroutine support
#include <exception>  // For std::exception_ptr, std::current_exception
#include <functional> // For std::function
#include <limits>     // For std::numeric_limits
#include <memory>     // For std::unique_ptr, std::exchange
#include <mutex>      // For std::mutex, std::unique_lock, std::lock_guard
#include <optional>
#include <span>       // For std::span
#include <stdexcept> // For std::runtime_error
#include <thread>    // For std::thread (used by ReporterThread)
#include <tuple>     // For returning multiple values from ParseArguments
#include <utility>   // For std::pair
#include <vector>

#include <assert.h>    // For assert()
#include <math.h>      // For std::pow (used in distance calc)
#include <pthread.h>  // Use POSIX threads directly
#include <semaphore.h>
#include <stdint.h>    // For uint8_t, uint64_t
#include <stdio.h>     // For printf, fprintf
#include <stdlib.h>    // For std::atoi, EXIT_FAILURE, EXIT_SUCCESS, abort
#include <string.h>    // For memcpy, strerror

#include "demo-helper.h"

// --- Preprocessor Flag ---
// Define this (e.g., via -D compiler flag or uncommenting) to use
// the recursive solver on a POSIX thread with a large stack.
// #define USE_POSIX_THREAD_RECURSION
// --- End Preprocessor Flag ---


// --- Coordinate representation using std::pair ---
using Pos = std::pair<int, int>; // Alias for coordinates (row, col)

// --- PosSet Bitset Implementation ---
class PosSet {
public:
  static constexpr int kSize = 4096;
  PosSet() : val_(new uint8_t[kValSize]{}) {}
  PosSet(const PosSet&) = delete;
  PosSet& operator=(const PosSet&) = delete;
  PosSet(PosSet&&) = delete;
  PosSet& operator=(PosSet&&) = delete;

  bool contains(Pos p) const {
    auto [ptr, bit] = getbit(p);
    return (*ptr & (uint8_t{1} << bit)) != 0;
  }
  bool insert(Pos p) {
    auto [ptr, bit] = getbit(p);
    auto mask = uint8_t{1} << bit;
    if ((*ptr & mask) != 0) { return false; }
    *ptr |= mask;
    size_++;
    return true;
  }
  size_t erase(Pos p) {
    auto [ptr, bit] = getbit(p);
    auto mask = uint8_t{1} << bit;
    if ((*ptr & mask) != 0) {
      *ptr &= ~mask;
      size_--;
      return 1;
    } else {
      return 0;
    }
  }
  int size() const { return size_; }

private:
  static constexpr int kValSize = (kSize * kSize + 7) / 8;
  std::pair<uint8_t*, int> getbit(Pos p) const {
    assert(p.first >= 0 && p.first < kSize && p.second >= 0 &&
           p.second < kSize && "Position out of bounds for PosSet capacity");
    int bit_index = p.first * kSize + p.second;
    return {val_.get() + bit_index / 8, bit_index % 8};
  }
  std::unique_ptr<uint8_t[]> val_;
  int size_{};
};
// --- End of PosSet Implementation ---

// --- Coroutine Task Implementation ---
// Using C++20 coroutines (via Task) allows for deep recursion depth without
// overflowing the stack, as coroutine state is typically heap-allocated.
template <typename T>
class [[nodiscard]] Task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    T value_{};
    std::exception_ptr exception_{};
    std::coroutine_handle<> continuation_ = nullptr;

    Task<T> get_return_object() {
      return Task<T>{handle_type::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(handle_type h) noexcept {
        return h.promise().continuation_ ? h.promise().continuation_
          : std::noop_coroutine();
      }
      void await_resume() noexcept {}
    };
    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_value(T value) noexcept { value_ = std::move(value); }
    void unhandled_exception() noexcept {
      exception_ = std::current_exception();
    }
  };

  handle_type coro_;

  explicit Task(handle_type h) : coro_(h) {}
  Task(Task&& t) noexcept : coro_(std::exchange(t.coro_, {})) {}
  ~Task() {
    if (coro_) coro_.destroy();
  }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  struct TaskAwaiter {
    handle_type coro_to_await;

    bool await_ready() const noexcept {
      return !coro_to_await || coro_to_await.done();
    }
    std::coroutine_handle<> await_suspend(
      std::coroutine_handle<> awaiting_coro) noexcept {
      coro_to_await.promise().continuation_ = awaiting_coro;
      return coro_to_await;
    }
    T await_resume() {
      if (coro_to_await.promise().exception_) {
        std::rethrow_exception(coro_to_await.promise().exception_);
      }
      return std::move(coro_to_await.promise().value_);
    }
  };

  auto operator co_await() { return TaskAwaiter{coro_}; }

  T get() {
    if (!coro_.done()) {
      coro_.resume();
    }
    if (!coro_.done()) {
      throw std::runtime_error(
        "Coroutine did not complete synchronously in get()");
    }
    if (coro_.promise().exception_) {
      std::rethrow_exception(coro_.promise().exception_);
    }
    return std::move(coro_.promise().value_);
  }
};
// --- End of Task Implementation ---

// --- Knight's Tour Implementation ---
class KnightTourSolver {
public:
  // Constructor: Takes actual board dimensions. Initializes members in order.
  KnightTourSolver(int rows, int cols)
    : rows_(rows),
      cols_(cols),
      total_squares_(rows * cols),
      center_r_((rows - 1) / 2.0), // Depends on rows_
      center_c_((cols - 1) / 2.0), // Depends on cols_
      backtrack_count_(0),
      min_backtrack_depth_(std::numeric_limits<int>::max())
    {
      assert(rows > 0 && cols > 0 && "Board dimensions must be positive.");
      assert(rows <= PosSet::kSize && cols <= PosSet::kSize &&
             "Board dimensions exceed PosSet capacity (kSize)");
    }

  // --- Public Interface ---

  // Find tour using C++20 Coroutines (default)
  std::optional<std::vector<Pos>> find_tour_coroutine(Pos start_pos = {0, 0}) {
    assert(is_valid(start_pos) &&
           "Start position is outside the board dimensions.");
    reset_stats();
    PosSet initial_visited;
    Task<std::optional<std::vector<Pos>>> task =
      solve_coroutine(start_pos, initial_visited);
    std::optional<std::vector<Pos>> reversed_path = task.get(); // Blocking get
    if (reversed_path) {
      std::reverse(reversed_path->begin(), reversed_path->end());
    }
    return reversed_path;
  }

  // Find tour using standard recursion (intended to be run on custom stack)
  std::optional<std::vector<Pos>> find_tour_recursive(Pos start_pos = {0, 0}) {
    assert(is_valid(start_pos) &&
           "Start position is outside the board dimensions.");
    reset_stats();
    PosSet initial_visited;
    std::optional<std::vector<Pos>> reversed_path =
      solve_recursive(start_pos, initial_visited);
    if (reversed_path) {
      std::reverse(reversed_path->begin(), reversed_path->end());
    }
    return reversed_path;
  }

  // --- Stats Getters ---
  uint64_t get_backtrack_count() const {
    return backtrack_count_.load(std::memory_order_relaxed);
  }
  int get_min_backtrack_depth() const {
    int min_depth = min_backtrack_depth_.load(std::memory_order_relaxed);
    return (min_depth == std::numeric_limits<int>::max()) ? -1 : min_depth;
  }
  int get_total_squares() const {
    return total_squares_;
  }

  void request_abort() {
    abort_requested_.store(true);
  }

private:
  // --- Shared Helper Methods & Data ---
  static constexpr std::array<Pos, 8> kMoves_ = {{
      {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
      {1, -2},  {1, 2},  {2, -1},  {2, 1}
    }};

  bool is_valid(const Pos& p) const {
    return p.first >= 0 && p.first < rows_ && p.second >= 0 &&
      p.second < cols_;
  }

  int calculate_degree(const Pos& p, const PosSet& visited) const {
    int degree = 0;
    for (const auto& move : kMoves_) {
      Pos next_pos = {p.first + move.first, p.second + move.second};
      if (is_valid(next_pos) && !visited.contains(next_pos)) {
        degree++;
      }
    }
    return degree;
  }

  double dist_sq_from_center(const Pos& p) const {
    double dr = static_cast<double>(p.first) - center_r_;
    double dc = static_cast<double>(p.second) - center_c_;
    return dr * dr + dc * dc;
  }

  void reset_stats() {
    backtrack_count_.store(0);
    min_backtrack_depth_.store(std::numeric_limits<int>::max());
    abort_requested_.store(false);
  }

  void record_backtrack(int current_depth) {
    backtrack_count_.store(backtrack_count_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    int current_min = min_backtrack_depth_.load(std::memory_order_relaxed);
    if (current_depth < current_min) {
      min_backtrack_depth_.store(current_depth, std::memory_order_relaxed);
    }
  }

  // --- Helper function for Warnsdorff's Rule ---
  std::span<std::pair<int, Pos>> get_sorted_next_moves(
    Pos current_pos,
    const PosSet& visited,
    std::array<std::pair<int, Pos>, kMoves_.size()>& storage
    ) const {
    int num_valid_moves = 0;
    for (const auto& move : kMoves_) {
      Pos next_pos = {current_pos.first + move.first,
        current_pos.second + move.second};
      if (is_valid(next_pos) && !visited.contains(next_pos)) {
        int degree = calculate_degree(next_pos, visited);
        storage[num_valid_moves++] = {degree, next_pos};
      }
    }
    std::span<std::pair<int, Pos>> next_moves_span(
      storage.data(), num_valid_moves);
    std::sort(next_moves_span.begin(), next_moves_span.end(),
              [this](const auto& a, const auto& b) {
                if (a.first != b.first) {
                  return a.first < b.first;
                }
                return dist_sq_from_center(a.second) > dist_sq_from_center(b.second);
              });
    return next_moves_span;
  }
  // --- End Helper function ---


  // --- Coroutine Solver ---
  Task<std::optional<std::vector<Pos>>> solve_coroutine(Pos current_pos,
                                                        PosSet& visited) {
    // Step 1: Mark visited
    visited.insert(current_pos);

    // Step 2: Base Case
    if (visited.size() == total_squares_) {
      std::vector<Pos> final_path;
      final_path.push_back(current_pos);
      co_return final_path;
    }

    // Step 3: Get sorted next moves using the helper function.
    std::array<std::pair<int, Pos>, kMoves_.size()> next_moves_storage;
    auto sorted_moves_span = get_sorted_next_moves(current_pos, visited, next_moves_storage);

    // Step 4: Explore moves in the heuristic order (iterating the span).
    for (const auto& move_pair : sorted_moves_span) {
      Pos next_pos = move_pair.second;
      std::optional<std::vector<Pos>> result_path_segment =
        co_await solve_coroutine(next_pos, visited);
      if (result_path_segment) {
        result_path_segment->push_back(current_pos);
        co_return result_path_segment;
      }
      if (abort_requested_.load(std::memory_order_relaxed)) {
        break;
      }
    }

    // Step 5: Backtrack
    record_backtrack(visited.size());
    visited.erase(current_pos);
    co_return std::nullopt;
  }

  // --- Recursive Solver ---
  std::optional<std::vector<Pos>> solve_recursive(Pos current_pos,
                                                  PosSet& visited) {
    // Step 1: Mark visited
    visited.insert(current_pos);

    // Step 2: Base Case
    if (visited.size() == total_squares_) {
      std::vector<Pos> final_path;
      final_path.push_back(current_pos);
      return final_path;
    }

    // Step 3: Get sorted next moves using the helper function.
    std::array<std::pair<int, Pos>, kMoves_.size()> next_moves_storage;
    auto sorted_moves_span = get_sorted_next_moves(current_pos, visited, next_moves_storage);

    // Step 4: Explore moves in the heuristic order (iterating the span).
    for (const auto& move_pair : sorted_moves_span) {
      Pos next_pos = move_pair.second;
      std::optional<std::vector<Pos>> result_path_segment =
        solve_recursive(next_pos, visited);
      if (result_path_segment) {
        result_path_segment->push_back(current_pos);
        return result_path_segment;
      }
      if (abort_requested_.load(std::memory_order_relaxed)) {
        break;
      }
    }

    // Step 5: Backtrack
    record_backtrack(visited.size());
    visited.erase(current_pos);
    return std::nullopt;
  }


  // --- Member Variables (Declaration Order Matters for Initializer List) ---
  const int rows_;
  const int cols_;
  const int total_squares_;
  const double center_r_;
  const double center_c_;
  std::atomic<uint64_t> backtrack_count_{0};
  std::atomic<int> min_backtrack_depth_;
  std::atomic<bool> abort_requested_{};
};

// --- Argument Parsing Function ---
std::optional<std::tuple<int, Pos>> ParseArguments(int argc, char* argv[]) {
  int board_size = 1001;
  Pos start_position = {0, 1};
  bool args_valid = true;

  if (argc != 1 && argc != 2 && argc != 4) {
    fprintf(stderr, "Usage: %s [board_size] [start_row start_col]\n", argv[0]);
    return std::nullopt;
  }

  if (argc >= 2) {
    int arg_size = std::atoi(argv[1]);
    if (arg_size <= 0) {
      fprintf(stderr, "Error: Invalid board size argument '%s'. Must be a positive integer.\n", argv[1]);
      args_valid = false;
      board_size = -1; // Mark invalid
    } else {
      board_size = arg_size;
    }
  }

  if (argc == 4) {
    int start_row = std::atoi(argv[2]);
    int start_col = std::atoi(argv[3]);
    if (start_row < 0 || start_col < 0) {
      fprintf(stderr, "Error: Invalid start position arguments '%s', '%s'. Row and column must be non-negative.\n", argv[2], argv[3]);
      args_valid = false;
    } else {
      start_position = {start_row, start_col};
    }
  }

  // Validations
  if (board_size > PosSet::kSize) {
    fprintf(stderr, "Error: board_size (%d) exceeds PosSet capacity (%d).\n", board_size, PosSet::kSize);
    args_valid = false;
  }
  if (board_size <= 0 && argc >= 2) {
    args_valid = false; // Error already printed
  }
  if (board_size > 0) {
    if (start_position.first < 0 || start_position.first >= board_size ||
        start_position.second < 0 || start_position.second >= board_size) {
      fprintf(stderr, "Error: Start position (%d,%d) is outside the board dimensions (%dx%d).\n", start_position.first, start_position.second, board_size, board_size);
      args_valid = false;
    }
  } else if (argc >= 2) {
    args_valid = false; // Board size was invalid
  }

  if (args_valid) {
    return std::make_tuple(board_size, start_position);
  } else {
    return std::nullopt;
  }
}

// --- Helper Class for Reporter Thread ---
class ReporterThread {
public:
  ReporterThread(std::function<void()> action, std::chrono::seconds period)
    : action_(std::move(action)), period_(period), finished_(false) {
    thread_ = std::thread([this]() {
      while (true) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (cv_.wait_for(lock, period_, [this] { return finished_; })) {
          break; // Finished flag is true
        }
        // Wait timed out
        lock.unlock();
        action_(); // Assume action_ is valid
      }
    });
  }

  ~ReporterThread() {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      finished_ = true;
    }
    cv_.notify_one();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ReporterThread(const ReporterThread&) = delete;
  ReporterThread& operator=(const ReporterThread&) = delete;
  ReporterThread(ReporterThread&&) = delete;
  ReporterThread& operator=(ReporterThread&&) = delete;

private:
  std::function<void()> action_;
  std::chrono::seconds period_;
  std::thread thread_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool finished_;
};
// --- End Helper Class ---

#ifdef USE_POSIX_THREAD_RECURSION
// --- POSIX Thread Helper for Recursive Solver ---

// Helper function to run a std::function on a thread with a specific stack size.
// Aborts on POSIX errors.
void run_with_stack(size_t stack_size, std::function<void()> work_func)
{
  pthread_t thread_id;
  pthread_attr_t attr;
  int ret;

  static_assert(sizeof(void*) == 8, "Large stack size requires 64-bit architecture.");

  ret = pthread_attr_init(&attr);
  if (ret != 0) {
    fprintf(stderr, "Error: pthread_attr_init failed: %s (%d)\n", strerror(ret), ret);
    abort();
  }

  ret = pthread_attr_setstacksize(&attr, stack_size);
  if (ret != 0) {
    fprintf(stderr, "Error: pthread_attr_setstacksize failed: %s (%d)\n", strerror(ret), ret);
    abort();
  }

  auto thread_lambda_entry = [](void* arg) -> void* {
    auto* func_ptr = static_cast<std::function<void()>*>(arg);
    (*func_ptr)();
    return nullptr;
  };

  ret = pthread_create(&thread_id, &attr, thread_lambda_entry, &work_func);
  pthread_attr_destroy(&attr); // Destroy attributes after create
  if (ret != 0) {
    fprintf(stderr, "Error: pthread_create failed: %s (%d)\n", strerror(ret), ret);
    abort();
  }

  ret = pthread_join(thread_id, nullptr);
  if (ret != 0) {
    fprintf(stderr, "Warning: pthread_join failed: %s (%d)\n", strerror(ret), ret);
    // abort(); // Optionally abort
  }
}
// --- End POSIX Thread Helper ---
#endif // USE_POSIX_THREAD_RECURSION

// --- Main Function ---
int main(int argc, char* argv[]) {
  auto heap_sample_cleanup = MaybeSetupHeapSampling("heap-sample", 2<<20);

  auto parsed_args = ParseArguments(argc, argv);
  if (!parsed_args) {
    return EXIT_FAILURE;
  }

  auto [board_size, start_position] = *parsed_args;

  KnightTourSolver solver(board_size, board_size);

  std::optional<std::vector<Pos>> tour;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time, end_time;
  std::optional<ReporterThread> reporter;

  // Common setup: Start clock and reporter thread
  start_time = std::chrono::high_resolution_clock::now();
  reporter.emplace([&]() { // Use default capture [&] - captures solver and start_time
    uint64_t count = solver.get_backtrack_count();
    int min_depth = solver.get_min_backtrack_depth();
    int total_squares = solver.get_total_squares();

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = now - start_time;
    double rate = (elapsed_seconds.count() > 0.01)
      ? (static_cast<double>(count) / elapsed_seconds.count())
      : 0.0;

    // Print all stats, including rate, regardless of execution path
    printf("[Reporter] Backtracks: %llu (Avg Rate: %.1f/sec), Min Depth: %d/%d\n",
           (unsigned long long)count, rate, min_depth, total_squares);
  }, std::chrono::seconds(1));

  auto* siginit_cleanup = new SignalHelper::Cleanup{SignalHelper::OnSIGINT([&] () -> bool {
    printf("got SIGINT\n");
    heap_sample_cleanup.DumpHeapSampleNow();
    solver.request_abort();

    return false;
  })};

  // Print message *after* reporter starts (accepting potential minor race)
  printf("Finding Knight's Tour (%s) on a %dx%d board starting at (%d,%d)...\n",
#ifdef USE_POSIX_THREAD_RECURSION
         "POSIX Thread Recursion",
#else
         "Coroutines",
#endif
         board_size, board_size, start_position.first, start_position.second);


#ifdef USE_POSIX_THREAD_RECURSION
  // --- POSIX Thread Execution Path ---
  constexpr size_t kStackSize = 4ULL * 1024 * 1024 * 1024; // 4 GiB
  run_with_stack(kStackSize, [&]() { // Work lambda captures necessary variables
    tour = solver.find_tour_recursive(start_position);
  });
  // --- End POSIX Thread Path ---
#else
  // --- Coroutine Execution Path (Default) ---
  tour = solver.find_tour_coroutine(start_position); // Run solver
  // --- End Coroutine Path ---
#endif

  // Common teardown
  end_time = std::chrono::high_resolution_clock::now();
  delete siginit_cleanup;
  reporter.reset(); // Stop and join reporter thread

  // --- Output Results ---
  std::chrono::duration<double, std::milli> duration_ms = end_time - start_time;
  uint64_t final_backtrack_count = solver.get_backtrack_count();
  int final_min_depth = solver.get_min_backtrack_depth();
  int final_total_squares = solver.get_total_squares();

  if (tour) {
    printf("Tour found (%zu steps) in %.3f ms.\n", tour->size(), duration_ms.count());
    printf("Total Backtracks: %llu\n", (unsigned long long)final_backtrack_count);
    printf("Min Backtrack Depth: %d/%d\n", final_min_depth, final_total_squares);
    printf("Path: ");
    for (size_t i = 0; i < tour->size(); ++i) {
      printf("(%d,%d)", (*tour)[i].first, (*tour)[i].second);
      if (i < tour->size() - 1) {
        printf(" -> ");
      } else {
        printf("\n");
      }
    }
  } else {
    printf("No tour found from the starting position in %.3f ms.\n", duration_ms.count());
    printf("Total Backtracks: %llu\n", (unsigned long long)final_backtrack_count);
    printf("Min Backtrack Depth: %d/%d\n", final_min_depth, final_total_squares);
  }

  return EXIT_SUCCESS;
}
