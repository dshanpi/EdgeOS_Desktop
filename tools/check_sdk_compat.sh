#!/usr/bin/env bash

set -Eeuo pipefail

fail()
{
    printf 'EdgeOS SDK compatibility: %s\n' "$*" >&2
    exit 1
}

usage()
{
    cat <<'EOF'
Usage: check_sdk_compat.sh [--sdk PATH]

Verify that the complete EdgeOS SDK patch set and active product
configuration are compatible with this application checkout.
EOF
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
EDGEOS_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
SDK_ROOT=
if [[ ${1:-} == -h || ${1:-} == --help ]]; then
    usage
    exit 0
fi
if [[ ${1:-} == --sdk ]]; then
    [[ -n ${2:-} ]] || fail "--sdk requires a path"
    SDK_ROOT=$(cd -- "$2" && pwd -P)
    shift 2
fi
(($# == 0)) || {
    usage >&2
    fail "unknown argument: $1"
}
if [[ -z $SDK_ROOT ]]; then
    APPS_DIR=$(dirname -- "$EDGEOS_ROOT")
    [[ $(basename -- "$APPS_DIR") == applications ]] ||
        fail "use --sdk PATH or place EdgeOS directly below src/applications"
    SDK_ROOT=$(cd -- "$APPS_DIR/../.." && pwd -P)
fi

APPS_DIR=$(cd -- "$SDK_ROOT/src/applications" 2>/dev/null && pwd -P) ||
    fail "missing SDK application directory"
[[ $(dirname -- "$EDGEOS_ROOT") == "$APPS_DIR" ]] ||
    fail "place this EdgeOS checkout directly below $APPS_DIR"
APPS_MK=$APPS_DIR/apps.mk
[[ -f $APPS_MK ]] || fail "missing $APPS_MK"
APP_MAPPING='subdirs-$(CONFIG_APP_ENABLE_LVGL_LAUNCHER) += '
APP_MAPPING+=$(basename -- "$EDGEOS_ROOT")
mapping_count=$(grep -Ec \
    '^[[:space:]]*subdirs-\$\(CONFIG_APP_ENABLE_LVGL_LAUNCHER\)[[:space:]]*\+=[[:space:]]*' \
    "$APPS_MK" || true)
[[ $mapping_count == 1 && $(grep -Fxc -- "$APP_MAPPING" "$APPS_MK" || true) == 1 ]] ||
    fail "application is not registered exactly once; run tools/integrate_canmv_sdk.sh"

"$SCRIPT_DIR/apply_sdk_patches.sh" --check --sdk "$SDK_ROOT"

require_text()
{
    local file=$1
    local pattern=$2
    local description=$3
    [[ -f $SDK_ROOT/$file ]] || fail "missing $file ($description)"
    grep -Eq "$pattern" "$SDK_ROOT/$file" ||
        fail "$description is absent from $file"
}

require_text \
    src/rtsmart/mpp/middleware/src/mp4_player/include/kplayer.h \
    'kd_player_seek[[:space:]]*\(' 'player seek API'
require_text \
    src/rtsmart/mpp/middleware/src/kdmedia/include/media.h \
    'k_vicap_mirror[[:space:]]+mirror' 'camera mirror configuration'
require_text \
    src/rtsmart/libs/3rd-party/lvgl/port/lv_k230_input_touch.h \
    'lv_k230_touch_accept_click[[:space:]]*\(' 'touch click filter API'
require_text \
    src/rtsmart/libs/rtsmart_hal/components/k230_ota/k230_ota.h \
    'k230_ota_get_status[[:space:]]*\(' 'OTA status API'
require_text \
    src/rtsmart/libs/rtsmart_hal/drivers/pmu/drv_pmu.h \
    'drv_pmu_reboot_to_upgrade[[:space:]]*\(' 'public PMU reboot API'
require_text \
    configs/k230_canmv_dongshanpi_edgeos_defconfig \
    'CONFIG_RT_PARTITION_NUMBER=4' 'four-partition EdgeOS product config'

if [[ -f $SDK_ROOT/.config ]]; then
    require_text .config \
        '^CONFIG_BOARD_CONFIG_NAME="k230_canmv_dongshanpi_edgeos_defconfig"$' \
        'active EdgeOS defconfig'
    require_text .config '^CONFIG_APP_ENABLE_LVGL_LAUNCHER=y$' \
        'enabled EdgeOS application'
    require_text .config '^CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL=y$' \
        'enabled userspace LVGL library'
else
    printf 'Warning: %s/.config is absent; run the EdgeOS defconfig next.\n' \
        "$SDK_ROOT" >&2
fi

archive=$SDK_ROOT/output/k230_canmv_dongshanpi_edgeos_defconfig/rtsmart/mpp/middleware/lib/libmp4_player.a
if [[ -f $archive ]]; then
    nm_tool=${K230_TOOLCHAIN_NM:-}
    if [[ -z $nm_tool ]]; then
        nm_tool=$(command -v riscv64-unknown-linux-musl-nm || true)
    fi
    if [[ -n $nm_tool ]]; then
        "$nm_tool" -g --defined-only "$archive" | grep -Eq \
            '[[:space:]]kd_player_seek$' ||
            fail "libmp4_player.a does not export kd_player_seek"
    else
        printf 'Warning: cross nm not found; archive symbol check skipped.\n' >&2
    fi
fi

printf 'EdgeOS SDK source/config compatibility checks passed.\n'
