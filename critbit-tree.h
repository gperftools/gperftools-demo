// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#ifndef CRITBIT_TREE_H_
#define CRITBIT_TREE_H_

// C Standard Libraries
#include <assert.h>  // For assert (used in validation failure)
#include <stddef.h>  // For size_t
#include <stdint.h>  // For fixed-width integers (if needed later)
#include <stdio.h>   // For printf (used in validation failure)
#include <stdlib.h>  // For abort (used in validation failure)

// C++ Standard Libraries
#include <algorithm>  // For std::mismatch, std::min
#include <bit>        // For std::countl_zero
#include <memory>     // For std::unique_ptr, std::make_unique
#include <optional>   // For std::optional
#include <string_view>// For std::string_view
#include <utility>    // For std::move, std::pair
#include <variant>    // For std::variant
#include <vector>     // For path storage

// --- Node Definitions ---

// Forward declaration for use in NodeVariant
struct InternalNode;

/**
 * @brief Represents a leaf node in the CritBitTree, storing the user's key.
 * This node terminates a branch in the tree.
 */
struct ExternalNode {
  /** The key associated with this leaf. Points to externally managed memory. */
  std::string_view key_;
};

/**
 * @brief A type alias for the variant used to represent links between nodes.
 *
 * Each link (root or child pointer) holds this variant. The variant contains a
 * std::unique_ptr that owns either an ExternalNode (leaf) or an InternalNode
 * (branching point). This structure provides memory efficiency.
 */
using NodeVariant =
    std::variant<std::unique_ptr<ExternalNode>, std::unique_ptr<InternalNode>>;

/**
 * @brief Represents an internal branching node in the CritBitTree.
 *
 * Internal nodes direct searches based on a specific bit (`critbit_index_`)
 * where the keys in its left and right subtrees are known to first differ.
 * They hold links to their two children but do not store keys directly.
 */
struct InternalNode {
  /** The 0-based index of the critical bit used for branching at this node. */
  size_t critbit_index_;
  /**
   * Links to the two child nodes (index 0 for bit value 0, index 1 for bit value 1).
   * In a valid tree constructed via Insert, these are assumed to always hold
   * non-null unique_ptrs after the node's creation.
   */
  NodeVariant children_[2];
};


// --- Detail Namespace (Helper Functions) ---
namespace detail {

/**
 * @brief Safely retrieves the value (0 or 1) of a specific bit from a string_view.
 *
 * Bits are indexed starting from 0 for the most significant bit (MSB) of the
 * first byte. If the requested bit index is beyond the string's length, it's
 * treated as 0 (as if the string were padded with zero bytes).
 *
 * @param sv The string_view to inspect.
 * @param bit_index The 0-based index of the bit to retrieve.
 * @return The bit value (0 or 1).
 */
inline int get_bit(std::string_view sv, size_t bit_index) noexcept {
  size_t byte_index = bit_index / 8;
  if (byte_index >= sv.length()) { return 0; } // Out-of-bounds reads as 0
  size_t bit_offset = 7 - (bit_index % 8); // 0=MSB, 7=LSB within the byte
  return (static_cast<unsigned char>(sv[byte_index]) >> bit_offset) & 1;
}

/**
 * @brief Finds the 0-based index of the first bit where two string_views differ.
 *
 * Assumes strings do not contain null bytes ('\0') as part of their significant
 * content. Handles cases where one string is a prefix of the other by comparing
 * the first byte after the prefix in the longer string against an implicit zero byte.
 *
 * @param s1 The first string_view.
 * @param s2 The second string_view.
 * @return std::optional containing the index of the first differing bit,
 * or std::nullopt if the strings are identical.
 */
inline std::optional<size_t> find_crit_bit(std::string_view s1,
                                           std::string_view s2) noexcept {
  const size_t len1 = s1.length();
  const size_t len2 = s2.length();
  const size_t min_len_bytes = std::min(len1, len2);

  // Find the first differing characters using std::mismatch.
  auto [p1, p2] = std::mismatch(s1.begin(), s1.begin() + min_len_bytes, s2.begin());

  size_t diff_byte_index;
  unsigned char c1, c2; // Bytes involved in the first difference

  if (p1 == s1.begin() + min_len_bytes) {
    // --- Case 1: One string is a prefix of the other (or identical) ---
    if (len1 == len2) {
      return std::nullopt; // Identical
    }

    // Difference occurs right after the common prefix (at index min_len_bytes).
    diff_byte_index = min_len_bytes;
    // Compare the byte in the longer string against an implicit zero byte.
    if (len1 > len2) { // s2 is prefix of s1
      c1 = static_cast<unsigned char>(s1[diff_byte_index]);
      c2 = 0;
    } else { // s1 is prefix of s2
      c1 = 0;
      c2 = static_cast<unsigned char>(s2[diff_byte_index]);
    }
    // Assumption: The byte from the longer string (c1 or c2) is not '\0'.
  } else {
    // --- Case 2: Difference found within the common prefix length ---
    diff_byte_index = std::distance(s1.begin(), p1);
    c1 = static_cast<unsigned char>(*p1);
    c2 = static_cast<unsigned char>(*p2);
  }

  // --- Calculate bit index from differing bytes ---
  unsigned char diff_bits = c1 ^ c2; // XOR reveals differing bits. Must be non-zero.

  // Find the index (0-7 from MSB) of the most significant differing bit.
#if defined(__cpp_lib_bitops)
  int bit_index_in_byte = std::countl_zero(diff_bits); // Efficient C++20 way
#else
  // Portable fallback using manual check
  int bit_index_in_byte = 0;
  unsigned char mask = 0x80; // Start with MSB mask
  while (!(diff_bits & mask)) {
    mask >>= 1;
    bit_index_in_byte++;
  }
#endif

  // Combine byte index and within-byte bit index for the final result.
  return diff_byte_index * 8 + bit_index_in_byte;
}

} // namespace detail


// --- CritBitTree Class ---

/**
 * @brief A Crit-Bit Tree implementation storing non-owning std::string_view keys.
 *
 * This data structure provides efficient insertion (O(key_length)) and
 * lower_bound search (O(key_length)) operations. It's suitable for scenarios
 * like dictionaries or prefix sets where the string data is managed externally.
 *
 * @invariant Assumes inserted keys do not contain null bytes ('\0').
 * @invariant Internal nodes always have two non-null children after construction.
 * @invariant Internal node crit-bit indices correctly partition the key space
 * based on the first differing bit between their subtrees.
 */
class CritBitTree {
private:
  /**
   * @brief Stores path information during tree traversals, enabling efficient
   * insertion restructuring and successor finding in LowerBound.
   */
  struct PathElement {
    /** Pointer to the parent InternalNode descended from. */
    InternalNode* parent_node_ptr_ = nullptr;
    /** Direction (0 or 1) taken from the parent to reach the next node. */
    int direction_taken_ = -1;
  };

  /** @brief Alias for the return type {representative_key, common_prefix_len_bits}
   * used by the recursive validation helper. */
  using ValidationInfo = std::pair<std::string_view, size_t>;

  /** @brief The root of the tree, stored as an optional NodeVariant. */
  std::optional<NodeVariant> root_ = std::nullopt;

  /** @brief A hint for reserving path vector capacity to potentially avoid reallocations. */
  static constexpr size_t kMaxExpectedPathDepth = 1024;

public:
  /** @brief Constructs an empty CritBitTree. */
  CritBitTree() = default;

  /** @brief Destructor. Node memory is automatically managed by std::unique_ptr. */
  ~CritBitTree() = default;

  // --- Rule of 5/6: Copy operations are deleted, move operations are defaulted ---
  CritBitTree(const CritBitTree&) = delete;
  CritBitTree& operator=(const CritBitTree&) = delete;
  CritBitTree(CritBitTree&&) noexcept = default;
  CritBitTree& operator=(CritBitTree&&) noexcept = default;

  /**
   * @brief Inserts a key into the tree. If the key already exists, this operation
   * has no effect.
   *
   * @param key The string_view key to insert. It must remain valid for the
   * lifetime of the tree and should not contain null bytes.
   */
  void Insert(std::string_view key) {
    // 1. Handle Empty Tree: Insert the first node as the root leaf.
    if (!root_) {
      root_.emplace(std::make_unique<ExternalNode>(ExternalNode{key}));
      return;
    }

    // 2. Descent and Path Building: Traverse down according to key bits.
    std::vector<PathElement> path;
    path.reserve(kMaxExpectedPathDepth); // Pre-allocate path storage
    NodeVariant* current_variant_ptr = &(*root_);

    // Loop while the current node is internal.
    while (std::holds_alternative<std::unique_ptr<InternalNode>>(*current_variant_ptr)) {
      auto& internal_node_ptr = std::get<std::unique_ptr<InternalNode>>(*current_variant_ptr);
      InternalNode* internal_node = internal_node_ptr.get(); // Assume non-null

      // Determine direction and store path element before descending.
      int direction = detail::get_bit(key, internal_node->critbit_index_);
      path.push_back({internal_node, direction});
      // Move to the child variant slot. Assumed valid based on invariants.
      current_variant_ptr = &internal_node->children_[direction];
    }

    // 3. Handle Existing Leaf: Descent ended, must be at an ExternalNode variant.
    auto& existing_leaf_ptr = std::get<std::unique_ptr<ExternalNode>>(*current_variant_ptr);
    std::string_view existing_key = existing_leaf_ptr->key_;

    // 4. Calculate Critical Bit: Find the first difference with the existing key.
    std::optional<size_t> critbit_opt = detail::find_crit_bit(key, existing_key);

    // Case 1: Keys are Identical - Insertion has no effect.
    if (!critbit_opt) { return; }

    // Case 2: Keys Differ - Restructuring is required.
    size_t new_critbit_index = *critbit_opt;
    auto new_leaf_ptr = std::make_unique<ExternalNode>(ExternalNode{key});
    int new_key_bit = detail::get_bit(key, new_critbit_index);

    // 5. Walk Up Path Stack: Find where the new internal node should be inserted.
    //    It replaces the child pointer *below* the highest ancestor whose
    //    critbit is less than the new critbit.
    NodeVariant* insert_pos_ptr = &(*root_); // Default if new node is child of root
    for (int i = path.size() - 1; i >= 0; --i) {
      InternalNode* parent_node_ptr = path[i].parent_node_ptr_; // Direct pointer from path
      int direction_taken = path[i].direction_taken_;

      if (parent_node_ptr->critbit_index_ < new_critbit_index) {
        // Found the correct ancestor. The insertion point is the variant slot
        // for the child we originally descended into from this parent.
        insert_pos_ptr = &parent_node_ptr->children_[direction_taken];
        break; // Stop walking up
      }
    }

    // 6. Restructure: Insert the new internal node, adjusting children.
    { // Scope limits lifetime of temporary moved variant
      // Move the variant at the insertion point (containing the existing subtree)
      // out of the tree using std::move on the variant itself.
      NodeVariant existing_node_variant_moved = std::move(*insert_pos_ptr);

      // Create the new internal node representing the split.
      auto new_internal_node = std::make_unique<InternalNode>();
      new_internal_node->critbit_index_ = new_critbit_index;

      // Assign the new leaf and the moved subtree to the correct children (0 or 1)
      // based on the bit value of the *new* key at the critical bit index.
      if (new_key_bit == 0) { // New key's path goes left (0)
        new_internal_node->children_[0] = std::move(new_leaf_ptr);
        new_internal_node->children_[1] = std::move(existing_node_variant_moved);
      } else { // New key's path goes right (1)
        new_internal_node->children_[0] = std::move(existing_node_variant_moved);
        new_internal_node->children_[1] = std::move(new_leaf_ptr);
      }
      // Place the new internal node (owning its children) into the tree structure.
      *insert_pos_ptr = std::move(new_internal_node);
    }
  }

  /**
   * @brief Finds the first element (leaf node) in the tree whose key is not
   * less than (is greater than or equal to) the given key.
   *
   * @param key The key to search for.
   * @return A pointer to the `std::string_view` of the found key within the tree,
   * or `nullptr` if no key in the tree is >= the search key. The returned pointer
   * remains valid only as long as the tree structure is not modified.
   */
  [[nodiscard]] const std::string_view* LowerBound(std::string_view key, bool really_upper = false) const {
    if (!root_) return nullptr; // Empty tree

    std::vector<PathElement> path;
    path.reserve(kMaxExpectedPathDepth); // Reserve capacity
    const NodeVariant* current_variant = &(*root_); // Start at root

    // Descend while at internal nodes, tracking the path.
    while (std::holds_alternative<std::unique_ptr<InternalNode>>(*current_variant)) {
      const auto& internal_node_ptr = std::get<std::unique_ptr<InternalNode>>(*current_variant);
      const InternalNode* internal_node = internal_node_ptr.get(); // Const raw pointer

      int direction = detail::get_bit(key, internal_node->critbit_index_);
      // Store path element. const_cast is needed as PathElement stores non-const InternalNode*,
      // but this const method will not modify the tree through it.
      path.push_back({const_cast<InternalNode*>(internal_node), direction});
      current_variant = &internal_node->children_[direction]; // Descend (assumed valid)
    }

    // Must have reached the ExternalNode variant containing the closest leaf.
    const auto& found_leaf_ptr = std::get<std::unique_ptr<ExternalNode>>(*current_variant);
    const ExternalNode& found_leaf = *found_leaf_ptr; // Assume non-null

    std::optional<size_t> critbit_opt = detail::find_crit_bit(key, found_leaf.key_);

    size_t critbit;
    int key_bit;

    if (!critbit_opt) {
      if (!really_upper) {
        return &found_leaf.key_;
      }
      critbit = ~size_t{0};
      key_bit = 1;
    } else {
      critbit = *critbit_opt;
      key_bit = detail::get_bit(key, critbit);
    }

    while (!path.empty() && path.rbegin()->parent_node_ptr_->critbit_index_ > critbit) {
      path.pop_back();
    }
    // if the insertion place for key is on the right from this
    // current node, then this node's subtree is all smaller then key,
    // so we want to pop up and switch to the right children.
    //
    // Otherwise, (i.e. key_bit == 0) then subtree's smallest leaf is
    // the leaf we need.
    if (key_bit == 1) {
      while (!path.empty() && path.rbegin()->direction_taken_ == 1) {
        path.pop_back();
      }
      if (path.empty()) {
        return nullptr;
      }
      // we found the place now lets switch it from left to right
      path.rbegin()->direction_taken_ = 1;
    }
    if (path.empty()) {
      assert(key_bit == 0);
      current_variant = &*root_;
    } else {
      const auto& e = *path.rbegin();
      current_variant = &e.parent_node_ptr_->children_[e.direction_taken_];
    }

    return &FindMinLeaf(current_variant)->key_;
  }

  /**
   * @brief Validates structural and crit-bit invariants of the tree.
   *
   * Checks that internal nodes have two children and that the crit-bit
   * index correctly partitions the keys in the subtrees. Aborts program
   * via abort() on failure, printing diagnostic info using printf.
   * Useful for debugging.
   */
  void ValidateInvariants() const {
    size_t node_count = 0;
    if (root_) {
      // Start the recursive validation from the root node.
      ValidateNodeRecursive(&(*root_), node_count);
    }
    // Optional: Can add more checks, e.g., verify node_count against expected size.
    // printf("CritBitTree::ValidateInvariants passed. Node count: %zu\n", node_count);
  }


private: // Private helper methods

  /**
   * @brief Finds the minimum element (leftmost leaf) in a given subtree.
   * Assumes the subtree structure is valid according to tree invariants.
   * @param start_variant Pointer to the variant representing the subtree root.
   * @return Pointer to the const ExternalNode data of the minimum element.
   */
  const ExternalNode* FindMinLeaf(const NodeVariant* start_variant) const {
    const NodeVariant* current_variant = start_variant;
    // Descend left through internal nodes. Assumes valid structure & non-null children.
    while (std::holds_alternative<std::unique_ptr<InternalNode>>(*current_variant)) {
      const auto& internal_node_ptr = std::get<std::unique_ptr<InternalNode>>(*current_variant);
      current_variant = &internal_node_ptr->children_[0]; // Go left
    }
    // Must be at the leftmost ExternalNode variant. Assume valid pointer inside.
    return std::get<std::unique_ptr<ExternalNode>>(*current_variant).get();
  }

  /**
   * @brief Recursive helper for validating tree invariants. Verifies structure
   * and crit-bit properties. Aborts via abort() if any invariant is violated.
   *
   * @param node_variant Pointer to the current node variant being validated.
   * @param node_count Reference to a counter tracking visited nodes (for sanity checks).
   * @return A pair containing: {a representative key from the validated subtree,
   * the length (in bits) of the common prefix shared by all keys in that subtree}.
   */
  ValidationInfo ValidateNodeRecursive(const NodeVariant* node_variant, size_t& node_count) const {
    node_count++; // Count the current node

    // Use std::visit to handle the specific node type (External or Internal).
    return std::visit(
        [&](auto&& node_ptr) -> ValidationInfo {
          // `node_ptr` is a const reference to the std::unique_ptr inside the variant.
          // Assumed non-null based on tree structure invariants (internal nodes have
          // non-null children, root is handled by caller).
          using T = typename std::decay_t<decltype(node_ptr)>::element_type;

          if constexpr (std::is_same_v<T, ExternalNode>) {
            // --- Base Case: Leaf Node ---
            const ExternalNode& leaf = *node_ptr;
            // The common prefix of a leaf node is simply its own key.
            // Return the key and its length in bits.
            return {leaf.key_, leaf.key_.length() * 8};
          }
          else if constexpr (std::is_same_v<T, InternalNode>) {
            // --- Recursive Case: Internal Node ---
            const InternalNode& internal = *node_ptr;
            const size_t current_critbit = internal.critbit_index_;

            // 1. Recursively Validate Children & Get Their Info
            // Assumes children_[0/1] variants hold valid, non-null unique_ptrs.
            ValidationInfo left_info = ValidateNodeRecursive(&internal.children_[0], node_count);
            ValidationInfo right_info = ValidateNodeRecursive(&internal.children_[1], node_count);

            // Extract info using descriptive names for clarity
            std::string_view left_rep_key = left_info.first;
            // size_t left_lcp_len_bits = left_info.second; // LCP length not needed directly
            std::string_view right_rep_key = right_info.first;
            // size_t right_lcp_len_bits = right_info.second; // LCP length not needed directly

            // --- Perform Core Crit-Bit Invariant Checks ---

            // Check 2a: Child Bit Value Invariant
            // Verifies that the representative key from the left subtree has bit 0
            // at this node's critical bit index, and the right has bit 1.
            int bit_L = detail::get_bit(left_rep_key, current_critbit);
            int bit_R = detail::get_bit(right_rep_key, current_critbit);
            if (bit_L != 0) {
              printf("[Validation Fail] Left subtree key bit is %d (expected 0) at node critbit %zu.\n",
                     bit_L, current_critbit);
              printf("  Left Key Sample: '%.*s'\n", (int)left_rep_key.length(), left_rep_key.data());
              abort();
            }
            if (bit_R != 1) {
              printf("[Validation Fail] Right subtree key bit is %d (expected 1) at node critbit %zu.\n",
                     bit_R, current_critbit);
              printf("  Right Key Sample: '%.*s'\n", (int)right_rep_key.length(), right_rep_key.data());
              abort();
            }

            // Check 2b: Key Divergence Invariant
            // Verifies that the representative keys from the left and right subtrees
            // *first* differ exactly at this node's `current_critbit`. This confirms
            // they shared a common prefix up to bit `current_critbit - 1`.
            std::optional<size_t> first_diff = detail::find_crit_bit(left_rep_key, right_rep_key);
            if (!first_diff.has_value() || *first_diff != current_critbit) {
              printf("[Validation Fail] Subtree key divergence error at node critbit %zu.\n", current_critbit);
              if(first_diff.has_value()) printf("  Actual first differing bit: %zu.\n", *first_diff);
              else printf("  Actual: Subtree keys were identical (violates bit value check).\n");
              printf("  Left Key Sample: '%.*s'\n", (int)left_rep_key.length(), left_rep_key.data());
              printf("  Right Key Sample: '%.*s'\n", (int)right_rep_key.length(), right_rep_key.data());
              abort();
            }

            // --- Validation Passed for this Internal Node ---

            // 3. Return Information For This Node's Subtree
            // The common prefix length for the subtree rooted at this internal node
            // is guaranteed up to (but not including) its `current_critbit`.
            // Return one of the child keys as representative; its specific value
            // beyond the common prefix doesn't matter to the parent node's validation.
            return {left_rep_key, current_critbit};
          }
          // else case should be unreachable due to variant definition
        },
        *node_variant); // Pass the variant itself to std::visit
  }

}; // class CritBitTree

#endif // CRITBIT_TREE_H_
