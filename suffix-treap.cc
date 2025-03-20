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

  size_t priority;

  // Some trivial RNG code "stolen" from gperftools.
  static constexpr uint64_t NextRandom(uint64_t rnd) {
    uint64_t prng_mult = 0x5DEECE66DULL;
    uint64_t prng_add = 0xB;
    uint64_t prng_mod_power = 48;
    uint64_t prng_mod_mask =
      ~((~static_cast<uint64_t>(0)) << prng_mod_power);
    return (prng_mult * rnd + prng_add) & prng_mod_mask;
  }

  static inline uint64_t rng = NextRandom(NextRandom(NextRandom(0xbeefcafe)));

  explicit Node(std::string_view value) : value(value), left{}, right{} {
    rng = NextRandom(rng);
    priority = rng;
  }
};

struct Treap {
  Node* root = nullptr;

  ~Treap() {
    Clear();
  }

  void Insert(std::string_view value) {
    // Note, we assume that the value doesn't exist in the tree. Which
    // is the case for our suffix-map application. It won't be hard to
    // handle this, but we keep things simple.
    Node* new_node = new Node(value);
    size_t priority = new_node->priority;

    struct Split {
      // Splits given search tree root into one tree with elements
      // less than `value' and another tree with elements larger than
      // `value'. "less than tree" is placed into *place_left and
      // another tree is placed into *place_right.
      //
      // Yes, this is tail-recursive, so decent compilers turn this
      // into plain straightforward loop.
      static void Rec(std::string_view value, Node* node,
                      Node** place_left, Node** place_right) {
        if (!node) {
          *place_left = nullptr;
          *place_right = nullptr;
          return;
        }

        // We silently assume node->value == value won't happen. It
        // won't be hard to handle this, but our toy use-case doesn't
        // need it.
        if (node->value < value) {
          *place_left = node;
          Rec(value, node->right, &node->right, place_right);
        } else {
          *place_right = node;
          Rec(value, node->left, place_left, &node->left);
        }
      }
    };

    Node** parent_place = &root;
    Node* node = root;

    while (node) {
      if (node->priority > priority) {
        Split::Rec(value, node, &new_node->left, &new_node->right);
        break;
      }

      if (node->value < value) {
        parent_place = &node->right;
      } else {
        parent_place = &node->left;
      }
      node = *parent_place;
    }

    *parent_place = new_node;
  }


  // We find smallest node that is >= than given string, or nullptr if
  // everything is smaller than str.
  const Node* LowerBound(std::string_view str) {
    const Node* node = root;

    const Node* best = (node && node->value >= str) ? node : nullptr;
    while (node) {
      if (node->value < str) {
        node = node->right;
      } else {
        best = node;
        node = node->left;
      }
    }
    return best;
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

        assert(!node->left || node->left->priority > node->priority);
        assert(!node->right || node->right->priority > node->priority);

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
    // Note, recursion is potentially unsafe here, because of
    // unpredictable maximum depth. But this code is not pretending to
    // be production, so lets keep it uncomplicated.
    struct Deleter {
      size_t total_deleted = 0;
      void Rec(Node* n) {
        if (n == nullptr) {
          return;
        }
        Rec(n->left);
        Node* r = n->right;
        delete n;
        total_deleted++;
        Rec(r);
      }
    };

    Deleter d;
    d.Rec(root);
    root = nullptr;
#ifndef NDEBUG
    printf("total_deleted: %zu\n", d.total_deleted);
#endif
    (void)d.total_deleted;
  }
};

int main(int argc, char** argv) {
  Treap locations; // Note, we want this destructor to run after we've
                   // dumped heap sample

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

  const Node* it = locations.LowerBound("the Roman Empire");
  assert(it);

  size_t off = it->value.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
