#!/usr/bin/env bash

if [[ "$(uname)" =~ "Darwin" ]]; then
  host=darwin-x86_64
  sanitize=-fsanitize=undefined
elif [[ "$(uname)" =~ "Linux" ]]; then
  host=linux-x86_64
  sanitize=-fsanitize=undefined,memory
fi

options="-Wall -Wextra -Wshadow -Wpedantic -Werror"

# Compile with 02
$ANDROID_HOME/ndk/25.2.9519653/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android26-clang REPRO.c -std=c99 -o remote-02 -O2 -g -fPIC ${options} || exit 1

# Compile with 0s and get crash
$ANDROID_HOME/ndk/25.2.9519653/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android26-clang REPRO.c -std=c99 -o remote-0s -Os -g -fPIC ${options} 2> CRASH_LOG.txt

# Crash should contain Loop vectorizer in BT
cat CRASH_LOG.txt | grep "llvm::InnerLoopVectorizer::scalarizeInstruction" || exit 2

# Compile with previous NDK
$ANDROID_HOME/ndk/25.1.8937393/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android26-clang REPRO.c -std=c99 -o remote-0s-old -Os -g -fPIC ${options} || exit 3

exit 0
