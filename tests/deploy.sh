#!/bin/sh
set -eu
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/overrides.mk" <<'EOF'
universal:
	@:
package-universal:
	@:
EOF
cat >"$tmp/adb" <<'EOF'
#!/bin/sh
echo "$3" >>"$ADB_TEST_LOG"
[ "$3" != "$ADB_TEST_FAIL" ]
EOF
chmod +x "$tmp/adb"
export ADB_TEST_LOG="$tmp/calls"
for ADB_TEST_FAIL in shell push; do
    export ADB_TEST_FAIL
    : >"$ADB_TEST_LOG"
    if make -s -f Makefile -f "$tmp/overrides.mk" deploy-platform \
        MAKE="make -s -f Makefile -f $tmp/overrides.mk" \
        PLATFORM=h700 SERIAL=test ADB="$tmp/adb" >"$tmp/output" 2>&1; then
        echo "FAIL: deploy ignored $ADB_TEST_FAIL failure"
        exit 1
    fi
    if grep -q 'Deploy complete' "$tmp/output"; then
        echo 'FAIL: deploy reported success after an error'
        exit 1
    fi
    if [ "$ADB_TEST_FAIL" = shell ]; then
        [ "$(cat "$ADB_TEST_LOG")" = shell ]
    fi
done
echo 'PASS: deployment stops and reports failed ADB commands'
