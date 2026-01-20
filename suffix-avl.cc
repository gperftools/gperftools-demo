// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <memory>
#include <string>
#include <string_view>

#include <assert.h>
#include <stdio.h>

#include "demo-helper.h"

#ifndef USE_LOCAL_DATA_PREFIX
#define USE_LOCAL_DATA_PREFIX 0
#endif

namespace avl {

struct node {
  node *childs[2];
  int balance;
};

constexpr int kMaxLevels = 104; // ceil(64 * golden_ratio)

struct tree_path {
  int parents_idx = 0;
  node** parents[kMaxLevels];
  node* current_node;

  tree_path(node** root) {
    parents[0] = root;
    current_node = *root;
  }

  static void assert_local_balance(node *n) {
    assert(-2 < n->balance && n->balance < 2);
  }

  static int balance_towards(node *n, int idx) {
    return idx ? n->balance : -n->balance;
  }

  // Returns true iff balance is 2 or -2 (assuming balance cannot be
  // outside of [-2,2])
  static bool is_imbalanced(int balance) {
    // same as !(-2 < balance && balance < 2)
    return (balance & 3) == 2;
  }

  bool is_at_root() {
    return parents_idx == 0;
  }

  void reset_to_root() {
    parents_idx = 0;
    current_node = *parents[0];
  }

  node* node_ptr() {
    assert(current_node == *parents[parents_idx]);
    return current_node;
  }

  node** incoming_link() {
    return parents[parents_idx];
  }

  void move_up() {
    assert(parents_idx > 0);
    --parents_idx;
    current_node = *parents[parents_idx];
  }

  // Moves one level up and returns idx of node_ptr() in
  // parent's childs array.
  int move_up_return_idx() {
    node* parent = *parents[parents_idx-1];
    int rv = (parents[parents_idx] == &parent->childs[0]) ? 0 : 1;
    move_up();
    return rv;
  }

  node *move_down(int idx) {
    assert(parents_idx < kMaxLevels);
    node *n = node_ptr();
    node **pp = parents[++parents_idx] = &n->childs[idx];
    current_node = *pp;
    return *pp;
  }

  // aka left rotation (or right when idx is 0)
  //
  //    2            1
  // -1   1   =>  2    0 b == 1 => balances = 0 & height reduced
  //    -b 0    -1 -b
  bool swap_child(int idx);

  //
  // before:
  // parent -> n
  //  n -> n1, n2
  //   n2 -> n21, n22
  //    n21 -> n211, n212

  // after:
  //  parent -> n21 -> n, n2
  //   n -> n1, n211
  //   n2 -> n212, n22

  //     3                       1
  //  0          2     =>    3       2
  // - -     1      0      0 -b1  -b2 0
  //     -b1  -b2
  void swap_grand_child(int idx);

  bool balance_locally(int balance, bool just_inserted) {
    int idx = (balance > 1);
    node *n = node_ptr();
    node *child = n->childs[idx];

    // if node is out of balance because of child and we've
    // inserted, we must have inserted under child and just grown
    // child's height, so it cannot be the case that both child's
    // childs have same height
    assert(!just_inserted || balance_towards(child, idx) != 0);

    if (balance_towards(child, idx) >= 0) {
      bool rv = swap_child(idx);
      assert(!just_inserted || rv);
      return rv;
    }
    swap_grand_child(idx);
    return true;
  }

  void erase_at_leaf() {
    *incoming_link() = nullptr;
    if (is_at_root()) {
      return;
    }
    int n_idx = move_up_return_idx();
    int balance_delta = (n_idx == 0) ? 1 : -1;
    node* parent = node_ptr();
    assert_local_balance(parent);

    // if height didn't decrease, we can stop balancing
    if (parent->balance == 0) {
      parent->balance = balance_delta;
      return;
    }

    parent->balance += balance_delta;

    bool need_balance = (parent->balance != 0);
    do {
      if (need_balance) {
        bool reduced = balance_locally(parent->balance, false);
        if (!reduced) {
          return;
        }
        if (is_at_root()) {
          return;
        }
        need_balance = false;
        parent = node_ptr();
      }
      if (is_at_root()) {
        return;
      }
      int child_idx = move_up_return_idx();
      parent = node_ptr();

      balance_delta = (child_idx == 0) ? 1 : -1;
      parent->balance += balance_delta;
      if (is_imbalanced(parent->balance)) {
        need_balance = true;
      } else if (parent->balance) {
        return;
      }
    } while (true);
  }

  void erase_current() {
    node *n = node_ptr();
    assert_local_balance(n);

    int closest_idx;
    switch (n->balance) {
    case 0:
      // When n is leaf, we just run erase_at_leaf
      // to fix balance and we're done
      if (n->childs[1] == nullptr) {
        assert(n->childs[0] == nullptr);
        erase_at_leaf();
        return;
      }
      // FALLTHROUGH
    case -1:
      closest_idx = 0;
      break;
    case 1:
      closest_idx = 1;
      break;
    }

    // But when n is not leaf, we're swapping current node
    // with one of it's descendants that is closest to it
    // by tree order (temporarily breaking order) and then
    // recursively erase from that new spot (which is
    // likely to be leaf).
    int other_idx = closest_idx ^ 1;

    int nth_depth = parents_idx;

    assert(n->childs[closest_idx]);
    node *p = move_down(closest_idx);
    while (node *q = p->childs[other_idx]) {
      p = q;
      move_down(other_idx); // p is now current
    }
    assert(p != n);

    node n_copy = *n;
    n->childs[0] = p->childs[0];
    n->childs[1] = p->childs[1];
    n->balance = p->balance;
    p->childs[0] = n_copy.childs[0];
    p->childs[1] = n_copy.childs[1];
    p->balance = n_copy.balance;

    parents[nth_depth+1] = &p->childs[closest_idx];
    *(parents[parents_idx]) = n;
    *(parents[nth_depth]) = p;
    current_node = *parents[parents_idx];

    erase_current();
  }

  void insert_leaf(node *new_leaf, int idx) {
    new_leaf->childs[0] = new_leaf->childs[1] = nullptr;
    new_leaf->balance = 0;

    node *ex_leaf = node_ptr();

    if (ex_leaf == nullptr) {
      // TODO: consider asserting instead given idx
      // is bogus in this case.

      // first node in tree
      *incoming_link() = new_leaf;
      return;
    }

    assert_local_balance(ex_leaf);
    assert(ex_leaf->childs[idx] == nullptr);
    ex_leaf->childs[idx] = new_leaf;

    while (true) {
      int balance_delta = idx ? 1 : -1;
      ex_leaf->balance += balance_delta;
      if (is_imbalanced(ex_leaf->balance)) {
        balance_locally(ex_leaf->balance, true);
        return;
      }
      if (!ex_leaf->balance) {
        return; // did not increase height
      }
      if (is_at_root()) {
        return; // total height increased
      }
      idx = move_up_return_idx();
      ex_leaf = node_ptr();
    }
  }
};

bool tree_path::swap_child(int idx) {
  int other_idx = idx ^ 1;
  node *n = node_ptr();
  node **link = incoming_link();

  node *child = n->childs[idx];
  node *child_child = child->childs[other_idx];

  assert(balance_towards(n, idx) == 2);
  assert(balance_towards(child, idx) >= 0);
  assert_local_balance(child);

  bool reduce = balance_towards(child, idx) == 1;

  //    2            1
  // -1   1   =>  2    0 b == 1 => balances = 0 & height reduced
  //    -b 0    -1 -b

  if (reduce) {
    n->balance = 0;
    child->balance = 0;
  } else {
    n->balance = idx ? 1 : -1;
    child->balance = idx ? -1 : 1;
  }

  *link = child; // replaces node_ptr() result
  n->childs[idx] = child_child;
  child->childs[other_idx] = n;

  return reduce;
}

void tree_path::swap_grand_child(int idx) {
  int other_idx = idx ^ 1;
  node *n = node_ptr();
  node **link = incoming_link();

  // node *n1 = n->childs[other_idx];
  node *n2 = n->childs[idx];
  node *n21 = n2->childs[other_idx];
  // node *n22 = n2->childs[idx];
  node *n211 = n21->childs[other_idx];
  node *n212 = n21->childs[idx];

  // before:
  // parent -> n
  // n -> n1, n2
  // n2 -> n21, n22
  // n21 -> n211, n212

  // after:
  // parent -> n21 -> n, n2
  // n -> n1, n211
  // n2 -> n212, n22

  //     3                       1
  //  0          2     =>    3       2
  // - -     1      0      0 -b1  -b2 0
  //     -b1  -b2

  assert(balance_towards(n, idx) == 2);
  assert(balance_towards(n2, idx) < 0);

  int b = balance_towards(n21, idx);

  n21->balance = 0;
  if (b == 0) {
    n->balance = 0;
    n2->balance = 0;
  } else if (b > 0) {
    n->balance = idx ? -1 : 1;
    n2->balance = 0;
  } else {
    n->balance = 0;
    n2->balance = idx ? 1 : -1;
  }

  *link = n21; // replaces value of node_ptr()
  n21->childs[other_idx] = n;
  n21->childs[idx] = n2;
  n->childs[idx] = n211;
  n2->childs[other_idx] = n212;
}

}  // namespace avl

struct Node : public avl::node {
  const std::string_view data;

#if USE_LOCAL_DATA_PREFIX
  char local_data_prefix[16];
#endif

  Node(std::string_view data) : data(data) {
    this->childs[0] = this->childs[1] = nullptr;
    this->balance = 0;
#if USE_LOCAL_DATA_PREFIX
    memset(local_data_prefix, 0, sizeof(local_data_prefix));
    memcpy(local_data_prefix, data.data(), std::min(data.size(), sizeof(local_data_prefix)));
#endif
  }

  bool LessThan(const Node* other) const {
#if USE_LOCAL_DATA_PREFIX
    int c = memcmp(local_data_prefix, other->local_data_prefix, sizeof(local_data_prefix));
    if (c != 0) {
      return (c < 0);
    } else {
      return data < other->data;
    }
#else
    return data < other->data;
#endif
  }

  const Node* GetLeft() const {
    return static_cast<const Node*>(childs[0]);
  }
  const Node* GetRight() const {
    return static_cast<const Node*>(childs[1]);
  }
  const Node* GetLeft() {
    return static_cast<Node*>(childs[0]);
  }
  const Node* GetRight() {
    return static_cast<Node*>(childs[1]);
  }

  ~Node() {
    delete GetLeft();
    delete GetRight();
  }
};

using Tree = std::unique_ptr<Node>;

void Insert(Tree* tree, std::string_view data) {
  if (!*tree) {
    *tree = std::make_unique<Node>(data);
    return;
  }

  Node* new_node = new Node(data);

  avl::node* root = tree->release();
  avl::tree_path path{&root};

  avl::node* node = root;
  for (;;) {
    if (static_cast<Node*>(node)->LessThan(new_node)) {
      node = path.move_down(1);
    } else {
      node = path.move_down(0);
    }

    if (!node) {
      break;
    }
  }

  int idx = path.move_up_return_idx();
  path.insert_leaf(new_node, idx);

  tree->reset(static_cast<Node*>(root));
}

// We find smallest node that is >= than given string, or nullptr if
// everything is smaller than str.
const Node* LowerBound(const Node* root, std::string_view str) {
  const Node* best = (root && root->data >= str) ? root : nullptr;
  while (root) {
    if (root->data < str) {
      root = root->GetRight();
    } else {
      best = root;
      root = root->GetLeft();
    }
  }
  return best;
}

void Validate(const Node* node) {
  struct Checker {
    std::optional<std::string_view> prev_seen;

    // Checking node's subtree returns it's height
    int Rec(const Node* node) {
      if (!node) {
        return 0;
      }

      int left_height = Rec(node->GetLeft());

      // In addition to checking balance we ensure that in-order tree
      // traversal sees nodes in increasing order.
      if (prev_seen.has_value()) {
        assert(node->data >= *prev_seen);
        if (node->data < *prev_seen) {
          abort();
        }
      }
      prev_seen = node->data;

      int right_height = Rec(node->GetRight());

      int actual_balance = right_height - left_height;
      int stored_balance = node->balance;
      assert(stored_balance == actual_balance);
      if (actual_balance != stored_balance) { abort(); }

      return std::max(left_height, right_height) + 1;
    }
  };

  Checker{}.Rec(node);
}

int main(int argc, char** argv) {
  // NOTE, we want this to be destroyed after heap sample dump we
  // setup just below.
  Tree locations;

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  AtomicFlag stop_req;
  auto sigint_cleanup = SignalHelper::OnSIGINT(&stop_req);

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    Insert(&locations, std::string_view{s}.substr(pos));
    if (stop_req) {
      fprintf(stderr, "interrupted insertions by seeing SIGINT\n");
      break;
    }
  }

#ifndef NDEBUG
  Validate(locations.get());
#endif

  auto it = LowerBound(locations.get(), "the Roman Empire");
  if (!it) {
    printf("failed to find lower bound\n");
    abort();
  }
  assert(it != nullptr);

  size_t off = it->data.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
