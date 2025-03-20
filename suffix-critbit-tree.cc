// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <fstream>
#include <sstream>
#include <string>

#include <assert.h>
#include <stdio.h>

#include "critbit-tree.h"
#include "demo-helper.h"

int main(int argc, char** argv) {
  // NOTE, we want this to be destroyed after heap sample dump we
  // setup just below.
  CritBitTree locations;

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
  }

#ifndef NDEBUG
  locations.ValidateInvariants();
#endif

  const std::string_view* it;
  const std::string_view prefix = "the Roman Empire";
  it = locations.LowerBound(prefix);
  if (!it) {
    fprintf(stderr, "didn't find?\n");
    abort();
  }

  const std::string_view* farthest_result = it;
  size_t seen_hits = 1;
  for (;;) {
    auto nextit = locations.LowerBound(*it, true);
    if (!nextit || !nextit->starts_with(prefix)) {
      break;
    }
    if (nextit->data() == it->data()) { abort(); }
    if (nextit->data() > farthest_result->data()) {
      farthest_result = nextit;
    }

    {
      size_t lcp = std::mismatch(it->begin(), it->end(),
                                 nextit->begin(), nextit->end()).first - it->begin();
      std::string test_s{it->substr(0, lcp + 1)};
      test_s[lcp]++;
      auto testit = locations.LowerBound(std::string_view{test_s});
      if (testit != nextit) { abort(); }
    }

    seen_hits++;
    it = nextit;
  }
  it = farthest_result;
  printf("seen_hits: %zu\n", seen_hits);

  size_t off = it->data() - s.data();
  printf("off = %zu\n", off);

  printf("context of last occurrence of 'the Roman Empire':\n");
  PrintOccurenceContext(s, off);
}
