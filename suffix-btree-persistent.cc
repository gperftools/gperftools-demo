// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <algorithm>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <assert.h>
#include <stdio.h>

#include "demo-helper.h"

#ifndef ENABLE_BTREE_FASTPATH
#define ENABLE_BTREE_FASTPATH 1
#endif

struct Node;

// NodePtr is our refcounted smart pointer to Node. Similar to
// shared_ptr, but a) immutable and non-null b) uses non-atomic ops
// for refcounting
class NodePtr {
public:
  explicit NodePtr(const Node* p) : ptr_(p) {
    IncRef(ptr_);
  }
  NodePtr(const NodePtr& other) : ptr_(other.ptr_) {
    IncRef(ptr_);
  }
  ~NodePtr() {
    DecRef(ptr_);
  }

  const Node& operator *() const {
    return *ptr_;
  }
  const Node* operator->() const {
    return ptr_;
  }
  const Node* Get() const {
    return ptr_;
  }

private:
  static void IncRef(const Node* p);
  static void DecRef(const Node* p);
  const Node* const ptr_;
};

// SplitRes is a result of node split. When full leaf insertion
// happens, we can use this struct to return both leaf "halves" and
// separation key. Similarly, when dealing with internal nodes, child
// split adds a key, and may require internal node split.
struct SplitRes {
  const Node* left;
  std::string_view key;
  const Node* right;
};

// Our nodes are logically immutable, and to help us construct nodes
// we have a couple helper templates that describe lists of keys/child
// pointers to copy into node we're creating. See
// Node::{MakeInternal,MakeLeaf} below.
namespace span_ops {

// InsertOp holds one list of elements (typically std::span or could
// be another span_op), position and value. And describes list of
// elements where value is inserted into a given position.
template <typename T, typename A>
struct InsertOp {
  const A& elements;
  const size_t pos;
  const T& value;

  InsertOp(const A& elements, size_t pos, const T& value) : elements(elements), pos(pos), value(value) {}

  const T& operator[](size_t i) const {
    if (i < pos) {
      return elements[i];
    } else if (i == pos) {
      return value;
    } else {
      return elements[i - 1];
    }
  }

  size_t size() const {
    return elements.size() + 1;
  }
};

// ReplaceOp holds one list of elements (typically std::span or could
// be another span_op), position and value. And describes list of
// elements where value replaces elements' contents in a given
// position.
template <typename T, typename A>
struct ReplaceOp {
  const A& elements;
  const size_t pos;
  const T& value;

  ReplaceOp(const A& elements, const size_t pos, const T& value) : elements(elements), pos(pos), value(value) {}

  const T& operator[](size_t i) const {
    if (i == pos) {
      return value;
    } else {
      return elements[i];
    }
  }

  size_t size() const {
    return elements.size();
  }
};

}  // namespace span_ops

// Node is our refcounted immutable internal or leaf btree node.
// Internal nodes contain up to kWidth keys and kWidth + 1 child
// pointers. Leaf nodes contain up to kLeafWidth keys. Actual number
// of keys is stored in size member variable. For internal nodes
// number of child pointers is always one more then number of keys.
//
// As per usual btree internal node semantics keys[i] is a key that is
// greater than all keys at smaller indexes and greater than entire
// subtree at children[i]. So children[size] (i.e. last child pointer)
// is entire subtree that is greater than keys[size-1].
//
// Another usual property of those nodes is that (other than for root
// node) size is at least half of max. possible width.
struct Node {
  static constexpr int kWidth = 19;
  static constexpr int kInternalPointersOffset =
    kWidth * sizeof(std::string_view);
  static constexpr int kInternalSize =
    (kWidth + 1) * sizeof(NodePtr) + kInternalPointersOffset;

  static constexpr int kLeafWidth =
    kInternalSize / sizeof(std::string_view);

  mutable int refcount;
  const int size;
  const bool is_leaf;

private:
  // storage is where we're constructing our array of keys and array
  // of child NodePtr.
  alignas(std::string_view) char storage[kInternalSize];

  // Keys are stored first in the storage
  const std::string_view* GetKeysStorage() const {
    return reinterpret_cast<const std::string_view*>(storage);
  }
  // Pointers are stored at the end of storage (at fixed offset
  // suitable for maximum node width case).
  const NodePtr* GetPtrStorage() const {
    assert(!is_leaf);
    return reinterpret_cast<const NodePtr*>(storage + kInternalPointersOffset);
  }

  // Real constructors are MakeXYZ static methods below. This only
  // constructs "trivial" members.
  Node(int size, bool is_leaf) : refcount(0), size(size), is_leaf(is_leaf) {
    assert(size > 0);
    if (is_leaf) {
      assert(size <= kLeafWidth);
    } else {
      assert(size <= kWidth);
    }
  }

  friend class NodePtr;
  // Node instances can only be destroyed via refcounting in NodePtr
  ~Node() {
    if (!is_leaf) {
      for (const NodePtr& p : GetChildren()) {
        const_cast<NodePtr&>(p).~NodePtr();
      }
    }
  }
public:

  // MakeInternal constructs internal node from given list of keys and
  // childs. Keys and children are assumed to be ordered
  // appropriately.
  //
  // Note, newly constructed node has refcount of 0, and it is
  // caller's responsibility to construct NodePtr from it (usually as
  // part of linking into a tree). We don't return NodePtr directly
  // because C++ implementations don't have means of returning smart
  // pointers in registers.
  template <typename KeysT, typename ChildsT>
  static const Node* MakeInternal(const KeysT& keys, const ChildsT& childs) {
    int size = keys.size();
    assert(childs.size() == static_cast<size_t>(size + 1));

    Node* ret = new Node(size, false);
    std::string_view* kp = const_cast<std::string_view*>(ret->GetKeysStorage());
    for (int i = 0; i < size; i++) {
      new (static_cast<void*>(kp + i)) std::string_view(keys[i]);
    }
    NodePtr* pp = const_cast<NodePtr*>(ret->GetPtrStorage());
    for (int i = 0; i <= size; i++) {
      new (static_cast<void*>(pp + i)) NodePtr(childs[i]);
    }
    return ret;
  }

  // MakeLeaf constructs leaf node from given list of keys. See above
  // for note about returning 'naked' Node* directly.
  template <typename KeysT>
  static const Node* MakeLeaf(const KeysT& keys) {
    int size = keys.size();
    Node* ret = new Node(size, true);
    std::string_view* kp = const_cast<std::string_view*>(ret->GetKeysStorage());
    for (int i = 0; i < size; i++) {
      new (static_cast<void*>(kp + i)) std::string_view(keys[i]);
    }
    return ret;
  }

  bool CanInsertInLeaf() const {
    assert(is_leaf);
    return size < kLeafWidth;
  }

  bool CanInsertInInternal() const {
    assert(!is_leaf);
    return size < kWidth;
  }

  std::span<const NodePtr> GetChildren() const {
    return {GetPtrStorage(), static_cast<size_t>(size + 1)};
  }
  std::span<const std::string_view> GetKeys() const {
    return {GetKeysStorage(), static_cast<size_t>(size)};
  }

  // FindInsertPos returns index of a smallest key that is >= than
  // given value.
  int FindInsertPos(std::string_view value) const {
    auto keys = GetKeys();
    return std::lower_bound(keys.begin(), keys.end(), value) - keys.begin();
  }

  // Splits leaf into 2 halves (extracting middle key into
  // SplitRes#key field).
  SplitRes SplitLeaf() const {
    assert(is_leaf);
    assert(size == kLeafWidth);

    int mid = kLeafWidth / 2;
    auto keys = GetKeys();

    return SplitRes{
      MakeLeaf(keys.subspan(0, mid)),
      keys[mid],
      MakeLeaf(keys.subspan(mid + 1))};
  }

  // Splits internal node.
  SplitRes SplitInternal() const {
    assert(!is_leaf);
    assert(size == kWidth);
    int mid = kWidth / 2;

    auto keys = GetKeys();
    auto children = GetChildren();

    auto mk = [&] (int from, int to) -> const Node* {
      return MakeInternal(keys.subspan(from, to - from),
                          children.subspan(from, to + 1 - from));
    };

    return SplitRes{
      mk(0, mid),
      keys[mid],
      mk(mid + 1, kWidth)};
  }

  // Builds new node with child at given index replaced by given
  // node. See note at MakeInternal above about implications of
  // returning naked Node* pointer.
  const Node* ReplaceChild(int child_index, const Node* new_child) const {
    assert(!is_leaf);
    assert(child_index <= size);
    assert(new_child);

    return MakeInternal(GetKeys(),
                        span_ops::ReplaceOp(GetChildren(),
                                            child_index,
                                            NodePtr{new_child}));
  }

  // Builds new leaf with given string view inserted at given
  // position. See note at MakeInternal above about implications of
  // returning naked Node* pointer.
  const Node* InsertIntoLeaf(int pos, std::string_view value) const {
    assert(is_leaf);
    assert(size < kLeafWidth);
    return MakeLeaf(span_ops::InsertOp(GetKeys(), pos, value));
  }

  // Builds new internal node with given split 'installed' at given
  // position. We're replacing one children at position `pos` with new
  // key and 2 children.
  const Node* InsertIntoInternal(int pos, const SplitRes& split) const {
    assert(!is_leaf);
    assert(size < kWidth);
    assert(pos <= size);
    return MakeInternal(
      span_ops::InsertOp(GetKeys(), pos, split.key),
      span_ops::InsertOp(
        span_ops::ReplaceOp(GetChildren(),
                            pos,
                            NodePtr{split.right}),
        pos,
        NodePtr{split.left}));
  }

  // Builds new node with only 2 children and 1 key. This node is only
  // valid as root.
  static const Node* MakeInternalFromSplit(const SplitRes& split) {
    NodePtr kids[2] = {NodePtr{split.left}, NodePtr{split.right}};
    return Node::MakeInternal(
      std::span<const std::string_view>(&split.key, 1),
      std::span<NodePtr>(kids, 2));
  }
};

void NodePtr::IncRef(const Node* p) {
  p->refcount++;
}
void NodePtr::DecRef(const Node* p) {
  if (--p->refcount == 0) {
    delete p;
  }
}

struct BTree {
  std::optional<NodePtr> root;

  void Insert(std::string_view value);
  const std::string_view* LowerBound(std::string_view str);
  int Validate();
};

void BTree::Insert(std::string_view value) {
  struct R {
    // We use simplified (and slightly more efficient) insertion
    // strategy. Rather than having recursive insert calls return
    // either rewritten child subtree or a split, we deal with potential
    // splits as we descend down the tree.
    //
    // Simplest implementation would literally be splitting full nodes
    // as we descend down the tree.
    //
    // We use slightly more elaborate version of that. The idea is to
    // only discover 'a run' of nodes that needs splitting and split
    // only them. We use 2-pass approach. On the first pass need_split
    // is set to false and we descend down the leaf and check if leaf
    // needs to be split. If it doesn't need to be split (which is by
    // far most common case) then we do the job and bubble up
    // rewritten subtree as usual up to the root and we're
    // done. I.e. in the most common case we don't really need 2
    // passes and we're done early.
    //
    // If leaf needs to be split, then we return nullptr and signal to
    // the parent that split is needed. This nullptr 'signal' is
    // bubbled up the tree to one level above the lowest point where
    // split is necessary. And then we retry recursion with need_split
    // set to true. As part of that all the nodes we traverse are
    // split. Ultimately this first splits the leaf and then inserts
    // into one of the halves.
    //
    // By doing this we obviate the need for Rec to return something
    // like std::variant<const Node*, SplitRes> which is less
    // efficient in practice (and, again, splits are uncommon; so no
    // need to pay the price all the time).
    //
    // Yes, potential second pass not only rewrites nodes twice (first
    // as part of splitting the child and second as part of rewriting
    // the tree after recursive insertion), but also adds redundant
    // FindInsertPos calls. But splits are rare enough for us to
    // easily afford this slight extra overhead at massive gain at
    // clarity and common-case performance. Also second pass deals
    // with 'warmed up' CPU caches populated by first pass, so is much
    // faster in practice.
    static const Node* Rec(const Node* n, std::string_view value, bool need_split) {
      int pos = n->FindInsertPos(value);

      if (n->is_leaf) {
        if (!n->CanInsertInLeaf()) {
          assert(!need_split);
          return nullptr; // signal that we need to split, so make
                          // sure parent is ready and splits us
        }
        return n->InsertIntoLeaf(pos, value);
      }

      const Node* kid = n->GetChildren()[pos].Get();

      if (need_split) [[unlikely]] {
        assert(n->CanInsertInInternal());
        assert(kid->is_leaf ? !kid->CanInsertInLeaf() : !kid->CanInsertInInternal());

        n = n->InsertIntoInternal(pos, kid->is_leaf ? kid->SplitLeaf() : kid->SplitInternal());
        NodePtr n_cleanup{n};

        pos = n->FindInsertPos(value);
        const Node* kid = Rec(n->GetChildren()[pos].Get(), value, true);
        return n->ReplaceChild(pos, kid);
      }

      assert(!need_split);
      const Node* new_kid = Rec(kid, value, false);
      if (new_kid == nullptr) {
        bool space_in_kid = !kid->is_leaf && kid->CanInsertInInternal();
        if (!space_in_kid) {
          // propagate "split needed" signal up. Parent will retry.
          return nullptr;
        }
        // ok, kid returned "split needed signal", but it actually
        // does have space. So that kid is the one that will re-try
        // inserting with _its_ kids split.
        new_kid = Rec(kid, value, true);
        assert(new_kid);
      }
      return n->ReplaceChild(pos, new_kid);
    }

    // TryFastPath efficiently deals with common case of btree only
    // having refcount of 1 and not having to split anything. In this
    // case, we 'cheat' a bit and rewrite leaf's child pointer
    // directly in it's parent internal node instead of bubbling up
    // node rewrites up to the tree. This saves main overhead of
    // common case insertions and makes our implementation roughly
    // competitive with imperative, non-persistent and polished abseil
    // btree code.
    static bool TryFastPath(const Node* n, std::string_view value) {
      if (n->is_leaf) {
        return false; // leaf root isn't fast-path (yet)
      }
      while (n->refcount == 1) {
        int pos = n->FindInsertPos(value);
        NodePtr* child_place = const_cast<NodePtr*>(&n->GetChildren()[pos]);
        const Node* child = child_place->Get();
        if (child->is_leaf) {
          if (!child->CanInsertInLeaf()) {
            return false; // fast-path is when leaf doesn't need splitting
          }

          // This is fast-path case. Okay, at this point we saw that
          // entire path from root to this leaf has refcounts of
          // 1. I.e. we're the only copy. So we produce new leaf, but
          // link it "directly" into it's parent's child pointer,
          // avoiding cost of rewriting the path up the tree.
          int child_pos = child->FindInsertPos(value);
          const Node* new_child = child->InsertIntoLeaf(child_pos, value);

          child_place->~NodePtr();
          new (static_cast<void*>(child_place)) NodePtr{new_child};

          return true; // yay \o/
        }

        assert(!child->is_leaf);
        n = child;
      }

      return false; // looks like we found internal node with refcount > 1
    }
  };

  if (!root) {
    root.emplace(Node::MakeLeaf(std::span<const std::string_view>(&value, 1)));
    return;
  }

#if ENABLE_BTREE_FASTPATH
  if (R::TryFastPath(root->Get(), value)) {
    return; // if fast-path succeeded, then root remains the same node
  }
#endif

  const Node* n = R::Rec(root->Get(), value, false);
  if (!n) [[unlikely]] {
    // root or immediate children of root (down the path to "our" key)
    // needs to be split. So we check.
    n = root->Get();
    bool root_full = n->is_leaf ? !n->CanInsertInLeaf() : !n->CanInsertInInternal();
    if (!root_full) {
      n = R::Rec(n, value, true);
    } else {
      // Ok root is full, so we split and make tree one level taller.
      SplitRes split = n->is_leaf ? n->SplitLeaf() : n->SplitInternal();
      // And then we insert into one of the halves, forcing splits all
      // the way down to leafs.
      if (value < split.key) {
        split.left = R::Rec(NodePtr{split.left}.Get(), value, true);
      } else {
        split.right = R::Rec(NodePtr{split.right}.Get(), value, true);
      }

      n = Node::MakeInternalFromSplit(split);
    }
  }

  root.emplace(n);
}

const std::string_view* BTree::LowerBound(std::string_view str) {
  if (!root) {
    return nullptr;
  }

  struct R {
    static const std::string_view* Rec(const Node* n, std::string_view str) {
      auto keys = n->GetKeys();
      if (n->is_leaf) {
        auto it = std::lower_bound(keys.begin(), keys.end(), str);
        if (it == keys.end()) {
          return nullptr;
        }
        return &*it;
      }

      int pos = n->FindInsertPos(str);
      const std::string_view* child_result = Rec(n->GetChildren()[pos].Get(), str);
      if (!child_result && static_cast<size_t>(pos) < keys.size()) {
        return &keys[pos];
      }
      return child_result;
    }
  };

  return R::Rec(root->Get(), str);
}

int BTree::Validate() {
  struct Checker {
    const Node* const root;
    std::optional<std::string_view> prev_seen;

    Checker(const Node* root) : root(root) {}

    // VisitKey validates that our btree actually stores keys in-order.
    void VisitKey(std::string_view v) {
      if (prev_seen) {
        bool ok = (prev_seen.value() < v);
        assert(ok);
        if (!ok) { abort(); }
      }
      prev_seen.emplace(v);
    }

    // AssertSize checks node size invariants.
    void AssertSize(const Node* n) {
      bool is_root = (n == root);
      if (n->is_leaf) {
        int min_size = is_root ? 1 : (Node::kLeafWidth - 1) / 2;
        bool leaf_size_ok = (min_size / 2 <= n->size) && (n->size <= Node::kLeafWidth);
        assert(leaf_size_ok); if (!leaf_size_ok) { abort(); }
      } else {
        int min_size = is_root ? 1 : (Node::kWidth - 1) / 2;
        bool node_size_ok = (min_size <= n->size) && (n->size <= Node::kWidth);
        assert(node_size_ok); if (!node_size_ok) { abort(); }
      }
    }

    int Rec(const Node* n) {
      AssertSize(n);
      if (n->is_leaf) {
        for (std::string_view s : n->GetKeys()) {
          VisitKey(s);
        }
        return 1;
      }

      int size = n->size;
      auto kids = n->GetChildren();
      int child_height = Rec(kids[0].Get());
      for (int i = 0; i < size; i++) {
        VisitKey(n->GetKeys()[i]);
        int this_height = Rec(kids[i + 1].Get());
        assert(child_height == this_height); if (child_height != this_height) { abort(); }
      }

      return child_height + 1;
    }
  };

  if (!root) return 0;

  return Checker{root->Get()}.Rec(root->Get());
}

int main(int argc, char** argv) {
  BTree locations; // we want to clean up btree last so that heap
                   // sample dump we arrange just below, happens while
                   // btree is still populated.

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  AtomicFlag stop_req;
  auto sigint_cleanup = SignalHelper::OnSIGINT(&stop_req);

  printf("kWidth: %d, kLeafWidth: %d, Node size: %zu, kInternalSize: %zu\n", Node::kWidth, Node::kLeafWidth, sizeof(Node), size_t{Node::kInternalSize});

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    locations.Insert(std::string_view{s}.substr(pos));
    if (stop_req) {
      fprintf(stderr, "interrupted insertions by seeing SIGINT\n");
      break;
    }
#ifndef NDEBUG
    size_t num_inserted = s.size() - pos;
    // We want to validate often when we're at small tree, but
    // otherwise avoid O(N^2) blowup in debug builds.
    if (num_inserted < 128 || (num_inserted & (num_inserted - 1)) == 0) {
      locations.Validate();
      printf("inserted %zu suffixes so far\n", num_inserted);
    }
#endif
  }

#ifndef NDEBUG
  printf("Tree height we built is %d\n", locations.Validate());
#endif

  auto it = locations.LowerBound("the Roman Empire");
  assert(it != nullptr);

  size_t off = it->data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
