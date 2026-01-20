// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
#include <algorithm>
#include <functional>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h> // for isatty

#include "demo-helper.h"

// Trigram is 3 adjacent characters. We choose to normalize various
// space-ful characters into usual ASCII space symbol (code 0x20).
struct Trigram {
  char data[3];

  static Trigram FromStringAt(std::string_view s, size_t pos) {
    assert(s.size() >= pos + 3);

    Trigram rv;
    memcpy(rv.data, s.data() + pos, 3);
    return rv;
  }

  static Trigram FromString(std::string_view s) {
    return FromStringAt(s, 0);
  }

  void Spacify() {
    struct H {
      static char ToSpace(char ch) {
        return (ch == '\t' || ch == '\n') ? ' ' : ch;
      }
    };
    data[0] = H::ToSpace(data[0]);
    data[1] = H::ToSpace(data[1]);
    data[2] = H::ToSpace(data[2]);
  }

  bool operator==(Trigram o) const {
    return memcmp(data, o.data, 3) == 0;
  }
};

// We'll be using trigrams as keys in std::unordered_map so lets
// define some trivial hashing of those things.
namespace std {
template <>
struct hash<Trigram> {
  size_t operator()(Trigram a) const {
    uint32_t v = 0;
    memcpy(&v, &a, sizeof(a));
    return std::hash<uint32_t>{}(v);
  }
};
}  // namespace std

// Our (deliberately simplified, naive) positional index is a mapping
// from trigram to ordered list of positions where those trigrams are
// in our text. Set of positions (in this case vector of uint32_t-s)
// is commonly called "posting list".
using Index = std::unordered_map<Trigram, std::vector<uint32_t>>;

// When searching occurrences of a trigram we use SearchTerm to group
// and pass around trigram, offset and posting list. Offsets are
// usually small integers. They're used to find substrings longer than
// 3 characters. I.e. string of foobar would be 2 SearchTerms, one
// "foo" at offset 0 and another "bar" at offset 3. Matching
// conjunction of such 2 terms will locate substring "foobar". See
// FindConjunction below.
struct SearchTerm {
  Trigram tgram;
  uint32_t tgram_offset;
  const std::vector<uint32_t>* hits;

  SearchTerm(Trigram tgram,
             uint32_t tgram_offset,
             const std::vector<uint32_t>* hits)
    : tgram(tgram), tgram_offset(tgram_offset), hits(hits) {}
};

// Special 0xffff_ffff offset ise used to represent "we didn't find anything"
// condition.
static constexpr uint32_t kNoMatch = ~uint32_t{0};

// AdvanceFn is a function that that is used to find successive
// matches. Single argument is minimal position and return value is
// either kNoMatch or position (>= then given minimum) of the match.
using AdvanceFn = std::function<uint32_t(uint32_t)>;

uint32_t EmptyAdvance(uint32_t) {
  return kNoMatch;
}

// FindConjunction finds match(es) where all of the given terms match
// the text. It is used to find matches of exact substrings. See
// PrepareSubstringSearch below.
uint32_t FindConjunction(const std::vector<SearchTerm>& terms, uint32_t min_pos) {
backtrack:
  for (size_t i = 0; i < terms.size(); i++) {
    const SearchTerm& t = terms[i];
    uint32_t this_pos = min_pos + t.tgram_offset;
    auto it = std::lower_bound(t.hits->begin(), t.hits->end(), this_pos);
    if (it == t.hits->end()) {
      return kNoMatch;
    }
    uint32_t pos = *it;
    if (pos != this_pos) {
      min_pos = pos - t.tgram_offset;
      if (i != 0) {
        goto backtrack;
      }
    }
  }
  return min_pos;
}

// PrepareSubstringSearch builds AdvanceFn used to match exact
// occurrences of a given substring.
AdvanceFn PrepareSubstringSearch(std::string_view str, const Index& idx) {
  std::vector<SearchTerm> terms;
  size_t sz = str.size();
  for (size_t i = 0; i < sz; i += 3) {
    size_t pos = std::min(sz - 3, i);
    Trigram t = Trigram::FromString(str.substr(pos, 3));
    auto it = idx.find(t);
    if (it == idx.end()) {
      return EmptyAdvance;
    }
    const std::vector<uint32_t>& hits = it->second;
    terms.emplace_back(t, pos, &hits);
  }

  // When ordering our search terms it is most useful to place
  // smallest posting lists first (i.e. most "selective").
  std::sort(terms.begin(), terms.end(),
            [&] (const SearchTerm& a, const SearchTerm& b) -> bool {
              return a.hits->size() < b.hits->size();
            });

  return [terms = std::move(terms)] (uint32_t off) {
    return FindConjunction(terms, off);
  };
}

// FindConjunctionOfDisjunctions is used in case-insensitive
// search. For example, when searching for substring "foobar", we use query that finds
// successive matches of different forms trigrams foo and bar. E.g. in
// some lisp-y notations:
//
//  (and (or "foo" "Foo" "fOo" ... "FOO") (or "bar" "Bar" "bAr" ... "BAR")).
//
// In order words, we'll be searching a match where for each of the
// elements of "outer" vector there will be at least one match among
// search terms of it's "inner" vector. Conjunction (i.e. "and") of disjunctions (i.e. "or").
uint32_t FindConjunctionOfDisjunctions(const std::vector<std::vector<SearchTerm>>& terms, uint32_t min_pos) {
backtrack:
  for (size_t i = 0; i < terms.size(); i++) {
    const std::vector<SearchTerm>& disj = terms[i];
    uint32_t best_pos = kNoMatch;
    for (const SearchTerm& t : disj) {
      uint32_t this_pos = min_pos + t.tgram_offset;
      auto it = std::lower_bound(t.hits->begin(), t.hits->end(), this_pos);
      if (it != t.hits->end()) {
        best_pos = std::min(best_pos, *it - t.tgram_offset);
      }
    }
    if (best_pos == min_pos) {
      continue;
    }
    if (best_pos == kNoMatch) {
      return kNoMatch;
    }
    min_pos = best_pos;
    if (i != 0) {
      goto backtrack;
    }
  }
  return min_pos;
}

// PrepareSubstringSearch returns AdvanceFn that performs
// case-insensitive searches of a given substring. See
// FindConjunctionOfDisjunctions.
AdvanceFn PrepareCISubstringSearch(std::string_view str, const Index& idx) {
  std::vector<std::vector<SearchTerm>> terms;

  for (size_t i = 0; i < str.size(); i += 3) {
    i = std::min(i, str.size() - 3);

    // Adder is our recursive "function" that generates all forms of
    // upper/lower-casedness of a given trigram. So we can generate
    // search terms for case-insensitiveness disjunction.
    struct Adder {
      const Index& idx;
      const size_t i;

      Trigram t;
      std::vector<SearchTerm> disj;

      Adder(const Index& idx, size_t i) : idx(idx), i(i) {}

      void Rec(int depth) {
        if (depth == 3) {
          auto it = idx.find(t);
          if (it == idx.end()) {
            return;
          }
          const std::vector<uint32_t>& hits = it->second;
          disj.emplace_back(t, i, &hits);
          return;
        }

        Rec(depth + 1);
        char ch = t.data[depth];
        if ('A' <= ch && ch <= 'Z') {
          t.data[depth] |= 0x20; // lowercase current character
          Rec(depth + 1);
          t.data[depth] = ch;
        } else if ('a' <= ch && ch <= 'z') {
          t.data[depth] &= ~char{0x20}; // flip to upper case
          Rec(depth + 1);
          t.data[depth] = ch;
        }
      }
    } adder{idx, i};
    adder.t = Trigram::FromString(str.substr(i, 3));
    adder.Rec(0);

    if (adder.disj.empty()) {
      return EmptyAdvance;
    }
    terms.push_back(std::move(adder.disj));
  }

  // Just like for case-sensitive search, it helps to order our
  // members of top-level conjunction from most selective (smallest
  // number of hits) to least selective.
  std::ranges::sort(terms.begin(), terms.end(), {},
                    [&] (const std::vector<SearchTerm>& a) -> size_t {
                      auto v = std::views::transform(a, [] (const SearchTerm& a) -> size_t {
                        return a.hits->size();
                      });
                      return std::reduce(v.begin(), v.end());
                    });

  return [terms = std::move(terms)] (uint32_t off) -> uint32_t {
    return FindConjunctionOfDisjunctions(terms, off);
  };
}

// PrepareSubstringSearch returns advance function that finds
// "space-runs-insensitive" matches of a given query. I.e. space
// characters of a query will match arbitrary runs of spaces in the
// original text. I.e. query "foo bar" will equivalent to regular
// expression /foo\s+bar/m.
AdvanceFn PrepareSpacefulSearch(std::string_view query,
                                bool search_ci,
                                const Index& index,
                                std::span<const std::pair<uint32_t, uint32_t>> space_runs) {
  assert(query.size() > 0);
  assert(!isspace(query[query.size() - 1])); // we don't support trailing or leading space (yet?)
  assert(!isspace(query[0]));

  // First, we split our query into a sequence of "words".
  std::vector<std::string_view> words;
  std::string_view q = query;
  bool first = true;
  while (!q.empty()) {
    auto qs = q.size();

    size_t s = 0;
    for (; s < qs; s++) {
      if (isspace(q[s])) {
        s++;
        break;
      }
    }

    // Word is non-space chars followed by single space (when
    // present).
    const char* start = &q[0];
    const char* end = start + s;
    if (!first) {
      start--; // add one preceding space character
    } else {
      first = false;
    }
    words.emplace_back(start, end - start);

    q = q.substr(s);
  }

  std::vector<AdvanceFn> word_searches;

  // Then each of the words is translated into it's own match function.
  for (std::string_view last_word : words) {
    if (search_ci) {
      word_searches.emplace_back(PrepareCISubstringSearch(last_word, index));
    } else {
      word_searches.emplace_back(PrepareSubstringSearch(last_word, index));
    }
  }

  // And then we return our own "composite" advance function that uses
  // sub-words matches and space runs.
  return [word_searches = std::move(word_searches),
          words = std::move(words),
          space_runs] (uint32_t min_pos) -> uint32_t {
  again:
    min_pos = word_searches[0](min_pos);
    if (min_pos == kNoMatch) {
      return kNoMatch;
    }
    uint32_t current_min = min_pos;
    uint32_t prev_word_size = words[0].size();

    for (size_t i = 1; i < word_searches.size(); i++) {
      auto it = std::lower_bound(space_runs.begin(), space_runs.end(), current_min + 1, [] (std::pair<uint32_t, uint32_t> a, uint32_t b) {
        return a.first < b;
      });
      assert(it != space_runs.end());
      assert(it->first == current_min + prev_word_size - 1);
      (void)prev_word_size;
      current_min = it->first + it->second - 1;

      uint32_t new_min = word_searches[i](current_min);
      if (new_min == kNoMatch) {
        return kNoMatch;
      }
      if (new_min != current_min) {
        // okay, we couldn't "confirm" current match position
        // (current_min). So lets try to "unwind" this new position to
        // potential new min_pos so we can restart full match there.
        //
        // First, we locate space run immediately before our current word.
        it = std::lower_bound(space_runs.begin(), space_runs.end(), new_min + 1, [] (std::pair<uint32_t, uint32_t> a, uint32_t b) {
          return a.first < b;
        });
        // Then we step back `i' times, being careful not to step past
        // current min_pos.
        for (size_t k = 0; k <= i; k++) {
          it--;
          assert(space_runs.begin() <= it);
          if (it->first <= min_pos) {
            min_pos = new_min;
            goto again;
          }
        }
        min_pos = it->first + it->second - 1;
        goto again;
      }

      prev_word_size = words[i].size();
    }

    return min_pos;
  };
}

bool GetBoolEnvDefaultTrue(const char* name) {
  const char* val = getenv(name);
  if (val == nullptr) {
    return true;
  }
  std::string_view val_str{val};
  return !(val_str == "0" || val_str == "NO");
}

int main(int argc, char** argv) {
  auto sampling_cleanup = MaybeSetupHeapSampling(argc, argv);

  std::string s = ReadFileToString("the-history-of-the-decline-and-fall-of-the-roman-empire.txt");
  printf("text size is %zu bytes\n", s.size());

  constexpr std::string_view kSearchString{"the Roman Empire"};

  // Somewhat hack-fully we do 10 repetitions to make sure entire
  // thing takes enough CPU time to produce useful CPU profile.
  for (int reps_left = 9; reps_left >= 0; reps_left--) {
    Index index;

    if (isatty(fileno(stdout))) {
      if (reps_left == 0) {
        printf("\nRunning last repetition. Will print outcomes\n");
      } else {
        printf("\rIndexing repetitions left: %4d", reps_left);
        fflush(stdout);
      }
    }

    // Build positional trigram index.
    for (uint32_t pos = 0; pos + 3 <= s.size(); pos++) {
      Trigram g = Trigram::FromStringAt(s, pos);
      g.Spacify();
      index[g].push_back(pos);
    }

    // Build index of space runs.
    std::vector<std::pair<uint32_t, uint32_t>> space_runs;
    {
      bool in_space_run = false;
      uint32_t first_space = ~uint32_t{};
      uint32_t pos = 0;
      for (; pos < s.size(); pos++) {
        if (isspace(s[pos])) {
          if (!in_space_run) {
            first_space = pos;
            in_space_run = true;
          }
        } else {
          if (in_space_run) {
            space_runs.emplace_back(first_space, pos - first_space);
            in_space_run = false;
          }
        }
      }
      if (in_space_run) {
        space_runs.emplace_back(first_space, pos - first_space);
      }
    }

    if (reps_left == 0) {
      printf("unique trigams count = %zu\n", index.size());
    }

    // Prepare search query for our query ("the roman empire" above).
    AdvanceFn advance_search;
    bool search_ci = GetBoolEnvDefaultTrue("TRIGRAM_SEARCH_CI");
    if (GetBoolEnvDefaultTrue("TRIGRAM_SEARCH_SPACEFUL")) {
      advance_search = PrepareSpacefulSearch(kSearchString, search_ci, index, space_runs);
    } else if (search_ci) {
      advance_search = PrepareCISubstringSearch(kSearchString, index);
    } else {
      advance_search = PrepareSubstringSearch(kSearchString, index);
    }

    auto print_occurrence = [&] (std::string_view nth, uint32_t off) {
      if (reps_left > 0) {
        return;
      }

      printf("off = %zu\n", static_cast<size_t>(off));
      assert(off + 1 != 0);

      printf("context of %.*s occurrence of '%.*s':\n",
             (int)nth.size(), nth.data(),
             (int)kSearchString.size(), kSearchString.data());
      PrintOccurenceContext(s, off);
    };

    // Iterate over all matches and print some of them.
    uint32_t prev_off = kNoMatch;
    uint32_t off = kNoMatch;
    size_t seen_hits = 0;
    for (;;) {
      off = advance_search(off + 1);
      if (off == kNoMatch) {
        break;
      }
      if (prev_off == kNoMatch) {
        print_occurrence("first", off);
      }
      prev_off = off;
      seen_hits++;
    }
    print_occurrence("last", prev_off);
    if (reps_left == 0) {
      printf("total hits seen: %zu\n", seen_hits);
    }

    if (reps_left == 0) {
      // Note, we want to capture and write heap sample before Index
      // is destroyed, so while it's memory is alive.
      sampling_cleanup.DumpHeapSampleNow();
    }
  }
}
