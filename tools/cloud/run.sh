#!/bin/bash
# opsis-run.sh <qt|cef|both> [group ...]: unpacks ~/opsis-src.tar.gz over ~/opsis/browser (the build dirs stay), builds
# in the Arch container and runs the engine checks there. Logs: ~/opsis/logs/<engine>.log, each ending with its totals;
# ~/opsis/logs/state says "running <what>" or "done". On CEF each group runs in a browser of its own: the checks give up
# at a step that never answers, and while CEF lacks what a group asks for that would end the whole run. Each runs through
# tools/check-engine, as on the laptop, so the browser gets what that gives it (Chromium's private X server, the sound
# group's sound server, the outer data folder). CEF is Tacit's engine as it ships, ~/opsis/cef-tacit (tools/cloud/setup.sh),
# not a stock build.
set -u
what=${1:-both}; shift || true
mkdir -p ~/opsis/logs ~/opsis/home/.local/share ~/opsis/incoming
echo "running $what" > ~/opsis/logs/state
rm -rf ~/opsis/incoming/* && tar -C ~/opsis/incoming -xzf ~/opsis-src.tar.gz
mkdir -p ~/opsis/browser
# by content, and what changed takes this moment's time: a file older than the last build (another tree's run came
# between) was copied with its own time and not rebuilt, so the run checked the other tree's code (2026-11-13)
rsync -a --no-times --checksum --delete --exclude=/build --exclude=/build-dev --exclude=/build-cef --exclude=/adblock/target ~/opsis/incoming/browser/ ~/opsis/browser/
[ -d ~/opsis/home/.local/share/fonts ] || tar -C ~/opsis/home/.local/share -xzf ~/opsis-fonts.tar.gz
run() { sudo docker run --rm --privileged --network=host -v ~/opsis:/work -e HOME=/work/home -e LANG=en_GB.UTF-8 -w /work/browser opsis-arch bash -lc "export XDG_RUNTIME_DIR=\$(mktemp -d); $1"; }
if [ ! -f ~/opsis/cef-tacit/build/libcef_dll_wrapper/libcef_dll_wrapper.a ]; then
    run 'cd /work/cef-tacit && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B build . >/dev/null && ninja -C build libcef_dll_wrapper | tail -1' > ~/opsis/logs/wrapper.log 2>&1
fi
if [ "$what" = qt ] || [ "$what" = both ]; then
    run "tools/check-engine $*" > ~/opsis/logs/qt.log 2>&1
    echo "exit $?" >> ~/opsis/logs/qt.log
fi
if [ "$what" = cef ] || [ "$what" = both ]; then
    groups=${*:-$(grep -o '{ group: "[a-zA-Z]*" }' ~/opsis/browser/qml/EngineCheck.qml | sed 's/.*"\(.*\)".*/\1/' | tr '\n' ' ')}
    start=$(date +%s)
    # a build that fails stops here: the checks used to run the browser an earlier build left in build-cef, so a
    # change that did not build was checked as the old code (2026-11-13: no Qt WebSockets in the image, the old
    # browser had no "sound" group, and the run said 0 checks, 0 failed, exit 0). A group's own exit is kept too.
    run "cmake -S . -B build-cef -G Ninja -DCMAKE_BUILD_TYPE=Release -DBROWSER_ENGINE=cef -DCEF_ROOT=/work/cef-tacit > /work/logs/cef-configure.log 2>&1 || { tail -20 /work/logs/cef-configure.log; echo 'BUILD FAILED: cmake'; exit 3; }
         cmake --build build-cef -j8 > /work/logs/cef-build.log 2>&1 || { grep -E 'error|FAILED' /work/logs/cef-build.log | tail -20; echo 'BUILD FAILED: cmake --build'; exit 3; }
         worst=0; for g in $groups; do CHECK_TIMEOUT=300 BROWSER_BUILD=build-cef tools/check-engine \$g; rc=\$?; [ \$rc = 0 ] || { echo \"FAIL group \$g ended with exit \$rc\"; worst=\$rc; }; done
         exit \$worst" > ~/opsis/logs/cef.log 2>&1
    rc=$?
    awk -v s=$(( $(date +%s) - start )) '/^ok/ { ok++ } /^FAIL/ || /^no group / || /^BUILD FAILED/ { failed++ } /^FAIL timed out at step/ { cut++ }
        END { printf "== %d checks, %d failed, %d s (%d groups cut short at a step that never answered)\n", ok + failed, failed, s, cut }' ~/opsis/logs/cef.log >> ~/opsis/logs/cef.log
    echo "exit $rc" >> ~/opsis/logs/cef.log
fi
echo done > ~/opsis/logs/state
