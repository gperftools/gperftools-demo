# Note, this file is auto-generated from generate_builds.rb. So if you
# intend to make longer-lasting changes, make them over there.

add_executable(trigram-index trigram-index.cc demo-helper.h)
target_compile_definitions(trigram-index PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(trigram-index PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(trigram-index-sysmalloc trigram-index.cc demo-helper.h)
target_link_libraries(trigram-index-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-map suffix-map.cc demo-helper.h)
target_compile_definitions(suffix-map PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-map PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-map-sysmalloc suffix-map.cc demo-helper.h)
target_link_libraries(suffix-map-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-btree suffix-btree.cc demo-helper.h)
target_compile_definitions(suffix-btree PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-btree PRIVATE gperftools::profiler gperftools::tcmalloc absl::btree Threads::Threads)

add_executable(suffix-btree-sysmalloc suffix-btree.cc demo-helper.h)
target_link_libraries(suffix-btree-sysmalloc PRIVATE gperftools::profiler absl::btree Threads::Threads)

add_executable(suffix-btree-persistent suffix-btree-persistent.cc demo-helper.h)
target_compile_definitions(suffix-btree-persistent PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-btree-persistent PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-btree-persistent-sysmalloc suffix-btree-persistent.cc demo-helper.h)
target_link_libraries(suffix-btree-persistent-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-avl suffix-avl.cc demo-helper.h)
target_compile_definitions(suffix-avl PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-avl PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-avl-sysmalloc suffix-avl.cc demo-helper.h)
target_link_libraries(suffix-avl-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-avl-persistent suffix-avl-persistent.cc demo-helper.h)
target_compile_definitions(suffix-avl-persistent PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-avl-persistent PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-avl-persistent-sysmalloc suffix-avl-persistent.cc demo-helper.h)
target_link_libraries(suffix-avl-persistent-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-critbit-tree suffix-critbit-tree.cc demo-helper.h critbit-tree.h)
target_compile_definitions(suffix-critbit-tree PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-critbit-tree PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-critbit-tree-sysmalloc suffix-critbit-tree.cc demo-helper.h critbit-tree.h)
target_link_libraries(suffix-critbit-tree-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-trie suffix-trie.cc demo-helper.h)
target_compile_definitions(suffix-trie PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-trie PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-trie-sysmalloc suffix-trie.cc demo-helper.h)
target_link_libraries(suffix-trie-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-splay suffix-splay.cc demo-helper.h)
target_compile_definitions(suffix-splay PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-splay PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-splay-sysmalloc suffix-splay.cc demo-helper.h)
target_link_libraries(suffix-splay-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-splay-classic suffix-splay-classic.cc demo-helper.h)
target_compile_definitions(suffix-splay-classic PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-splay-classic PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-splay-classic-sysmalloc suffix-splay-classic.cc demo-helper.h)
target_link_libraries(suffix-splay-classic-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(suffix-treap suffix-treap.cc demo-helper.h)
target_compile_definitions(suffix-treap PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(suffix-treap PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(suffix-treap-sysmalloc suffix-treap.cc demo-helper.h)
target_link_libraries(suffix-treap-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(coloring coloring.cc demo-helper.h coloring-graph-src-inl.h)
target_compile_definitions(coloring PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(coloring PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(coloring-sysmalloc coloring.cc demo-helper.h coloring-graph-src-inl.h)
target_link_libraries(coloring-sysmalloc PRIVATE gperftools::profiler Threads::Threads)

add_executable(knight-path knight-path.cc demo-helper.h)
target_compile_definitions(knight-path PRIVATE WE_HAVE_TCMALLOC)
target_link_libraries(knight-path PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(knight-path-stack knight-path.cc demo-helper.h)
target_compile_definitions(knight-path-stack PRIVATE WE_HAVE_TCMALLOC USE_POSIX_THREAD_RECURSION)
target_link_libraries(knight-path-stack PRIVATE gperftools::profiler gperftools::tcmalloc Threads::Threads)

add_executable(knight-path-sysmalloc knight-path.cc demo-helper.h)
target_link_libraries(knight-path-sysmalloc PRIVATE gperftools::profiler Threads::Threads)
