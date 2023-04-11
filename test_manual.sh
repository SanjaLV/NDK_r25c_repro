#!/usr/bin/env bash

if [[ "$(uname)" =~ "Darwin" ]]; then
  host=darwin-x86_64
  sanitize=-fsanitize=undefined
elif [[ "$(uname)" =~ "Linux" ]]; then
  host=linux-x86_64
  sanitize=-fsanitize=undefined,memory
fi

# Run code with local clang sanitizers
clang REPRO.c -std=c99 -O2 ${sanitize} -g -fPIC -o local || exit 1
./local > LOCAL_LOG.txt || exit 2

# Compile with 02 and run test on arm64 device
$ANDROID_HOME/ndk/25.2.9519653/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android31-clang REPRO.c -std=c99 -o remote-02 -O2 -g -fPIC || exit 3
adb push remote-02 /data/local/tmp/remote-02.test || exit 4
adb shell "chmod 0755 /data/local/tmp/remote-02.test" || exit 5
adb shell /data/local/tmp/remote-02.test > REMOTE_02_LOG.txt || exit 6

# Compile with 0s and run test on arm64 device
$ANDROID_HOME/ndk/25.2.9519653/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android31-clang REPRO.c -std=c99 -o remote-0s -Os -g -fPIC || exit 7
adb push remote-0s /data/local/tmp/remote-0s.test || exit 8
adb shell "chmod 0755 /data/local/tmp/remote-0s.test" || exit 9
adb shell /data/local/tmp/remote-0s.test > REMOTE_0s_LOG.txt || exit 10

# Compile with previous NDK and run test on arm64 device
$ANDROID_HOME/ndk/25.1.8937393/toolchains/llvm/prebuilt/${host}/bin/aarch64-linux-android31-clang REPRO.c -std=c99 -o remote-0s-old -Os -g -fPIC || exit 11
adb push remote-0s-old /data/local/tmp/remote-0s-old.test || exit 12
adb shell "chmod 0755 /data/local/tmp/remote-0s-old.test" || exit 13
adb shell /data/local/tmp/remote-0s-old.test > REMOTE_0s_OLD_LOG.txt || exit 14

# LOCAL_LOG and REMOTE_02_LOG should be the same
diff LOCAL_LOG.txt REMOTE_02_LOG.txt || exit 15

# LOCAL_LOG and REMOTE_0s_OLD_LOG should be the same
diff LOCAL_LOG.txt REMOTE_0s_OLD_LOG.txt || exit 16

# REMOTE_0s_LOG should NOT match LOCAL_LOG
diff REMOTE_0s_LOG.txt LOCAL_LOG.txt || exit 0

exit 17

