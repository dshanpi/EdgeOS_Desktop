# Building EdgeOS Desktop under `rtos_k230`

This guide is for users who need to integrate EdgeOS Desktop into a K230 RTOS
SDK and generate complete firmware. `rtos_k230` and `canmv_k230` are only local
directory names. When the workspace comes from the locked K230 RTOS/CanMV repo
manifest, the correct target is still
`k230_canmv_dongshanpi_edgeos_defconfig`.

This procedure pins the following compatibility unit:

| Component | Pinned version |
| --- | --- |
| EdgeOS Desktop | `edgeos-sdk-v1.0.0` |
| EdgeOS commit | `18df75d569bd5ecdfd8ccec8d37bf343e530533d` |
| Upstream manifest | `d207027db3ae457cd43629c80b8a42e3b79fd51a` |
| SDK projects | 24 immutable revisions in `sdk/manifests/upstream-lock.xml` |
| Target board | DshanPI CanMV-K230 V3 |

Do not force this patch set onto an arbitrary "latest" SDK or apply only part
of it.

## 1. Prepare the host

Ubuntu 20.04 or 22.04 x86_64 is recommended. Install the common K230 SDK host
dependencies, Git LFS, and repo:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  make autoconf automake bison flex gcc g++ gawk libncurses5-dev \
  pkg-config libconfuse-dev libssl-dev python3 python3-pip \
  python-is-python3 cmake libyaml-dev scons mtools bzip2 curl git \
  git-lfs openssh-client rsync dosfstools ca-certificates wget fdisk
pip3 install pycryptodome gmssl scons==3.1.2

mkdir -p "$HOME/.bin"
export PATH="$HOME/.bin:$PATH"
curl -L https://raw.githubusercontent.com/canmv-k230/git-repo/stable/repo \
  -o "$HOME/.bin/repo"
chmod a+rx "$HOME/.bin/repo"
git lfs install
```

Reserve at least about 20 GiB of free space. The EdgeOS output directory alone
was about 6.3 GiB in this validation; sources, toolchains, repo objects, and
temporary images require additional space.

## 2. Create a locked SDK workspace

A fresh workspace is strongly recommended. The directory is deliberately
named `rtos_k230` below to demonstrate that the name does not change the SDK
type or target:

The command below requires `$HOME/rtos_k230` not to exist. Stop immediately if
`mkdir` or any later command fails; do not continue from the wrong directory:

```bash
mkdir "$HOME/rtos_k230" &&
cd "$HOME/rtos_k230" &&
repo init \
  -u https://github.com/dshanpi/EdgeOS_Desktop.git \
  -b refs/tags/edgeos-sdk-v1.0.0 \
  -m sdk/manifests/upstream-lock.xml \
  --repo-url=https://github.com/canmv-k230/git-repo.git &&
repo sync -c -j"$(nproc)"
```

The full `refs/tags/edgeos-sdk-v1.0.0` ref is required. A bare
`-b edgeos-sdk-v1.0.0` is interpreted as a branch by repo and fails.

Inspect the workspace and download both SDK toolchains:

```bash
repo status
make dl_toolchain
```

A fresh sync should have no ordinary tracked project changes. Nested project
directories may appear as untracked in a repo-managed multi-project tree; that
is expected.

## 3. Clone and verify EdgeOS

The application must be an immediate child of `src/applications/`:

```bash
(
set -e
cd "$HOME/rtos_k230/src/applications"
git clone --branch edgeos-sdk-v1.0.0 \
  https://github.com/dshanpi/EdgeOS_Desktop.git
cd EdgeOS_Desktop

test "$(git rev-parse HEAD)" = \
  "18df75d569bd5ecdfd8ccec8d37bf343e530533d"
printf 'Verified EdgeOS commit: %s\n' "$(git rev-parse HEAD)"
git describe --tags --exact-match HEAD
git lfs pull
git lfs fsck
)
```

The LFS commands are mandatory. The repository contains many LFS-managed
models, fonts, images, and prebuilt assets. LFS pointer files are not usable
runtime resources.

## 4. Verify and apply Scheme A

Run the read-only gate first, then apply all six SDK series as one unit:

```bash
cd "$HOME/rtos_k230/src/applications/EdgeOS_Desktop" &&
./tools/apply_sdk_patches.sh --check &&
./tools/apply_sdk_patches.sh --apply &&
./tools/integrate_canmv_sdk.sh
```

`--check` verifies the patch SHA-256 inventory, all 24 SDK projects, six exact
bases, deterministic HEAD/tree results, Git operation state, and untracked
path collisions. Do not force past a failure.

The patch process creates local commits in six SDK Git projects. Afterward, do
not blindly run a moving-branch `repo sync`, reset, or clean on this workspace.
Create a new locked workspace when upgrading to another release.

## 5. Select the product configuration

Return to the SDK root and use the dedicated EdgeOS defconfig:

```bash
cd "$HOME/rtos_k230"
make k230_canmv_dongshanpi_edgeos_defconfig &&
./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

Do not invent a `k230_rtos_*` target because the local directory is named
`rtos_k230`. The active configuration should include at least:

```text
CONFIG_BOARD_CONFIG_NAME="k230_canmv_dongshanpi_edgeos_defconfig"
CONFIG_RT_PARTITION_NUMBER=4
CONFIG_RTSMART_3RD_PARTY_ENABLE_LVGL=y
CONFIG_APP_ENABLE_LVGL_LAUNCHER=y
```

The dedicated defconfig already enables EdgeOS. Use `make menuconfig` only to
inspect or customize it; the application appears under
`Applications Configuration`. If you only inspect the menu, run the defconfig
again afterward so an accidental save cannot alter the release configuration.

Menu discovery and build registration are separate. `Kconfig` supplies the
menu entry, while `tools/integrate_canmv_sdk.sh` registers the actual directory
in `src/applications/apps.mk`.

## 6. Run the full build

Use a command that propagates a nested make failure through `tee`:

```bash
cd "$HOME/rtos_k230"
bash -o pipefail -c \
  'time make 2>&1 | tee edgeos-v1.0.0-build.log'
```

Do not treat `make app` as final acceptance. It is useful for application
diagnostics after the base libraries exist. A release build must return zero
from the full `make` and end with:

```text
Build K230 done, board k230_canmv_dongshanpi, config k230_canmv_dongshanpi_edgeos_defconfig
```

Run compatibility checking once more after the build. This also verifies the
symbol exported by the generated player archive:

```bash
K230_TOOLCHAIN_NM="${SDK_TOOLCHAIN_DIR:-$HOME/.kendryte/k230_toolchains}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
```

## 7. Verify the artifacts

```bash
cd "$HOME/rtos_k230/output/k230_canmv_dongshanpi_edgeos_defconfig"

test -s DshanPI_EdgeOS_Desktop_v0.7.5.img
test -s DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg
sha256sum \
  DshanPI_EdgeOS_Desktop_v0.7.5.img \
  DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg
fdisk -l DshanPI_EdgeOS_Desktop_v0.7.5.img
sed -n '1,120p' images/sdcard/revision.txt
```

The factory image should contain three pre-created FAT partitions: a 20 MiB
`bin`, a 1 GiB `app_a`, and a 1 GiB `app_b`. On the first boot from an 8 GB or
larger microSD, RT-Smart creates the fourth `/data` partition from the remaining
space, reboots once to rescan the table, then formats and mounts it. Do not
remove power during this first-boot sequence.

The device OTA client consumes the uncompressed
`DshanPI_EdgeOS_Desktop_v0.7.5_ota.kdimg`. Do not configure a `.kdimg.gz` file
as the device download target. Before downloading an OTA, `/data` must have
more free space than the uncompressed KDIMG. This run produced about 1.02 GiB;
leave additional headroom in a real deployment.

## 8. Non-default toolchain path

The top-level SDK and the EdgeOS sub-applications use different variables. If
the toolchain is not in its default location, use this complete block instead
of the section 6 build and post-build check:

```bash
(
set -e
export SDK_TOOLCHAIN_DIR="$HOME/k230_toolchains"
export K230_TOOLCHAIN_BIN="$SDK_TOOLCHAIN_DIR/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin"

cd "$HOME/rtos_k230"
make dl_toolchain
make k230_canmv_dongshanpi_edgeos_defconfig
bash -o pipefail -c 'time make 2>&1 | tee edgeos-v1.0.0-build.log'
K230_TOOLCHAIN_NM="$K230_TOOLCHAIN_BIN/riscv64-unknown-linux-musl-nm" \
  ./src/applications/EdgeOS_Desktop/tools/check_sdk_compat.sh
)
```

- `SDK_TOOLCHAIN_DIR` is the toolchain root used by the SDK Makefiles.
- `K230_TOOLCHAIN_BIN` is the musl `bin` directory used by standalone EdgeOS
  sub-application scripts.

Keep both variables consistent for defconfig, compatibility checks, and builds.

## 9. Safely handling an existing SDK

A fresh workspace is preferred. If an existing directory must be reused:

1. Preserve your source and logs, then inspect `repo status`.
2. Run `./tools/apply_sdk_patches.sh --check` from EdgeOS.
3. Stop on a revision mismatch; rebuild using section 2 instead of forcing.
4. Do not treat output from the ordinary DshanPI defconfig as EdgeOS evidence.

If the integration script was run before patching, the SDK root may have only
the expected unstaged `apps.mk` change. Use the recovery sequence below only
when both `test` commands pass and manual inspection confirms that the diff is
only the EdgeOS mapping generated by the integration script. It writes that
diff to a unique temporary file. The idempotent integration script regenerates
the mapping after patching, so no pre-existing Git stash can be popped by
mistake:

```bash
(
set -e
cd "$HOME/rtos_k230"
test -z "$(git diff --cached --name-only)"
test "$(git diff --name-only)" = "src/applications/apps.mk"
git diff -- src/applications/apps.mk

edgeos_apps_backup=
trap 'if [ -n "$edgeos_apps_backup" ]; then printf "apps.mk backup kept at %s\\n" "$edgeos_apps_backup"; fi' EXIT
edgeos_apps_backup=$(mktemp "${TMPDIR:-/tmp}/edgeos-apps-mk.XXXXXX.patch")
git diff --binary -- src/applications/apps.mk > "$edgeos_apps_backup"
test -s "$edgeos_apps_backup"
git apply --check --reverse "$edgeos_apps_backup"
git restore --worktree -- src/applications/apps.mk

cd src/applications/EdgeOS_Desktop
./tools/apply_sdk_patches.sh --check
./tools/apply_sdk_patches.sh --apply

cd ../../..
./src/applications/EdgeOS_Desktop/tools/integrate_canmv_sdk.sh
git diff -- src/applications/apps.mk
)
```

Do not use this to hide other project changes. `--check` remains the only gate
for deciding whether patching is safe. Keep the printed backup file until the
new mapping has been reviewed and the build has completed.

## 10. Troubleshooting

| Symptom | Cause | Resolution |
| --- | --- | --- |
| `fatal error: kplayer.h` | Complete Scheme A was not applied | Return to the exact lock and run `--check`, then `--apply` |
| Missing `lv_k230_touch_accept_click()` or `k230_ota_get_status()` | Only the app or selected patches were copied | Apply the complete six-project unit |
| EdgeOS is absent from `Applications Configuration` | The clone is not an immediate child of `src/applications/`, or Kconfig was not regenerated | Correct the location and rerun defconfig/menuconfig |
| Menu entry exists but no `[BUILD] applications EdgeOS_Desktop` | `apps.mk` is not registered | Run `tools/integrate_canmv_sdk.sh` |
| `revision mismatch` | SDK is not the v1.0.0 24-project baseline | Create a locked workspace; do not force |
| Models are tiny LFS pointers | LFS content was not downloaded | Run `git lfs pull && git lfs fsck` |
| Cross compiler not found | Toolchains are missing or variables disagree | Run `make dl_toolchain` and inspect both variables |
| DTLS-SRTP/Mbed TLS undefined references | Old Mbed TLS objects survived a config change, or the ordinary defconfig is active | Select the EdgeOS defconfig; use the targeted clean below only when necessary |
| App FAT image reports no space | Old 512 MiB layout or incomplete resources | Verify complete Scheme A and the 1 GiB A/B layout |

For a reused SDK with confirmed stale Mbed TLS objects, use only this targeted
cleanup:

```bash
cd "$HOME/rtos_k230"
make -C src/rtsmart/libs/3rd-party/mbedtls/mbedtls/library clean
```

See the measured `rtos_k230` results in
[`validation/rtos-k230-edgeos-sdk-v1.0.0.md`](validation/rtos-k230-edgeos-sdk-v1.0.0.md).
