// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// #undef NDEBUG
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <assert.h>
#include <stdio.h>

#include "demo-helper.h"

struct Node {
  const std::string_view value;

  Node* left;
  Node* right;

  explicit Node(std::string_view value) : value(value), left{}, right{} {}
};

struct SplayTree {
  Node* root = nullptr;

  ~SplayTree() {
    Clear();
  }

  void InsertMoveToTop(std::string_view value) {
    Node* node = new Node(value);

    struct Split {
      // Splits given search tree root into one tree with elements
      // less than `value' and another tree with elements larger than
      // `value'. "less than tree" is placed into *place_left and
      // another tree is placed into *place_right.
      //
      // Yes, this is tail-recursive, so decent compilers turn this
      // into plain straightforward loop.
      //
      // Note, this is not splaying as it lacks handling of "zig-zig"
      // case which is crucial for reaching amortized O(log N)
      // bound. See below for actual splay-ful insert routine.
      static void Rec(std::string_view value, Node* node,
                      Node** place_left, Node** place_right) {
        if (!node) {
          *place_left = nullptr;
          *place_right = nullptr;
          return;
        }

        if (node->value < value) {
          *place_left = node;
          Rec(value, node->right, &node->right, place_right);
        } else {
          *place_right = node;
          Rec(value, node->left, place_left, &node->left);
        }
      }
    };

    Split::Rec(value, root, &node->left, &node->right);
    root = node;
  }

  // This is trivial unbalanced "insert at the bottom" routine.
  void NonSplayUnbalancedInsert(std::string_view value) {
    Node** parent_place = &root;
    Node* node = root;
    while (node) {
      if (node->value < value) {
        parent_place = &node->right;
      } else {
        parent_place = &node->left;
      }
      node = *parent_place;
    }
    *parent_place = new Node(value);
  }

  // SplitOp is the combination of split and splay. For basic idea
  // look at InsertMoveToTop above. What we have here in addition to
  // regular split is zig-zig case handling which turns it into
  // top-down splay operation. In order to detect zig-zig cases we
  // need to consider two layers of the tree at once. And then
  // avoiding duplicate comparisons pushes us into less then simplest
  // code.
  //
  // We still rely on ~all decent compilers optimizing out tail-calls
  // (and inlining Go{Left,Right} helpers below). And even with that
  // the generated code still doesn't look entirely perfect to me. So
  // production version of this code would likely need manual
  // transformation into regular loop. I didn't do it here to keep
  // closer resemblance of the "move-to-top" Split above.
  struct SplitOp {
    static void Rec(std::string_view value, bool value_is_less,
                    Node* root, Node** place_left, Node** place_right) {
      // We silently assume root->value == value won't happen. It
      // won't be hard to handle this, but our toy use-case doesn't
      // need it.
      if (value_is_less) {
        // value "belongs to" left subtree
        Node* l = root->left;
        if (l) {
          if (value < l->value) {
            // "double left" case. This is zig-zig op. So we first
            // "pull" l to be higher than root and then split at l.
            root->left = l->right;
            l->right = root;
            root = l;
            l = l->left;
          } else {
            // We want to descend the left subtree, and we know it's
            // root is greater than value (we just compared above). So
            // lets avoid doing this comparison second time.
            return GoLeft<true, false>(value, root, l, place_left, place_right);
          }
        }
        return GoLeft<false, false>(value, root, l, place_left, place_right);
      } else {
        // "Right" case is mirror of left.
        Node* r = root->right;
        if (r) {
          if (value > r->value) {
            root->right = r->left;
            r->left = root;
            root = r;
            r = r->right;
          } else {
            return GoRight<true, true>(value, root, r, place_left, place_right);
          }
        }
        return GoRight<false, false>(value, root, r, place_left, place_right);
      }
    }

    template <bool comparison_known, bool value_is_less>
    static void GoLeft(std::string_view value, Node* root, Node* l,
                       Node** place_left, Node** place_right) {
      *place_right = root;
      if (!comparison_known && !l) {
        *place_left = root->left = nullptr;
        return;
      }
      bool v_is_less = comparison_known ? value_is_less : (value < l->value);
      Rec(value, v_is_less, l, place_left, &root->left);
    }
    template <bool comparison_known, bool value_is_less>
    static void GoRight(std::string_view value, Node* root, Node* r,
                        Node** place_left, Node** place_right) {
      *place_left = root;
      if (!comparison_known && !r) {
        root->right = *place_right = nullptr;
        return;
      }
      bool v_is_less = comparison_known ? value_is_less : (value < r->value);
      Rec(value, v_is_less, r, &root->right, place_right);
    }
  };

  void Insert(std::string_view value) {
    Node* node = new Node(value);
    if (root) {
      SplitOp::Rec(value, (value < root->value), root, &node->left, &node->right);
    }
    root = node;
  }

  // We find smallest node that is >= than given string, or nullptr if
  // everything is smaller than str.
  //
  // We choose to make LowerBound move-to-top the node we're looking
  // for up. So returned value is new root (unless it is
  // nullptr).
  //
  // Also note that we're not properly splaying here to keep things
  // simpler (see Insert vs InsertMoveToTop above). We only use lower
  // bound a handful of times in this toy use-case, so we can keep it
  // simpler.
  const Node* LowerBound(std::string_view str) {
    struct Split {
      static void Rec(std::string_view str, Node* root,
                      Node** place_left, Node** place_right,
                      Node*** place_lower_bound) {
        if (!root) {
          *place_left = *place_right = nullptr;
          return;
        }

        if (root->value < str) {
          // root (and therefore it's left subtree) are smaller than
          // our bound. So it goes left. And we continue splitting
          // right sub-tree (conceptually tearing it "off" root,
          // first).
          *place_left = root;
          Rec(str, root->right, &root->right, place_right, place_lower_bound);
        } else {
          *place_right = root;
          *place_lower_bound = place_right;
          Rec(str, root->left, place_left, &root->left, place_lower_bound);
        }
      }
    };

    Node** place_lower_bound = &root;
    Node* left;
    Node* right;

    Split::Rec(str, root, &left, &right, &place_lower_bound);

    if (place_lower_bound == &root) {
      assert(left == root);
      assert(right == nullptr);
      if (root && root->value >= str) {
        return root;
      }
      return nullptr;
    }

    Node* new_root = *place_lower_bound;
    assert(new_root->left == nullptr);
    *place_lower_bound = new_root->right;
    new_root->left = left;
    new_root->right = right;

    root = new_root;
    return root;
  }

  void RemoveRoot() {
    if (!root) {
      return;
    }

    // We could choose different approaches here, but we ended up
    // choosing to alternate left/right subtrees when choosing joining
    // "candidates". Intuitively, it feels like "better" option to
    // avoid longest O(N) chains, but I have no idea if it really
    // works better.
    //
    // Notably, this is different than what proper splay tree does. It
    // picks either rightmost node of the left subtree of leftmost
    // node of the right subtree, removes it and places it on top
    // instead of our root node. It seems like it would take
    // approximately same amount of work. Or maybe even end up
    // touching fewer nodes, so maybe I should've done it instead.
    struct Join {
      static void RecLeft(Node** place_root, Node* left, Node* right) {
        if (!left) {
          *place_root = right;
          return;
        }
        *place_root = left;
        RecRight(&left->right, left->right, right);
      }
      static void RecRight(Node** place_root, Node* left, Node* right) {
        if (!right) {
          *place_root = left;
          return;
        }
        *place_root = right;
        RecLeft(&right->left, left, right->left);
      }
    };

    Node* old_root = root;
    Join::RecLeft(&root, root->left, root->right);
    delete old_root;
  }

  void Validate(bool print_stats) {
    struct Checker {
      std::optional<std::string_view> prev_seen;
      size_t total_height = 0;
      size_t node_count = 0;

      int Rec(const Node* node, int depth) {
        if (!node) {
          return 0;
        }

        total_height += depth;
        node_count += 1;
        int left_height = Rec(node->left, depth + 1);

        if (prev_seen.has_value()) {
          assert(node->value > *prev_seen);
          if (node->value <= *prev_seen) {
            abort();
          }
        }
        prev_seen = node->value;

        int right_height = Rec(node->right, depth + 1);

        return std::max<int>(left_height, right_height) + 1;
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
  }

  void Clear() {
    size_t total_deleted = 0;
    Node* n = root;
    Node* p = nullptr;

    for (;;) {
      Node* next;
      if (!n) {
        // when current node is absent, then we go back "up" to
        // parent.
        if (!p) {
          break;
        }

        n = p;
        // grandparent is at parent's `left' link
        p = n->left;
        next = n->right;

        delete n;
        total_deleted++;
      } else {
        // before we're able to drop current node, we need to handle
        // it's left subtree. And remember our parent, we set our left
        // link to our parent.
        next = n->left;
        n->left = p;
        p = n;
      }

      n = next;
    }

    root = nullptr;
#ifndef NDEBUG
    printf("total_deleted: %zu\n", total_deleted);
#endif
    (void)total_deleted;
  }
};

void MaybeSetupInsertOp(int* argc, char*** argv,
                        void (SplayTree::** insert_op_var)(std::string_view)) {
  static constexpr std::string_view kInsertOp = "--insert-op=";
  std::string_view arg = (*argv)[1];
  if (!arg.starts_with(kInsertOp)) {
    return;
  }

  (*argv)++;
  (*argc)--;

  arg = arg.substr(kInsertOp.size());
  if (arg == "splay") {
    *insert_op_var = &SplayTree::Insert;
  } else if (arg == "move-to-top") {
    *insert_op_var = &SplayTree::InsertMoveToTop;
  } else if (arg == "naive") {
    *insert_op_var = &SplayTree::NonSplayUnbalancedInsert;
  } else {
    fprintf(stderr, "--insert-op can be one of the splay, classic, move-to-top or naive\n");
    exit(1);
  }
}

int main(int argc, char** argv) {
  void (SplayTree::* insert_op)(std::string_view) = &SplayTree::Insert;
  if (argc > 1) {
    MaybeSetupInsertOp(&argc, &argv, &insert_op);
  }

  SplayTree locations; // Note, we want this destructor to run after
                       // we've dumped heap sample

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  AtomicFlag stop_req;
  auto sigint_cleanup = SignalHelper::OnSIGINT(&stop_req);

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    (locations.*insert_op)(std::string_view{s}.substr(pos));
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

  static constexpr std::string_view kSearchString = "the Roman Empire";

  const Node* it = locations.LowerBound(kSearchString);
  assert(it);

  while (it) {
    size_t off = it->value.data() - s.data();
    printf("off = %zu\n", off);

    printf("context occurrence of '%.*s':\n", (int)kSearchString.size(), kSearchString.data());
    PrintOccurenceContext(s, off);

    assert(locations.root == it);
    locations.RemoveRoot();

    it = locations.LowerBound(kSearchString);
    assert(it->value.data() >= kSearchString);

    if (it && !it->value.starts_with(kSearchString)) {
      it = nullptr;
    }
  }

#ifndef NDEBUG
  locations.Validate(true);
#endif
}
