// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <string>
#include <string_view>

#include <assert.h>
#include <stdio.h>

#include "absl/container/btree_set.h"

#include "demo-helper.h"

struct Loc {
  const std::string_view data;

  Loc(std::string_view data) : data(data) {}
};

struct LocLess {
  bool operator()(const Loc& a, const Loc& b) const {
    return a.data < b.data;
  }
  bool operator()(std::string_view a, const Loc& b) const {
    return a < b.data;
  }
};

int main(int argc, char** argv) {
  // NOTE, we want this to be destroyed after heap sample dump we
  // setup just below.
  absl::btree_set<Loc, LocLess> locations;

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    Loc l = Loc{std::string_view{s}.substr(pos)};
    locations.insert(l);
  }

  auto it = locations.lower_bound(std::string_view{"the Roman Empire"});
  assert(it != locations.end());

  size_t off = it->data.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last(ish) occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
