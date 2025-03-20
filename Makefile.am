# Note, this file is auto-generated from generate_builds.rb. So if you
# intend to make longer-lasting changes, make them over there.

ACLOCAL_AMFLAGS = -I m4

AM_CXXFLAGS = $(cpuprofiler_CFLAGS)

# Note, we need to pull -lprofiler even when system's default is
# --as-needed. So we end up with this. I'd prefer -Wl,-uProfilerStart
# but see here: https://sourceware.org/bugzilla/show_bug.cgi?id=32816
#
# So, note, we end up with specifying -lprofiler twice. First in
# LDFLAGS (with no-as-needed added). This is to get cpuprofiler added
# if it is shared library. And -lprofiler is added second time as part
# of XYZ_LDADD definitions below and -uProfilerStart here and
# -lprofiler there have libprofiler linked it if we're dealing with
# statically built libprofiler.
AM_LDFLAGS = -Wl,--push-state,--no-as-needed \
              $(cpuprofiler_LIBS) \
             -Wl,--pop-state -Wl,-uProfilerStart

noinst_PROGRAMS = trigram-index \
                  trigram-index-sysmalloc \
                  suffix-map \
                  suffix-map-sysmalloc \
                  suffix-btree-persistent \
                  suffix-btree-persistent-sysmalloc \
                  suffix-avl \
                  suffix-avl-sysmalloc \
                  suffix-avl-persistent \
                  suffix-avl-persistent-sysmalloc \
                  suffix-critbit-tree \
                  suffix-critbit-tree-sysmalloc \
                  suffix-trie \
                  suffix-trie-sysmalloc \
                  suffix-splay \
                  suffix-splay-sysmalloc \
                  suffix-splay-classic \
                  suffix-splay-classic-sysmalloc \
                  suffix-treap \
                  suffix-treap-sysmalloc \
                  coloring \
                  coloring-sysmalloc \
                  knight-path \
                  knight-path-stack \
                  knight-path-sysmalloc

trigram_index_SOURCES = trigram-index.cc demo-helper.h
trigram_index_CPPFLAGS = -DWE_HAVE_TCMALLOC
trigram_index_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
trigram_index_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

trigram_index_sysmalloc_SOURCES = trigram-index.cc demo-helper.h
trigram_index_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_map_SOURCES = suffix-map.cc demo-helper.h
suffix_map_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_map_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_map_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_map_sysmalloc_SOURCES = suffix-map.cc demo-helper.h
suffix_map_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_btree_persistent_SOURCES = suffix-btree-persistent.cc demo-helper.h
suffix_btree_persistent_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_btree_persistent_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_btree_persistent_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_btree_persistent_sysmalloc_SOURCES = suffix-btree-persistent.cc demo-helper.h
suffix_btree_persistent_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_avl_SOURCES = suffix-avl.cc demo-helper.h
suffix_avl_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_avl_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_avl_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_avl_sysmalloc_SOURCES = suffix-avl.cc demo-helper.h
suffix_avl_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_avl_persistent_SOURCES = suffix-avl-persistent.cc demo-helper.h
suffix_avl_persistent_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_avl_persistent_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_avl_persistent_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_avl_persistent_sysmalloc_SOURCES = suffix-avl-persistent.cc demo-helper.h
suffix_avl_persistent_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_critbit_tree_SOURCES = suffix-critbit-tree.cc demo-helper.h critbit-tree.h
suffix_critbit_tree_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_critbit_tree_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_critbit_tree_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_critbit_tree_sysmalloc_SOURCES = suffix-critbit-tree.cc demo-helper.h critbit-tree.h
suffix_critbit_tree_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_trie_SOURCES = suffix-trie.cc demo-helper.h
suffix_trie_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_trie_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_trie_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_trie_sysmalloc_SOURCES = suffix-trie.cc demo-helper.h
suffix_trie_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_splay_SOURCES = suffix-splay.cc demo-helper.h
suffix_splay_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_splay_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_splay_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_splay_sysmalloc_SOURCES = suffix-splay.cc demo-helper.h
suffix_splay_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_splay_classic_SOURCES = suffix-splay-classic.cc demo-helper.h
suffix_splay_classic_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_splay_classic_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_splay_classic_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_splay_classic_sysmalloc_SOURCES = suffix-splay-classic.cc demo-helper.h
suffix_splay_classic_sysmalloc_LDADD = $(cpuprofiler_LIBS)

suffix_treap_SOURCES = suffix-treap.cc demo-helper.h
suffix_treap_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_treap_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
suffix_treap_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

suffix_treap_sysmalloc_SOURCES = suffix-treap.cc demo-helper.h
suffix_treap_sysmalloc_LDADD = $(cpuprofiler_LIBS)

coloring_SOURCES = coloring.cc demo-helper.h coloring-graph-src-inl.h
coloring_CPPFLAGS = -DWE_HAVE_TCMALLOC
coloring_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
coloring_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

coloring_sysmalloc_SOURCES = coloring.cc demo-helper.h coloring-graph-src-inl.h
coloring_sysmalloc_LDADD = $(cpuprofiler_LIBS)

knight_path_SOURCES = knight-path.cc demo-helper.h
knight_path_CPPFLAGS = -DWE_HAVE_TCMALLOC
knight_path_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
knight_path_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

knight_path_stack_SOURCES = knight-path.cc demo-helper.h
knight_path_stack_CPPFLAGS = -DWE_HAVE_TCMALLOC -DUSE_POSIX_THREAD_RECURSION
knight_path_stack_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS)
knight_path_stack_LDADD = $(tcmalloc_LIBS) $(cpuprofiler_LIBS)

knight_path_sysmalloc_SOURCES = knight-path.cc demo-helper.h
knight_path_sysmalloc_LDADD = $(cpuprofiler_LIBS)

if BUILD_BTREE
noinst_PROGRAMS += suffix-btree suffix-btree-sysmalloc

suffix_btree_SOURCES = suffix-btree.cc demo-helper.h
suffix_btree_CPPFLAGS = -DWE_HAVE_TCMALLOC
suffix_btree_CXXFLAGS = $(AM_CXXFLAGS) $(tcmalloc_CFLAGS) $(absl_btree_CFLAGS)
suffix_btree_LDADD = $(tcmalloc_LIBS) $(absl_btree_LIBS) $(cpuprofiler_LIBS)

suffix_btree_sysmalloc_SOURCES = suffix-btree.cc demo-helper.h
suffix_btree_sysmalloc_CXXFLAGS = $(AM_CXXFLAGS) $(absl_btree_CFLAGS)
suffix_btree_sysmalloc_LDADD = $(absl_btree_LIBS) $(cpuprofiler_LIBS)
endif BUILD_BTREE

EXTRA_DIST = README.adoc LICENSE \
             BUILD MODULE.bazel .bazelrc \
             generate_builds.rb \
             the-history-of-the-decline-and-fall-of-the-roman-empire.txt
