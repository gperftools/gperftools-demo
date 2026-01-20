// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <algorithm>
#include <bit>
#include <functional>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "demo-helper.h"

struct Leaf {
  const std::string_view data;
  Leaf(std::string_view data) : data(data) {}
};

struct Node;

class NodePtr {
public:
  NodePtr() : value_(0) {}
  explicit NodePtr(Leaf* ptr) : value_(reinterpret_cast<uintptr_t>(ptr) | 1) {
    assert(IsLeaf());
  }
  explicit NodePtr(Node* ptr) : value_(reinterpret_cast<uintptr_t>(ptr)) {
    assert(!IsLeaf());
  }

  NodePtr(NodePtr&) = delete;
  NodePtr& operator=(NodePtr&) = delete;

  NodePtr(NodePtr&& other) {
    value_ = other.value_;
    other.value_ = 0;
  }
  NodePtr& operator=(NodePtr&& other) {
    Reset();
    value_ = other.value_;
    other.value_ = 0;
    return *this;
  }

  bool IsLeaf() const {
    return (value_ & 1) != 0;
  }

  bool IsEmpty() const {
    return value_ == 0;
  }

  void Unpack(Leaf** l, Node **n) const {
    if (IsLeaf()) {
      *l = reinterpret_cast<Leaf*>(value_ & ~uintptr_t{1});
      *n = nullptr;
    } else {
      *l = nullptr;
      *n = reinterpret_cast<Node*>(value_);
    }
  }

  ~NodePtr() {
    Reset();
  }

  void Reset();

private:
  uintptr_t value_;
};

namespace detail {

struct ArrayIndex {
  uint64_t in_use_bits[4] = {};
  uint8_t start_indexes[4] = {};

  bool HasElement(uint8_t pos) const {
    uint8_t word_idx = pos / 64;
    uint8_t bit = pos % 64;
    return  ((in_use_bits[word_idx] >> bit) & 1) != 0;
  }
  uint8_t NumElementsBefore(uint8_t pos) const {
    uint8_t word_idx = pos / 64;
    uint8_t bit = pos % 64;
    uint8_t start = start_indexes[word_idx];
    uint64_t mask = ~(~uint64_t{} << bit);
    return start + std::popcount(in_use_bits[word_idx] & mask);
  }

  void InitInUse(uint8_t bit) {
    in_use_bits[bit / 64] |= uint64_t{1} << (bit % 64);
  }

  void FinishInitialization() {
    uint8_t acc = 0;
    for (int i = 0; i < 4; i++) {
      start_indexes[i] = acc;
      acc += std::popcount(in_use_bits[i]);
    }
  }
};

}  // namespace detail

struct Node {
  const uint32_t size;
  const uint32_t depth;
  detail::ArrayIndex idx;
  uintptr_t children[/* size */];

  static Node* MakeInserting(Node* prev_node, uint8_t ch, uint8_t pos, Leaf* leaf) {
    assert(!prev_node->idx.HasElement(ch));
    assert(pos == prev_node->idx.NumElementsBefore(ch));
    uint32_t size = prev_node->size + 1;
    Node* n;
    NodePtr* childs_storage;
    std::tie(n, childs_storage) = Allocate(size, prev_node->depth);

    memcpy(&n->idx, &prev_node->idx, sizeof(n->idx));
    n->idx.InitInUse(ch);
    n->idx.FinishInitialization();

    std::span<NodePtr> prev_children = prev_node->GetChildren();

    for (uint32_t i = 0; i < size; i++) {
      if (i == pos) {
        new (childs_storage + i) NodePtr(leaf);
      } else if (i < pos) {
        new (childs_storage + i) NodePtr(std::move(prev_children[i]));
      } else {
        new (childs_storage + i) NodePtr(std::move(prev_children[i - 1]));
      }
    }

    return n;
  }

  static Node* MakeFrom2(uint32_t depth,
                         uint8_t ch1, NodePtr&& child1,
                         uint8_t ch2, NodePtr&& child2) {
    assert(ch1 != ch2);
    NodePtr* c1 = &child1;
    NodePtr* c2 = &child2;
    if (ch1 > ch2) {
      std::swap(c1, c2);
    }
    Node* n;
    NodePtr* childs_storage;
    std::tie(n, childs_storage) = Allocate(2, depth);

    n->idx.InitInUse(ch1);
    n->idx.InitInUse(ch2);
    n->idx.FinishInitialization();
    new (childs_storage) NodePtr(std::move(*c1));
    new (childs_storage + 1) NodePtr(std::move(*c2));

    return n;
  }

  NodePtr* GetSmallestChild() {
    return &GetChildren()[0];
  }

  void EnumChildren(const std::function<void(uint8_t, const NodePtr&)>& body) {
    uint32_t i = 0;
    uint8_t ch = 0;
    std::span<NodePtr> children = GetChildren();
    do {
      if (idx.HasElement(ch)) {
        body(ch, children[i]);
        i++;
      }
      ch++;
    } while (ch != 0);
  }

  NodePtr* FindChild(uint8_t ch, uint8_t* pos_place) const {
    uint8_t pos = idx.NumElementsBefore(ch);
    if (pos_place) {
      *pos_place = pos;
    }
    if (!idx.HasElement(ch)) {
      return nullptr;
    } else {
      return &GetChildren()[pos];
    }
  }

private:
  friend class NodePtr;

  std::span<NodePtr> GetChildren() const {
    return {reinterpret_cast<NodePtr*>(const_cast<uintptr_t*>(children)), static_cast<size_t>(size)};
  }

  static void Delete(Node* node) {
    size_t alloc_size = offsetof(Node, children) + sizeof(NodePtr) * node->size;
    node->~Node();
#if __cpp_sized_deallocation
    (::operator delete)(node, alloc_size);
#else
    (::operator delete)(node);
    (void)alloc_size; // unused in this config. Avoid warning.
#endif
  }

  static std::pair<Node*, NodePtr*> Allocate(uint32_t size, uint32_t depth) {
    size_t alloc_size = offsetof(Node, children) + sizeof(NodePtr) * size;

    Node* n = new ((::operator new)(alloc_size)) Node(size, depth);
    NodePtr* childs_storage = reinterpret_cast<NodePtr*>(n->children);
    return {n, childs_storage};
  }

  Node(uint8_t size, uint32_t depth) : size(size), depth(depth) {}

  ~Node() {
    for (NodePtr& p : GetChildren()) {
      p.~NodePtr();
    }
  }
};

static uint8_t ReadString(std::string_view data, size_t depth) {
  if (depth < data.size()) [[likely]] {
    return data[depth];
  }
  return 0;
}

void NodePtr::Reset() {
  Leaf* l;
  Node* n;
  Unpack(&l, &n);
  value_ = 0;
  if (l) {
    delete l;
  } else if (n) {
    Node::Delete(n);
  }
}

std::pair<Leaf*, size_t> FindLCPLeaf(NodePtr* place, std::string_view data) {
  Leaf* leaf;
  Node* node;

  for (;;) {
    place->Unpack(&leaf, &node);
    if (leaf) {
      break;
    }

    uint8_t ch = ReadString(data, node->depth);
    NodePtr* new_place = node->FindChild(ch, nullptr);
    if (!new_place) {
      // Entire subtree at node has common prefix, but we don't know
      // what is that prefix exactly (because successive down-ward
      // steps could have skipped over chars). In order to get this
      // prefix we simply step down to any concrete leaf.
      do {
        node->GetSmallestChild()->Unpack(&leaf, &node);
      } while (leaf == nullptr);
      break;
    }

    place = new_place;
  }

  size_t lcp = std::mismatch(
    data.begin(), data.end(),
    leaf->data.begin(), leaf->data.end()).first - data.begin();

  return {leaf, lcp};
}

void Insert(NodePtr* root_place, std::string_view data) {
  if (root_place->IsEmpty()) {
    *root_place = NodePtr{new Leaf(data)};
    return;
  }

  // Okay, whatever common prefix other_leaf and our `data' string
  // have is the depth at which we'll be inserting our new
  // leaf. I.e. it is possible to prove there are no other leaf
  // (i.e. existing set member) that has longer common prefix with our
  // data.
  Leaf* other_leaf;
  size_t lcp;
  std::tie(other_leaf, lcp) = FindLCPLeaf(root_place, data);

  // We guarantee that our data set has no strings where one is prefix
  // of the other. This is because all suffixes end in \0 and there is
  // no \0 character in our main text.
  assert(lcp < data.size());

  NodePtr* place = root_place;

  for (;;) {
    Leaf* leaf;
    Node* node;

    place->Unpack(&leaf, &node);
    if (leaf != nullptr || node->depth > lcp) {
      auto example_char = ReadString(other_leaf->data, lcp);
      auto this_char = ReadString(data, lcp);
      *place = NodePtr{
        Node::MakeFrom2(lcp,
                        example_char, std::move(*place),
                        this_char, NodePtr{new Leaf(data)})};
      return;
    }

    uint8_t ch = ReadString(data, node->depth);
    uint8_t pos;
    NodePtr* new_place = node->FindChild(ch, &pos);
    if (node->depth == lcp) {
      assert(!new_place);
      *place = NodePtr{Node::MakeInserting(node, ch, pos, new Leaf(data))};
      return;
    }

    place = new_place;
  }
}

struct ValidationState {
  size_t leaf_count = 0;
  size_t node_count = 0;
  size_t max_depth = 0;
  size_t depth_total = 0;

  size_t node_size_freq[257] = {};
  size_t depth_freq[256] = {};
};

#define validation_assert(c) do { \
    if (!(c)) { \
      fprintf(stderr, "validation assert failed: %s at %s:%d\n", #c, __FILE__, __LINE__); \
      fflush(stderr); \
      __builtin_trap(); \
    } \
  } while (false)

std::string_view DoValidate(ValidationState* state, const NodePtr& ptr, uint32_t min_depth) {
  Leaf* l;
  Node* n;
  ptr.Unpack(&l, &n);

  if (l) {
    state->leaf_count++;
    state->depth_total += min_depth;
    state->max_depth = std::max<size_t>(state->max_depth, min_depth);
    state->depth_freq[min_depth]++;
    return l->data;
  }
  validation_assert(n != nullptr);

  state->node_count++;

  size_t size = n->size;
  validation_assert(size <= 256);
  validation_assert(size > 0);
  validation_assert(n->depth >= min_depth);

  state->node_size_freq[size]++;

  std::string_view my_lcp;
  uint8_t prev_ch;
  uint32_t seen_children = 0;

  n->EnumChildren(
    [&] (uint8_t ch, const NodePtr& ptr) -> void {
      validation_assert(!ptr.IsEmpty());
      std::string_view lcp = DoValidate(state, ptr, n->depth + 1);
      validation_assert(lcp.size() > n->depth);
      validation_assert(static_cast<uint8_t>(lcp[n->depth]) == ch);

      if (seen_children == 0) {
        my_lcp = lcp;
      } else {
        size_t len = std::mismatch(my_lcp.begin(), my_lcp.end(),
                                   lcp.begin(), lcp.end()).first - my_lcp.begin();
        validation_assert(len == n->depth);
        my_lcp = my_lcp.substr(0, len);
        validation_assert(prev_ch < ch);
      }
      prev_ch = ch;
      seen_children++;
    });

  validation_assert(seen_children == n->size);

  return my_lcp;
}

void ValidateTrie(NodePtr* root) {
  ValidationState state{};
  DoValidate(&state, *root, 0);

  printf("trie-size. leafs: %zu, node: %zu\n", state.leaf_count, state.node_count);
  for (int i = 0; i <= 256; i++) {
    if (!state.node_size_freq[i]) continue;
    printf("node_size_freq[%d]: %zu\n", i, state.node_size_freq[i]);
  }
  printf("\nmax_depth: %zu\n", state.max_depth);
  printf("average depth: %g\n", static_cast<double>(state.depth_total) / state.leaf_count);
  for (size_t i = 0; i <= state.max_depth; i++) {
    printf("node_depth_freq[%zu]: %zu\n", i, state.depth_freq[i]);
  }
}

Leaf* LowerBound(NodePtr* root_place, std::string_view data) {
  if (root_place == nullptr) {
    return nullptr;
  }

  struct R {
    std::string_view data;
    Leaf* other_leaf;
    size_t lcp;

    Leaf* Rec(NodePtr* place) {
      Leaf* leaf;
      Node* node;
      place->Unpack(&leaf, &node);
      if (leaf) {
        if (leaf->data > data) {
          return leaf;
        }
        return nullptr;
      }

      // Note, we can do tighter but this is correct.
      uint8_t ch = (node->depth > lcp) ? 0 : ReadString(data, node->depth);
      NodePtr* child_place = node->FindChild(ch, nullptr);
      if (child_place) {
        leaf = Rec(child_place);
        if (leaf) {
          return leaf;
        }
      }

      for (uint32_t i = ch + 1; i < 256; i++) {
        child_place = node->FindChild(i, nullptr);
        if (child_place) {
          break;
        }
      }
      if (!child_place) {
        return nullptr;
      }

      for (;;) {
        child_place->Unpack(&leaf, &node);
        if (leaf) {
          break;
        }
        child_place = node->GetSmallestChild();
      }

      if (leaf->data > data) {
        return leaf;
      }
      return nullptr;
    }
  };

  R r;
  std::tie(r.other_leaf, r.lcp) = FindLCPLeaf(root_place, data);
  r.data = data;

  return r.Rec(root_place);

}

int main(int argc, char** argv) {
  // NOTE, we want this to be destroyed after heap sample dump we
  // setup just below.
  NodePtr locations;

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);

  std::string s = ReadRomanHistoryText();
  s.append(1, '\0');

  AtomicFlag stop_req;
  auto sigint_cleanup = SignalHelper::OnSIGINT(&stop_req);

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    auto l = std::string_view{s}.substr(pos);
    Insert(&locations, l);
    if (stop_req) {
      fprintf(stderr, "interrupted insertions by seeing SIGINT\n");
      break;
    }

#ifndef NDEBUG
    size_t num_inserted = s.size() - pos;
    // We want to validate often when we're at small tree, but
    // otherwise avoid O(N^2) blowup in debug builds.
    if (num_inserted < 128 || (num_inserted & (num_inserted - 1)) == 0) {
      ValidateTrie(&locations);
      printf("inserted %zu suffixes so far\n", num_inserted);
    }
#endif
  }

#ifndef NDEBUG
  ValidateTrie(&locations);
#endif

  auto it = LowerBound(&locations, "the Roman Empire");
  assert(it != nullptr);
  if (it == nullptr) {
    printf("failed to find\n");
    abort();
  }

  size_t off = it->data.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
