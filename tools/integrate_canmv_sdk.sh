#!/bin/sh

set -eu

fail()
{
    printf 'EdgeOS SDK integration: %s\n' "$*" >&2
    exit 1
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
APPS_DIR=$(dirname -- "$APP_DIR")
SDK_ROOT=$(CDPATH= cd -- "$APPS_DIR/../.." && pwd)
APP_NAME=$(basename -- "$APP_DIR")
APPS_MK="$APPS_DIR/apps.mk"

[ "$(basename -- "$APPS_DIR")" = "applications" ] ||
    fail "place this repository directly under canmv_k230/src/applications"
[ -f "$APP_DIR/Kconfig" ] || fail "missing $APP_DIR/Kconfig"
grep -Eq '^[[:space:]]*config[[:space:]]+APP_ENABLE_EDGEOS_DESKTOP([[:space:]]|$)' \
    "$APP_DIR/Kconfig" || fail "Kconfig does not define APP_ENABLE_EDGEOS_DESKTOP"
[ -f "$SDK_ROOT/Makefile" ] || fail "cannot find the CanMV SDK root"
[ -f "$APPS_MK" ] || fail "cannot find $APPS_MK"

case "$APP_NAME" in
    ''|*[!A-Za-z0-9_.-]*)
        fail "unsupported application directory name: $APP_NAME"
        ;;
esac

MAPPING='subdirs-$(CONFIG_APP_ENABLE_EDGEOS_DESKTOP) += '"$APP_NAME"
TMP_FILE=$(mktemp "$APPS_MK.edgeos.XXXXXX") ||
    fail "cannot create a temporary file next to $APPS_MK"

cleanup()
{
    rm -f -- "$TMP_FILE"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

awk -v mapping="$MAPPING" '
    BEGIN { registered = 0 }
    /^[[:space:]]*subdirs-\$\(CONFIG_APP_ENABLE_EDGEOS_DESKTOP\)[[:space:]]*\+=[[:space:]]*/ {
        if (!registered) {
            print mapping
            registered = 1
        }
        next
    }
    { print }
    END {
        if (!registered)
            print mapping
    }
' "$APPS_MK" > "$TMP_FILE"

chmod --reference="$APPS_MK" "$TMP_FILE"
if cmp -s "$APPS_MK" "$TMP_FILE"; then
    printf 'EdgeOS Desktop is already registered in %s\n' "$APPS_MK"
else
    mv -- "$TMP_FILE" "$APPS_MK"
    printf 'Registered EdgeOS Desktop (%s) in %s\n' "$APP_NAME" "$APPS_MK"
fi

cleanup
trap - EXIT HUP INT TERM
printf 'Next: cd %s && make k230_canmv_dongshanpi_edgeos_defconfig\n' "$SDK_ROOT"
printf 'The product defconfig enables "DshanPI EdgeOS Desktop" automatically.\n'
printf 'Use make menuconfig only when you need to inspect or customize it.\n'
