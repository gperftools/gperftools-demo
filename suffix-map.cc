// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <set>
#include <string>
#include <string_view>

#include <assert.h>
#include <stdio.h>

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
  std::set<Loc, LocLess> locations;

  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);
  std::string s = ReadRomanHistoryText();

  for (int pos = s.size() - 1; pos >= 0; pos--) {
    Loc l = Loc{std::string_view{s}.substr(pos)};
    locations.insert(l);
  }

  const std::string_view prefix = "the Roman Empire";
  auto it = locations.lower_bound(prefix);
  assert(it != locations.end());

  assert(!std::prev(it)->data.starts_with(prefix));

  auto farthest_result = it;
  size_t seen_hits = 1;
  for (;;) {
    auto nextit = locations.upper_bound(it->data);
    if (nextit == locations.end() || !nextit->data.starts_with(prefix)) {
      break;
    }
    if (nextit->data.data() > farthest_result->data.data()) {
      farthest_result = nextit;
    }
    seen_hits++;
    it = nextit;
  }
  it = farthest_result;
  printf("seen_hits: %zu\n", seen_hits);

  size_t off = it->data.data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
