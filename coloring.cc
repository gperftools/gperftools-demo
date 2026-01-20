// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// #undef NDEBUG
// #define DEBUG 1

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "demo-helper.h"

#include "coloring-graph-src-inl.h"

template <typename... Args>
void dprintf(const char* fmt, Args... args) {
#if defined(DEBUG)
  printf(fmt, args...);
#endif
}


static constexpr int kColors = 4;

// ColorSet is a bitset of colors. std::bitset isn't efficient for our needs.
struct ColorSet {
  static_assert(kColors <= 8);

  uint8_t value_ = (1 << kColors) - 1;
  ColorSet& reset() {
    value_ = 0;
    return *this;
  }
  ColorSet& reset(int bit) {
    assert(bit < kColors);
    value_ &= ~(uint8_t{1} << bit);
    return *this;
  }
  ColorSet& set(int bit, bool new_value = true) {
    assert(bit < kColors);
    if (!new_value) {
      return reset(bit);
    }
    value_ |= (uint8_t{1} << bit);
    return *this;
  }
  bool operator[](int bit) const {
    return (value_ & (uint8_t{1} << bit)) != 0;
  }
  size_t size() const { return kColors; }
  size_t count() const {
    return std::popcount(value_);
  }

  bool count_is_one() const {
    return ((value_ & (value_ - 1)) == 0) && value_;
  }
  bool is_empty() const {
    return value_ == 0;
  }

  void make_singleton_at_bit(int bit) {
    assert(bit < kColors);
    value_ = uint8_t{1} << bit;
  }

  int set_index() const {
    return 7 - std::countl_zero(value_);
  }
};

static int GetColor(const ColorSet& colors) {
  assert(colors.count() == 1);
  return colors.set_index();
}

std::array<double, kColors> kColorEntropyDeltas = ([] () {
  std::array<double, kColors> ret;
  for (int i = 1; i < kColors; i++) {
    ret[i] = log2(static_cast<double>(i + 1)) - log2(static_cast<double>(i));
  }
  return ret;
})();

template <class Child>
struct Refcountable {
  int32_t refcount;
  Refcountable() : refcount(0) {}

  void Ref() { refcount++; }
  void UnRef() {
    refcount--;
    assert(refcount >= 0);
    if (refcount == 0) {
      delete static_cast<Child*>(this);
    }
  }

protected:
  Refcountable(const Refcountable& other) : refcount(0) {}
  virtual ~Refcountable() {}

  template <typename T>
  friend struct RefPtr;
};

template <typename T>
struct RefPtr {
  T* ptr;

  RefPtr() : ptr(nullptr) {}
  explicit RefPtr(T* ptr) : ptr(ptr) { ptr->Ref(); }
  RefPtr(const RefPtr& other) : ptr(other.ptr) { ptr->Ref(); }

  RefPtr& operator=(const RefPtr& other) {
    Reset();
    ptr = other.ptr;
    if (ptr) {
      ptr->Ref();
    }
    return *this;
  }

  ~RefPtr() {
    Reset();
  }

  bool IsEmpty() const { return ptr == nullptr; }

  const T* operator->() const {
    return ptr;
  }
  const T& operator*() const {
    return *ptr;
  }

  T* Mutate() {
    if (ptr->refcount == 1) {
      return ptr;
    }
    ptr->refcount--;
    ptr = new T{*ptr};
    ptr->Ref();
    return ptr;
  }

  void Reset() {
    if (!ptr) [[unlikely]] { return; }
    ptr->UnRef();
    ptr = nullptr;
  }
};

template <typename T, size_t N>
struct LeafArray final : public Refcountable<LeafArray<T,N>> {
  static constexpr int kMaxSize = N;
  using LeafType = LeafArray;

  T array[N] = {};

  T& operator[](int idx) { return array[idx]; }
  const T& operator[](int idx) const { return array[idx]; }
  const T& ReadAt(int idx) const { return array[idx]; }

  static void PrintArrayStructure(FILE* f) {
    fprintf(f, "Leaf<%zu>", N);
  }
};

template <typename T, size_t UN, typename ChildType>
struct NonLeafArray final : public Refcountable<NonLeafArray<T, UN, ChildType>> {
  static constexpr int kChildSize = ChildType::kMaxSize;
  static constexpr int kMaxSize = UN * kChildSize;

  using LeafType = ChildType::LeafType;

  RefPtr<ChildType> array[UN];

  NonLeafArray() {
    for (size_t i = 0; i < UN; i++) {
      array[i] = RefPtr<ChildType>{new ChildType{}};
    }
  }

  T& operator[](int idx) {
    return (*array[idx / kChildSize].Mutate())[idx % kChildSize];
  }

  const T& ReadAt(int idx) const {
    return array[idx / kChildSize]->ReadAt(idx % kChildSize);
  }
  const T& operator[](int idx) const {
    return ReadAt(idx);
  }

  static void PrintArrayStructure(FILE* f) {
    fprintf(f, "NonLeaf<%zu, ", UN);
    ChildType::PrintArrayStructure(f);
    fprintf(f, ">");
  }
};

template <typename T, size_t N>
struct ArrayStructureCalculation {
  static consteval size_t GetLeafSize() {
    if (N <= 128) {
      return N;
    }
    return 128;
  }

  static consteval size_t GetNonLeafSize(size_t child_size) {
    size_t size_candidate = (N + child_size - 1) / child_size;
    if (size_candidate > 16) {
      return 16;
    }
    return size_candidate;
  }

  template <typename ChildType,
            std::enable_if_t<ChildType::kMaxSize < N, bool> = true>
  static auto Pick() {
    // If our current candidate array type isn't big enough to hold N
    // elements, then we tack a layer of NonLeafArray.
    using ArrayType = NonLeafArray<T, GetNonLeafSize(ChildType::kMaxSize), ChildType>;
    return Pick<ArrayType>();
  }
  template <typename ChildType,
            std::enable_if_t<ChildType::kMaxSize >= N, bool> = true>
  static auto Pick() {
    // If our current candidate array is big enough, then we're 'done'
    return ChildType{};
  }

  using ArrayType = decltype(Pick<LeafArray<T, GetLeafSize()>>());
};

template <typename T, size_t N>
using CopyableArray = ArrayStructureCalculation<T, N>::ArrayType;

using Coloring = CopyableArray<ColorSet, kSize>;

struct State {
  RefPtr<Coloring> coloring_ptr;
  std::bitset<kSize> frontier;
  double entropy_reduction = 0;
  int depth = 0;

  static inline size_t num_backtrackings;
  static inline size_t num_pick_colors;

  State() : coloring_ptr{new Coloring} { }
  State(const State& other) = default;

  using MaybeState = std::optional<State>;

  MaybeState PickColorAt(int node, int color) const {
    MaybeState ret{std::in_place, *this};
    if (!ret->DoPickColorAt(node, color)) {
      ret.reset();
    }
    return ret;
  }

  bool DoPickColorAt(int node, int color);
  bool Rec();
};

bool State::DoPickColorAt(int node, int color) {
  num_pick_colors++;

  std::vector<std::pair<int, int>> q;
  size_t init_capacity = 1 << ((std::bit_width(size_t{kSize - 1}) + 1) / 2);
  q.reserve(init_capacity);
  auto enq = [&] (int node, int color) {
    q.emplace_back(node, color);
  };
  auto deq = [&] () -> std::pair<int, int> {
    std::pair<int, int> ret = *q.rbegin();
    q.pop_back();
    return ret;
  };

  entropy_reduction = 0;
  depth++;

  Coloring& coloring = *coloring_ptr.Mutate();

  assert(frontier[node]);
  int orig_node = node;
  enq(node, color);
  coloring[node].make_singleton_at_bit(color);

  while (!q.empty()) {
    std::tie(node, color) = deq();
    for (int adj_node : kAdj[node]) {
      if (!coloring.ReadAt(adj_node)[color]) {
        continue;
      }

      ColorSet& adj_color = coloring[adj_node];

      adj_color.reset(color);
      int new_colors = adj_color.count();
      // dprintf("%d: DoPickColorAt at %d dropped possible color %d (%d colors left)\n",
      //        depth, adj_node, color, new_colors);
      if (new_colors == 0) {
        return false;
      }

      if (!frontier[adj_node]) {
        frontier.set(adj_node);
      }

      if (new_colors == 1) {
        entropy_reduction += 1; // change from 2 possible colors to 1 is reduction by 2x so 1 bit
        enq(adj_node, adj_color.set_index());
      } else {
        // from 4 to 3, or from 3 to 2
        entropy_reduction += kColorEntropyDeltas[new_colors];
      }
    }
  }

  frontier.reset(orig_node);
  return true;
}

bool State::Rec() {
  Coloring& coloring = *coloring_ptr.Mutate();

  // First step is figuring out which node and which color we're going
  // to try picking. We only explore frontier nodes.
  std::unique_ptr<MaybeState> best_child_state;
  int best_child = -1;
  int best_child_color = -1;
  for (int i = 0; i < kSize; i++) {
    if (!frontier[i]) {
      continue;
    }

    assert(!coloring[i].is_empty());

    for (int j = 0; j < kColors; j++) {
      if (!coloring[i][j]) {
        continue; // color j is already excluded for node i
      }
      std::unique_ptr<MaybeState> candidate{new MaybeState{PickColorAt(i, j)}};
      if (!candidate->has_value()) {
        // if we found color selection that is "impossible" then
        // lets continue with this child node
        best_child = i;
        best_child_color = j;
        std::swap(best_child_state, candidate);
        dprintf("%d: excluding color %d at node %i\n", depth, j, i);
        goto have_best_child;
      }

      auto is_better = [&] (int ia, const State& a, int ib, const State& b) -> bool {
        if (a.entropy_reduction < b.entropy_reduction) {
          return true;
        }
        if (coloring.ReadAt(ia).count() < coloring.ReadAt(ib).count()) {
          return true;
        }
        return false;
      };

      if (!best_child_state || is_better(i, best_child_state->value(), best_child, best_child_state->value())) {
        std::swap(best_child_state, candidate);
        best_child = i;
        best_child_color = j;
      }
    }
  }

  if (!best_child_state) {
    // Nothing was in frontier. This implies we've found an assignment
    // of colors that works \o/
    return true;
  }

  dprintf("%d: selected color %d at node %d\n", depth, best_child_color, best_child);
  dprintf(" entropy_reduction: %g, possible colors count: %zu\n",
          best_child_state->value().entropy_reduction,
          coloring[best_child].count());

  assert(best_child_state); assert(best_child_state->has_value());
have_best_child:

  if (best_child < 0) { abort(); }

  // Then for selected node and it's colors (starting with "best"
  // color we picked above) we recurse.
  do {
    if (best_child_state->has_value()) {
      auto& child_state = best_child_state->value();

      if (child_state.Rec()) {
        coloring_ptr = best_child_state->value().coloring_ptr;
        return true; // \o/
      }
    }

    // We just found that best_child_color assignment doesn't work. So
    // we exclude it.
    coloring[best_child].reset(best_child_color);
    if (coloring[best_child].is_empty()) {
      dprintf("%d: failure with node %d\n", depth, best_child);
      num_backtrackings++;
      return false;
    }

    dprintf("%d: excluded color %d at node %d\n", depth, best_child_color, best_child);

    // Lets pick next color to try among those we haven't excluded yet.
    int color;
    for (color = 0; color < kColors; color++) {
      if (coloring[best_child][color]) {
        break;
      }
    }
    assert(color < kColors);

    best_child_color = color;
    best_child_state.reset(new MaybeState{PickColorAt(best_child, color)});
    dprintf("%d: continuing with color %d at node %d\n", depth, best_child_color, best_child);
  } while(true);
  __builtin_unreachable();
}

// RenameGraph applies given reordering of nodes and returns "undo" function.
std::function<void(RefPtr<Coloring>& coloring_ptr)> RenameGraph(std::span<const int> ordering) {
  assert(ordering.size() == kSize);

  struct UndoState {
    const decltype(kAdj) old_adj;
    std::vector<int> perm = std::vector<int>(kSize);
    std::vector<std::vector<int>> storage;

    UndoState() : old_adj(kAdj) {}
    ~UndoState() {
      kAdj = old_adj;
    }
  };
  auto state_ptr = std::make_shared<std::unique_ptr<UndoState>>(std::make_unique<UndoState>());

  UndoState& state = **state_ptr;
  std::span<int> perm = state.perm;
  assert(perm.size() == kSize);
  for (int i = 0; i < kSize; i++) {
    perm[ordering[i]] = i;
  }

  auto copy_span = [&state] (std::span<const int> s) -> std::span<int> {
    std::vector<int>& copy_vec = state.storage.emplace_back(s.begin(), s.end());
    return copy_vec;
  };

  decltype(kAdj) new_adj;
  for (int i = 0; i < kSize; i++) {
    std::span<int> new_row = copy_span(kAdj[ordering[i]]);
    for (auto& x : new_row) {
      x = perm[x];
    }
    std::sort(new_row.begin(), new_row.end());
    new_adj[i] = new_row;
  }

  // lets check new_adj is isomorphic to kAdj
  for (int i = 0; i < kSize; i++) {
    int renamed_node = i;
    int old_node = ordering[i];
    const auto &renamed_adj = new_adj[renamed_node];
    const auto &old_adj = kAdj[old_node];

    assert(renamed_adj.size() == old_adj.size());
    if (renamed_adj.size() != old_adj.size()) { abort(); }
    for (int x : renamed_adj) {
      int old_x = ordering[x];
      bool exists = (std::find(old_adj.begin(), old_adj.end(), old_x) != old_adj.end());
      assert(exists);
      if (!exists) { abort(); }
    }
  }

  kAdj = new_adj;

  return [state_ptr] (RefPtr<Coloring>& coloring_ptr) {
    std::unique_ptr<UndoState> state_ownership{std::move(*state_ptr)};
    // reverse function can only be called once
    assert(state_ownership);

    UndoState& state = *state_ownership;

    const std::span<const int> rev_ordering = state.perm;

    RefPtr<Coloring> new_coloring_ptr{new Coloring};
    Coloring& new_coloring = *new_coloring_ptr.Mutate();
    const Coloring& old_coloring = *coloring_ptr;

    for (int i = 0; i < kSize; i++) {
      new_coloring[i] = old_coloring[rev_ordering[i]];
    }

    std::swap(coloring_ptr, new_coloring_ptr);
  };
}

void PrintOrdering(std::span<const int> ordering) {
  printf("ordering:\n");
  for (int i = 0; i < 10; i++) {
    printf("% 4d| ", i);
  }
  printf("\n-------------------------------------------------------------\n");
  for (int i = 0; i < kSize; i++) {
    printf("% 4d", ordering[i]);
    if ((i + 1) % 10 == 0 || i == kSize - 1) {
      if (i != kSize - 1) {
        printf(",");
      }
      printf(" /* %d */\n", (i / 10) * 10);
    } else {
      printf(", ");
    }
  }
  printf("\n");
}

std::array<int, kSize> RunDijkstra(int i) {
  std::array<int, kSize> ret;

  std::vector<int> frontier;
  frontier.reserve(kSize);
  std::bitset<kSize> seen;

  seen.set(i);
  frontier.push_back(i);
  ret[i] = 0;
  size_t frontier_idx = 0;

  while (frontier_idx < frontier.size()) {
    int node = frontier[frontier_idx++];
    for (int adj_node : kAdj[node]) {
      if (seen[adj_node]) {
        continue;
      }
      ret[adj_node] = ret[node] + 1;
      seen.set(adj_node);
      frontier.push_back(adj_node);
    }
  }

  return ret;
}

int FindCenterNode() {
  // To save time we only probe relatively small subset of
  // nodes. We're okay to be close, but not exactly at the center.
  int step = 1 << (std::bit_width(size_t{kSize - 1}) / 4);
  dprintf("step: %d\n", step);

  std::minstd_rand0 rnd{};

  int best_node = 0;
  int max_min = 1<<30;
  for (int i = 0; i < kSize; i++) {
    if ((rnd() % step) != 0) {
      // yes, this is slightly biased, but I want this to be same
      // between all implementations and this is the simplest way.
      continue;
    }

    using dist_array = std::array<int, kSize>;
    std::unique_ptr<dist_array> dist_ptr{new dist_array{RunDijkstra(i)}};
    const auto& dist = *dist_ptr;

    int max_path = 0;
    for (int j = 0; j < kSize; j++) {
      max_path = std::max(max_path, dist[j]);
    }
    if (max_path < max_min) {
      best_node = i;
      max_min = max_path;
    }
  }

  printf("approx. center node: %d (at radius: %d)\n", best_node, max_min);
  return best_node;
}

// BuildOrdering implements heuristic for reordering graph node
// ids. First node (at index 0) is approximate center of the graph.
// And rest of the ordering is simply breadth-first visit order. We
// use this order try to reduce mutations of the coloring array on
// each step. Roughly expecting to have adjacent node ids for adjacent
// nodes. I haven't tested this though.
std::vector<int> BuildOrdering() {
  int start = FindCenterNode();

  std::bitset<kSize> seen;
  std::vector<int> order;
  order.reserve(kSize);

  order.push_back(start);
  seen.set(start);
  size_t order_idx = 0;

  while (order_idx < order.size()) {
    int node = order[order_idx++];

    for (int adj_node : kAdj[node]) {
      if (seen[adj_node]) {
        continue;
      }
      order.push_back(adj_node);
      seen.set(adj_node);
    }
  }

  assert(order.size() == kSize);
  for (int i = 0; i < kSize; i++) {
    auto it1 = std::find(order.begin(), order.end(), i);
    assert(it1 != order.end()); if (it1 == order.end()) { abort(); }
    auto it2 = std::find(it1 + 1, order.end(), i);
    assert(it2 == order.end()); if (it2 != order.end()) { abort(); }
  }

  return order;
}


int main(int argc, char** argv) {
  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);

  printf("CopyableArray structure: ");
  Coloring::PrintArrayStructure(stdout);
  printf("\n");

#define DO_RENAME 1
#if DO_RENAME
  auto ordering = BuildOrdering();
  // PrintOrdering(ordering);
  auto rev_ordering = RenameGraph(ordering);
#endif

  State s;
  s.frontier.set(0);
  bool ok = s.Rec();

  printf("num_backtrackings: %zu\n", State::num_backtrackings);
  printf("num_pick_colors: %zu\n", State::num_pick_colors);

  if (!ok) {
    printf("failed!\n");
    return 1;
  }

#if DO_RENAME
  // Now reverse renaming of kAdj and apply matching renaming to
  // coloring
  rev_ordering(s.coloring_ptr);
#endif

  const Coloring& coloring = *s.coloring_ptr;

  for (int i = 0; i < kSize; i++) {
    auto color = GetColor(coloring[i]);
    for (int adj_node : kAdj[i]) {
      assert(0 <= adj_node && adj_node < kSize);
      auto adj_color = GetColor(coloring[adj_node]);
      assert(adj_color >= 0 && adj_color < kColors);
      if (adj_color == color) {
        printf("bad adj. color (%d) between nodes %d and %d\n", color, i, adj_node);
        fflush(stdout);
      }
      assert(adj_color != color);
    }
  }

  printf("found coloring:\n");
  for (int i = 0; i < kSize; i++) {
    if (i == 10 && kSize - 11 > 10) { printf("... skipped ...\n"); i = kSize - 11; }
    printf("node %d has color %d\n", i, GetColor(coloring[i]));
  }

  sampling_cleanup.DumpHeapSampleNow();
}
