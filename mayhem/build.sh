#!/usr/bin/env bash
#
# mayhem/build.sh — build Etterna's self-contained parsing/calculation code into libFuzzer harnesses
# (MinaCalc, MsdFile, RageUnicode) plus a behavioral self-test. The full game (Vulkan, ffmpeg, Lua,
# crashpad, ...) is deliberately not built: these sources compile standalone and have no network deps.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

BUILD="$SRC/build"
GEN="$BUILD/generated"
FUZZ="$BUILD/fuzz"
TESTS="$BUILD/tests"
mkdir -p "$GEN" "$FUZZ" "$TESTS"

# config.hpp is normally produced by CMake's configure_file; emit the Linux subset of its switches.
sed -E '/^#cmakedefine (HAVE_ALLOCA_H|HAVE_DIRENT_H|HAVE_INTTYPES_H|HAVE_STDINT_H|HAVE_SYS_PARAM_H|HAVE_SYS_UTSNAME_H|HAVE_FCNTL_H|HAVE_UNISTD_H|HAVE_MKDIR|HAVE_SNPRINTF|HAVE_STRCASECMP|HAVE_PTHREAD_MUTEX_TIMEDLOCK|HAVE_PTHREAD_COND_TIMEDWAIT) 1/{s/^#cmakedefine/#define/;b};s/^#cmakedefine .*//' \
  src/Etterna/config.in.hpp > "$GEN/config.hpp"

# Upstream's STANDALONE_CALC mode no longer compiles: Calc::InitializeHands calls
# Bazoinkazoink::load_calc_params_from_disk, which that mode compiles out. Build MinaCalc from a copy
# of its directory without that one disk-params call (debug/loadparams only; the calculator itself is
# unchanged). The copy is regenerated from src/ on every run, so edits to MinaCalc still take effect.
OVERLAY="$BUILD/minacalc_overlay"
rm -rf "$OVERLAY"
mkdir -p "$OVERLAY/Etterna"
cp -r src/Etterna/MinaCalc "$OVERLAY/Etterna/MinaCalc"
ln -s "$SRC/src/Etterna/Models" "$OVERLAY/Etterna/Models"
sed -i '/ulbu_in_charge->load_calc_params_from_disk(true);/d' "$OVERLAY/Etterna/MinaCalc/MinaCalc.cpp"
if grep -q 'load_calc_params_from_disk' "$OVERLAY/Etterna/MinaCalc/MinaCalc.cpp"; then
  echo "build.sh: MinaCalc.cpp still references load_calc_params_from_disk" >&2; exit 1
fi

INC=(-I"$OVERLAY" -I"$SRC/src" -I"$SRC/src/Etterna" -I"$SRC/src/Core" -I"$GEN")
CXXSTD=(-std=c++20 -w -DSTANDALONE_CALC)
# The project sources the harnesses exercise.
PROJECT_SRCS=(
  build/minacalc_overlay/Etterna/MinaCalc/MinaCalc.cpp
  src/Etterna/FileTypes/MsdFile.cpp
  src/RageUtil/Misc/RageUnicode.cpp
)
objname() { local b; b="$(basename "$1")"; echo "${b%.*}"; }

# 1) Sanitized project objects (ASan + UBSan, halting; DWARF <= 3), built twice: $FUZZ/lf holds the
#    libFuzzer set with SanCov (-fsanitize=fuzzer-no-link, so the fuzzed code reports edges), $FUZZ/sa
#    the uninstrumented set for the -standalone reproducers.
COV_LF="-fsanitize=fuzzer-no-link"
mkdir -p "$FUZZ/lf" "$FUZZ/sa"
for src in "${PROJECT_SRCS[@]}"; do
  for mode in lf sa; do
    cov=""; [ "$mode" = lf ] && cov="$COV_LF"
    $CXX $SANITIZER_FLAGS $cov $DEBUG_FLAGS -O1 -ffunction-sections -fdata-sections "${CXXSTD[@]}" "${INC[@]}" \
      -c "$src" -o "$FUZZ/$mode/$(objname "$src").o" &
    while [ "$(jobs -rp | wc -l)" -ge "$MAYHEM_JOBS" ]; do wait -n; done
  done
done
wait

# 2) Leak detection off at build time, and the run-once driver compiled as C.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$FUZZ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ/standalone_main.o"

# 3) Each harness twice: libFuzzer binary + standalone reproducer. MsdFile.cpp's file-reading path
#    references RageFile, which the harnesses never call. --gc-sections drops it from the uninstrumented
#    standalone link, but with SanCov the default GNU ld keeps ReadFile alive through the coverage
#    tables, so that libFuzzer link (only) uses lld ($3), whose --gc-sections honours them.
build_harness() {
  local name="$1" obj="$2" lf_ldflags="${3:-}"
  $CXX $SANITIZER_FLAGS $COV_LF $DEBUG_FLAGS -O1 -ffunction-sections -fdata-sections "${CXXSTD[@]}" "${INC[@]}" \
    -c "mayhem/$name/harness.cc" -o "$FUZZ/lf/$name.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -ffunction-sections -fdata-sections "${CXXSTD[@]}" "${INC[@]}" \
    -c "mayhem/$name/harness.cc" -o "$FUZZ/sa/$name.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE -Wl,--gc-sections $lf_ldflags \
    "$FUZZ/lf/$name.o" "$FUZZ/lf/$obj" "$FUZZ/lsan_off.o" -o "/mayhem/$name"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS -Wl,--gc-sections \
    "$FUZZ/sa/$name.o" "$FUZZ/sa/$obj" "$FUZZ/lsan_off.o" "$FUZZ/standalone_main.o" -o "/mayhem/$name-standalone"
}
build_harness fuzz_minacalc MinaCalc.o
build_harness fuzz_msdfile  MsdFile.o -fuse-ld=lld
build_harness fuzz_utf8     RageUnicode.o

# 4) Behavioral self-test with NORMAL flags (no sanitizers); mayhem/test.sh only runs it.
TEST_OBJS=()
for src in "${PROJECT_SRCS[@]}" mayhem/selftest.cc; do
  obj="$TESTS/$(objname "$src").o"
  $CXX -O2 $COVERAGE_FLAGS -ffunction-sections -fdata-sections "${CXXSTD[@]}" "${INC[@]}" -c "$src" -o "$obj" &
  while [ "$(jobs -rp | wc -l)" -ge "$MAYHEM_JOBS" ]; do wait -n; done
  TEST_OBJS+=("$obj")
done
wait
$CXX $COVERAGE_FLAGS -Wl,--gc-sections "${TEST_OBJS[@]}" -o "$TESTS/etterna_selftest"
