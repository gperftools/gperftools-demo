// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <assert.h>
#include <stdio.h>

#include "demo-helper.h"

struct Node;

class NodePtr {
public:
  NodePtr() : ptr_{nullptr} {}

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

  explicit operator bool() const {
    return ptr_ != nullptr;
  }
private:
  static void IncRef(const Node* p);
  static void DecRef(const Node* p);
  const Node* const ptr_;
};

struct Node {
  mutable int refcount{};
  const int height;
  const NodePtr left;
  const NodePtr right;

  const std::string_view value;

  friend class NodePtr;

  static int HeightOf(const Node* ptr) {
    return ptr ? ptr->height : 0;
  }

  static int BalanceOf(const Node* left, const Node* right) {
    return HeightOf(right) - HeightOf(left);
  }

  explicit Node(std::string_view value) : height(1), left{}, right{}, value(value) { }

  Node(const Node* left, std::string_view value, const Node* right)
    : height(std::max(HeightOf(left), HeightOf(right)) + 1), left(left), right(right), value(value) {
    assert(abs(BalanceOf(left, right)) < 2);
  }

  bool GreaterThan(std::string_view value) const {
    return value < this->value;
  }

  const Node* RawLeft() const {
    return left.Get();
  }
  const Node* RawRight() const {
    return right.Get();
  }

  static __attribute__((always_inline))
  const Node* MakeAndRebalance(const Node* left, std::string_view value, const Node* right) {
    int balance = BalanceOf(left, right);
    if ((balance & 3) != 2) {
      // not out of balance. So just create Node directly
      return new Node(left, value, right);
    }

    return MakeAndRebalanceSlowPath(left, value, right, balance);
  }

  static const Node* MakeAndRebalanceSlowPath(const Node* left, std::string_view value, const Node* right, int balance) {
    // left and right might be freshly out of MakeAndRebalance and
    // have "bogus" refcount of 0. So lets make sure we're cleaning
    // them up if we end up un-using one of them as part of
    // rebalancing logic.
    NodePtr left_holder{left};
    NodePtr right_holder{right};

    // We have 4 subtrees and 3 keys which are in-order and we'll be
    // building new tree out of them
    auto mk3 = [] (const Node* a,
                   std::string_view k1, const Node* b,
                   std::string_view k2, const Node* c,
                   std::string_view k3, const Node* d) {
      assert(!a || a->value < k1);
      assert(k1 < k2);
      assert(!b || b->value < k2);
      assert(k2 < k3);
      assert(!c || c->value < k3);
      assert(!d || d->value > k3);

      return new Node(
        new Node(a, k1, b),
        k2,
        new Node(c, k3, d));
    };

    // Same as above but with 2 keys and 3 subtrees.
    auto mk2 = [] (const Node* a,
                   std::string_view k1, const Node* b,
                   std::string_view k2, const Node* c,
                   bool root_at_k1) {
      assert(!a || a->value < k1);
      assert(k1 < k2);
      assert(!b || b->value < k2);
      assert(!c || c->value > k2);
      if (root_at_k1) {
        return new Node(
          a,
          k1,
          new Node(b, k2, c));
      } else {
        return new Node(
          new Node(a, k1, b),
          k2,
          c);
      }
    };

    if (balance == -2) {
      // Node we're about to create is out of balance and left side is
      // deeper. Which implies left subtree is nonnull.
      assert(left);
      if (BalanceOf(left->RawLeft(), left->RawRight()) == 1) {
        // left-right is the "deepest" of the nodes considered, so
        // "pull" it to the top, keeping the order. Being deepest also
        // implies it is non-null.
        const Node* left_right = left->RawRight();
        assert(left_right);
        return mk3(left->RawLeft(), left->value, left_right->RawLeft(), left_right->value, left_right->RawRight(), value, right);
      } else {
        return mk2(left->RawLeft(), left->value, left->RawRight(), value, right, true /* new root at ex-left */);
      }
    } else {
      assert(balance == 2);
      assert(right);
      if (BalanceOf(right->RawLeft(), right->RawRight()) == -1) {
        const Node* right_left = right->RawLeft();
        assert(right_left);
        return mk3(left, value, right_left->RawLeft(), right_left->value, right_left->RawRight(), right->value, right->RawRight());
      } else {
        return mk2(left, value, right->RawLeft(), right->value, right->RawRight(), false /* new root is at ex-right */);
      }
    }
  }
};

void NodePtr::IncRef(const Node* p) {
  if (p) { p->refcount++; }
}
void NodePtr::DecRef(const Node* p) {
  if (!p) return;
  if (--p->refcount == 0) {
    delete p;
  }
}

struct AVLTree {
  std::optional<NodePtr> root;

  void Insert(std::string_view value) {
    if (!root) {
      root.emplace(new Node(value));
      return;
    }

    struct R {
      static const Node* Rec(const Node* node, std::string_view value) {
        if (!node) {
          return new Node(value);
        }
        auto left = node->RawLeft();
        auto right = node->RawRight();
        if (node->GreaterThan(value)) {
          // Because rebalance is going to touch right child, lets
          // prefetch it
          __builtin_prefetch(right, 1, 1);
          left = Rec(left, value);
        } else {
          __builtin_prefetch(left, 1, 1);
          right = Rec(right, value);
        }
        return Node::MakeAndRebalance(left, node->value, right);
      }
    };

    root.emplace(R::Rec(root->Get(), value));
  }

  void Validate(bool print_stats) {
    if (!root) {
      return;
    }
    DoValidate(root->Get(), print_stats);
  }

  static const Node* DoValidate(const Node* root, bool print_stats) {
    struct Checker {
      std::optional<std::string_view> prev_seen;
      size_t total_height = {};
      size_t node_count = {};

      // Checking node's subtree returns it's height
      int Rec(const Node* node, int depth) {
        if (!node) {
          return 0;
        }

        total_height += depth;
        node_count += 1;

        assert(node->refcount > 0);
        if (node->refcount < 1) { abort(); }

        int left_height = Rec(node->RawLeft(), depth + 1);

        // In addition to checking balance we ensure that in-order tree
        // traversal sees nodes in increasing order.
        if (prev_seen.has_value()) {
          assert(node->value >= *prev_seen);
          if (node->value < *prev_seen) {
            abort();
          }
        }
        prev_seen = node->value;

        int right_height = Rec(node->RawRight(), depth + 1);

        int expected_height = std::max(left_height, right_height) + 1;
        assert(expected_height == node->height);
        if (expected_height != node->height) { abort(); }

        int balance = Node::BalanceOf(node->RawLeft(), node->RawRight());
        assert(abs(balance) < 2);
        if(abs(balance) >= 2) { abort(); }

        return node->height;
      }
    };

    Checker checker;
    int max_height = checker.Rec(root, 1);

    if (print_stats) {
      printf("total node count: %zu, average depth: %g, max_height: %d\n",
             checker.node_count,
             static_cast<double>(checker.total_height) / checker.node_count,
             max_height);
    }

    return root;
  }

  // We find smallest node that is >= than given string, or nullptr if
  // everything is smaller than str.
  const Node* LowerBound(std::string_view str) {
    if (!root) {
      return nullptr;
    }
    const Node* node = root->Get();

    const Node* best = node->value >= str ? node : nullptr;
    while (node) {
      if (node->value < str) {
        node = node->RawRight();
      } else {
        best = node;
        node = node->RawLeft();
      }
    }
    return best;
  }

};

int main(int argc, char** argv) {
  AVLTree locations; // Note, we want this destructor to run after
                     // we've dumped heap sample

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  AtomicFlag stop_req;
  auto sigint_cleanup = SignalHelper::OnSIGINT(&stop_req);

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
      locations.Validate(false);
      printf("inserted %zu suffixes so far\n", num_inserted);
    }
#endif
  }

#ifndef NDEBUG
  locations.Validate(true);
#endif

  printf("AVL tree height = %d\n", locations.root.value()->height);

  const Node* it = locations.LowerBound("the Roman Empire");
  assert(it);

  size_t off = it->value.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
