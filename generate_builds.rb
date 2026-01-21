#!/usr/bin/ruby

require 'json'

def gen!(b)
  programs = %w[trigram-index suffix-map
                suffix-btree suffix-btree-persistent
                suffix-avl suffix-avl-persistent
                suffix-critbit-tree suffix-trie
                suffix-splay suffix-splay-classic suffix-treap
                coloring]
  programs.each do |name|
    extra_dep = if name == "suffix-btree" then [b.deps.absl_btree] else [] end
    extra_hdr = if name == "suffix-critbit-tree" then ["critbit-tree.h"] else [] end
    extra_hdr += if name == "coloring" then ["coloring-graph-src-inl.h"] else [] end

    # each of the "suffix index" programs have 2 variants. With
    # gperftools' tcmalloc and with system's native memory allocator.
    b.add_binary(name: name,
                 deps: [b.deps.cpu_profiler, b.deps.tcmalloc] + extra_dep,
                 srcs: [name + ".cc", "demo-helper.h"] + extra_hdr,
                 defines: ["WE_HAVE_TCMALLOC"],
                 uses_roman_history: true,
                 no_windows: name == "coloring")
    b.add_binary(name: name + "-sysmalloc",
                 deps: [b.deps.cpu_profiler] + extra_dep,
                 srcs: [name + ".cc", "demo-helper.h"] + extra_hdr,
                 uses_roman_history: true,
                 no_windows: name == "coloring")
  end

  begin
    base_kp = {name: "knight-path",
               deps: [b.deps.cpu_profiler],
               srcs: ["knight-path.cc", "demo-helper.h"],
               defines: []}

    # knight-path program has 3 variants defined by appending the
    # following modifications to the 'base' definition
    [{deps: [b.deps.tcmalloc], defines: ["WE_HAVE_TCMALLOC"]},
     {name: "-stack", deps: [b.deps.tcmalloc], defines: ["WE_HAVE_TCMALLOC", "USE_POSIX_THREAD_RECURSION"], no_windows: true},
     {name: "-sysmalloc"}].each do |h|
      b.add_binary(**(base_kp.merge(h) {|k, v1, v2| v1 + v2}))
    end
  end
end

class BazelGen
  module D
    extend self
    def tcmalloc; :tcmalloc; end
    def cpu_profiler; :cpu_profiler; end
    def absl_btree; "@abseil-cpp//absl/container:btree"; end
  end

  def deps; D; end

  def initialize
    puts(<<HERE)
# Note, this file is auto-generated from generate_builds.rb. So if you
# intend to make longer-lasting changes, make them over there.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

config_setting(
    name = "msvc_compiler",
    flag_values = {"@bazel_tools//tools/cpp:compiler": "msvc-cl"},
)

DEFAULT_COPTS = select({
    ":msvc_compiler": ["/std:c++20"],
    "//conditions:default": ["-std=c++20"],
})

TCMALLOC_DEPS = select({
    "@bazel_tools//src/conditions:windows": ["@gperftools//:tcmalloc_minimal"],
    "//conditions:default": ["@gperftools//:tcmalloc"],
})

PROFILER_DEPS = select({
    "@bazel_tools//src/conditions:windows": [],
    "//conditions:default": ["@gperftools//:cpu_profiler"],
})
HERE
  end

  def add_binary(name:, srcs:, deps: nil, defines: nil, uses_roman_history: false, no_windows: false)
    puts "\ncc_binary("
    puts "    name = #{name.inspect},"
    puts "    srcs = #{srcs.inspect},"
    puts "    copts = DEFAULT_COPTS,"
    
    if defines && !defines.empty?
      puts "    defines = #{defines.inspect},"
    end

    if deps && !deps.empty?
      dep_vars = []
      literal_deps = []
      deps.each do |d|
        case d
        when :tcmalloc then dep_vars << "TCMALLOC_DEPS"
        when :cpu_profiler then dep_vars << "PROFILER_DEPS"
        else literal_deps << d
        end
      end
      
      parts = dep_vars
      unless literal_deps.empty?
         literal_deps = literal_deps.sort.reverse
         parts << literal_deps.inspect
      end
      
      puts "    deps = #{parts.join(' + ')},"
    end

    if no_windows
      puts "    target_compatible_with = select({"
      puts "        \"@bazel_tools//src/conditions:windows\": [\"@platforms//:incompatible\"],"
      puts "        \"//conditions:default\": [],"
      puts "    }),"
    end

    if uses_roman_history
      puts "    data = [\"the-history-of-the-decline-and-fall-of-the-roman-empire.txt\"],"
    end
    puts ")"
  end
end

class AMGen
  module D
    extend self
    def tcmalloc; :tcmalloc; end
    def cpu_profiler; :cpuprofiler; end
    def absl_btree; :absl_btree; end
  end
  def deps; D; end

  def initialize
    @b = []
  end
  def add_binary(**kwargs)
    @b << kwargs
  end

  def finalize!
    # header
    puts(<<'HERE')
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

HERE

    btree, non_btree = @b.partition do |d|
      (d[:deps] || []).include? :absl_btree
    end

    print_var!("noinst_PROGRAMS", non_btree.map {|d| d[:name]})
    non_btree.each {|d| puts; print_prog_definition!(**d)}

    unless non_btree.empty?
      puts
      puts "if BUILD_BTREE"
      print_var!("noinst_PROGRAMS+", btree.map {|d| d[:name]})

      btree.each {|d| puts; print_prog_definition!(**d)}
      puts "endif BUILD_BTREE"
    end

    # footer
    puts(<<'HERE')

EXTRA_DIST = README.adoc LICENSE \
             BUILD MODULE.bazel .bazelrc \
             generate_builds.rb \
             the-history-of-the-decline-and-fall-of-the-roman-empire.txt
HERE
  end

  def print_prog_definition!(name:, srcs:, deps:, defines: [], uses_roman_history: false, no_windows: false)
    u = name.gsub("-", "_")
    print_var!("#{u}_SOURCES", srcs)
    unless defines.empty?
      print_var! "#{u}_CPPFLAGS", (defines.map {|d| '-D' + d})
    end

    deps.delete :cpuprofiler
    unless deps.empty?
      cflags = deps.map {|d| "$(#{d}_CFLAGS)"}
      libs = (deps + [:cpuprofiler]).map {|d| "$(#{d}_LIBS)"}
      print_var!("#{u}_CXXFLAGS", ["$(AM_CXXFLAGS)", *cflags])
      print_var!("#{u}_LDADD", libs)
    else
      print_var!("#{u}_LDADD", ["$(cpuprofiler_LIBS)"])
    end
  end

  def print_var!(name, value)
    puts(gen_print_var(name, value))
  end

  def gen_print_var(name, value)
    plus_eq = false
    if name.end_with? '+'
      name = name[0..-2]
      plus_eq = true
    end

    value = [value] if value.kind_of? String

    simple_value = value.join(' ')
    simple_def = if plus_eq
                   "#{name} += #{simple_value}"
                 else
                   "#{name} = #{simple_value}"
                 end
    return simple_def if simple_def.size < 130

    ret = if plus_eq
            "#{name} += "
          else
            "#{name} = "
          end
    indent = ret.size
    ret << value.first
    value[1..-1].each {|v| ret << " \\\n" << (" " * indent) << v}

    ret
  end
end

# generator for compiler_commands.json for tools like ccls or
# clangd. Essentially required for modern IDEs like VSCode or Antigravity.
class CompileCommandsGen
  def deps; AMGen::D; end

  def initialize
    @entries = []
  end

  def add_binary(name:, srcs:, deps: nil, defines: nil, uses_roman_history: false, no_windows: false)
    return if name.end_with?("-sysmalloc") # want to index tcmalloc-ful compilations
    flags = (defines || []).map {|d| "-D#{d}"}
    includes = %w[abseil-cpp+ gperftools+/src].map do |path_frag|
      "-Ibazel-gperftools-demo/external/#{path_frag}"
    end

    first_non_header = srcs.find {|f| !f.end_with?(".h")}
    raise unless first_non_header

    srcs.each do |filename|
      cc_src = if filename.end_with?(".h")
                 first_non_header
               else
                 filename
               end
      e = {directory: Dir.pwd,
           file: filename,
           arguments: ["g++", *flags, *includes, "-std=c++20", "-c", cc_src]}
      @entries << e
    end
  end

  def finalize!
    puts JSON.pretty_generate(@entries)
  end
end

class CMakeGen
  module D
    extend self
    def tcmalloc; "gperftools::tcmalloc"; end
    def cpu_profiler; "gperftools::profiler"; end
    def absl_btree; "absl::btree"; end
  end
  def deps; D; end

  def initialize
    puts(<<~HERE)
      # Note, this file is auto-generated from generate_builds.rb. So if you
      # intend to make longer-lasting changes, make them over there.
    HERE
  end

  def add_binary(name:, srcs:, deps: nil, defines: nil, uses_roman_history: false, no_windows: false)
    puts "\nadd_executable(#{name} #{srcs.join(' ')})"

    if defines && !defines.empty?
      puts "target_compile_definitions(#{name} PRIVATE #{defines.join(' ')})"
    end

    link_deps = (deps || []) + ["Threads::Threads"]
    puts "target_link_libraries(#{name} PRIVATE #{link_deps.join(' ')})"
  end
end

def replacing_stdout!(f, &block)
  if f.kind_of? String
    return File.open(f, "wb") {|io| replacing_stdout!(io, &block)}
  end

  io = STDOUT.dup
  STDOUT.reopen(f.dup)
  begin
    yield
  ensure
    STDOUT.reopen(io)
    io.close rescue nil
  end
end

Dir.chdir File.dirname(__FILE__)

replacing_stdout! "BUILD" do
  gen!(BazelGen.new)
end

replacing_stdout! "Makefile.am" do
  AMGen.new.tap {|b| gen!(b)}.finalize!
end

replacing_stdout! "compile_commands.json" do
  CompileCommandsGen.new.tap {|b| gen!(b)}.finalize!
end

replacing_stdout! "targets.cmake" do
  gen!(CMakeGen.new)
end
