#!/usr/bin/env bash

set -Eeuo pipefail

fail()
{
    printf 'EdgeOS SDK patch set: %s\n' "$*" >&2
    exit 1
}

usage()
{
    cat <<'EOF'
Usage: apply_sdk_patches.sh [--check | --apply] [--sdk PATH]

  --check      Verify exact SDK revisions and preflight every patch (default).
  --apply      Preflight all projects, then apply the complete patch set.
  --sdk PATH   CanMV K230 SDK root. It is inferred when EdgeOS is checked out
               directly below src/applications/.

The script never runs repo sync, fetch, stash, reset, or clean.
EOF
}

MODE=check
SDK_ROOT=
while (($#)); do
    case "$1" in
        --check)
            MODE=check
            ;;
        --apply)
            MODE=apply
            ;;
        --sdk)
            shift
            (($#)) || fail "--sdk requires a path"
            SDK_ROOT=$1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
    shift
done

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
EDGEOS_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
MANIFEST=$EDGEOS_ROOT/sdk/manifest.json
CHECKSUMS=$EDGEOS_ROOT/sdk/SHA256SUMS
LOCK_MANIFEST=$EDGEOS_ROOT/sdk/manifests/upstream-lock.xml

if [[ -z $SDK_ROOT ]]; then
    APPS_DIR=$(dirname -- "$EDGEOS_ROOT")
    [[ $(basename -- "$APPS_DIR") == applications ]] ||
        fail "use --sdk PATH or place EdgeOS directly below src/applications"
    SDK_ROOT=$(cd -- "$APPS_DIR/../.." && pwd -P)
else
    SDK_ROOT=$(cd -- "$SDK_ROOT" && pwd -P)
fi

[[ -d $SDK_ROOT/.repo ]] || fail "not a repo-managed SDK: $SDK_ROOT"
[[ -f $SDK_ROOT/Makefile ]] || fail "missing SDK Makefile: $SDK_ROOT"
[[ -f $MANIFEST ]] || fail "missing $MANIFEST"
[[ -f $CHECKSUMS ]] || fail "missing $CHECKSUMS"
[[ -f $LOCK_MANIFEST ]] || fail "missing $LOCK_MANIFEST"
command -v git >/dev/null || fail "git is required"
command -v python3 >/dev/null || fail "python3 is required"
command -v sha256sum >/dev/null || fail "sha256sum is required"

python3 - "$EDGEOS_ROOT" <<'PY' ||
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
checksum_file = root / "sdk/SHA256SUMS"
sdk_items = list((root / "sdk").rglob("*"))
protected_tools = [
    root / "tools/apply_sdk_patches.sh",
    root / "tools/check_sdk_compat.sh",
    root / "tools/integrate_canmv_sdk.sh",
]
links = [item for item in sdk_items + protected_tools if item.is_symlink()]
if links:
    raise SystemExit("symlinks are forbidden in the patch inventory: " +
                     ", ".join(str(item.relative_to(root)) for item in links))
entries = {}
for line_number, line in enumerate(
        checksum_file.read_text(encoding="utf-8").splitlines(), 1):
    match = re.fullmatch(r"([0-9a-f]{64}) ([ *])(.+)", line)
    if not match:
        raise SystemExit(f"invalid SHA256SUMS line {line_number}")
    name = match.group(3)
    path = pathlib.PurePosixPath(name)
    if (path.is_absolute() or ".." in path.parts or str(path) != name or
            name in entries):
        raise SystemExit(f"unsafe or duplicate checksum path: {name!r}")
    entries[name] = match.group(1)

expected = {
    item.relative_to(root).as_posix()
    for item in sdk_items
    if item.is_file() and item != checksum_file
}
expected.update({
    "tools/apply_sdk_patches.sh",
    "tools/check_sdk_compat.sh",
    "tools/integrate_canmv_sdk.sh",
})
missing = sorted(expected - entries.keys())
extra = sorted(entries.keys() - expected)
if missing or extra:
    details = []
    if missing:
        details.append("missing: " + ", ".join(missing))
    if extra:
        details.append("unexpected: " + ", ".join(extra))
    raise SystemExit("checksum inventory mismatch (" + "; ".join(details) + ")")
PY
    fail "patch-set checksum inventory is incomplete"

(cd -- "$EDGEOS_ROOT" &&
    sha256sum --strict --quiet --check sdk/SHA256SUMS) ||
    fail "patch-set checksum verification failed"

declare -a PROJECT_ROWS=()
PROJECT_DATA=
if ! PROJECT_DATA=$(python3 - "$MANIFEST" <<'PY'
import json
import posixpath
import re
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
projects = manifest.get("projects")
if not isinstance(projects, list) or not projects:
    raise SystemExit("manifest contains no projects")
fields = ("id", "path", "base_revision", "patched_revision",
          "patched_tree", "series")
seen_ids = set()
seen_paths = set()
for project in projects:
    values = []
    for field in fields:
        value = project.get(field)
        if (not isinstance(value, str) or not value or
                any(char in value for char in "\t\r\n\0")):
            raise SystemExit(f"invalid {field!r} in project manifest")
        values.append(value)
    project_id, path, base, patched_revision, tree, series = values
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", project_id):
        raise SystemExit(f"unsafe project id: {project_id!r}")
    if path != "." and (path.startswith("/") or
                         posixpath.normpath(path) != path or
                         ".." in path.split("/")):
        raise SystemExit(f"unsafe project path: {path!r}")
    if (series.startswith("/") or posixpath.normpath(series) != series or
            ".." in series.split("/") or
            not series.startswith("sdk/patches/") or
            not series.endswith("/series")):
        raise SystemExit(f"unsafe series path: {series!r}")
    if not re.fullmatch(r"[0-9a-f]{40}", base):
        raise SystemExit(f"invalid base revision for {project_id}")
    if not re.fullmatch(r"[0-9a-f]{40}", patched_revision):
        raise SystemExit(f"invalid patched revision for {project_id}")
    if not re.fullmatch(r"[0-9a-f]{40}", tree):
        raise SystemExit(f"invalid patched tree for {project_id}")
    if project_id in seen_ids or path in seen_paths:
        raise SystemExit(f"duplicate project id/path: {project_id!r}")
    seen_ids.add(project_id)
    seen_paths.add(path)
    print("\t".join(values))
PY
); then
    fail "project manifest could not be read"
fi
[[ -n $PROJECT_DATA ]] || fail "project manifest contains no usable rows"
mapfile -t PROJECT_ROWS <<< "$PROJECT_DATA"

python3 - "$SDK_ROOT" "$MANIFEST" "$LOCK_MANIFEST" <<'PY' ||
import json
import pathlib
import posixpath
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

sdk_root = pathlib.Path(sys.argv[1]).resolve()
with open(sys.argv[2], encoding="utf-8") as stream:
    patch_manifest = json.load(stream)
targets = {project["path"]: project for project in patch_manifest["projects"]}

def git(repo, *arguments, check=True):
    result = subprocess.run(
        ["git", "-C", str(repo), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if check and result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise SystemExit(f"git {' '.join(arguments)} failed in {repo}: {detail}")
    return result

try:
    lock_root = ET.parse(sys.argv[3]).getroot()
except (ET.ParseError, OSError) as error:
    raise SystemExit(f"cannot read resolved SDK lock manifest: {error}")
if lock_root.tag != "manifest":
    raise SystemExit("resolved SDK lock has an invalid root element")

seen_paths = set()
for project in lock_root.findall("project"):
    path = project.get("path")
    revision = project.get("revision")
    if not path or not revision:
        raise SystemExit("resolved SDK lock contains an incomplete project")
    if (path != "." and (path.startswith("/") or
                          posixpath.normpath(path) != path or
                          ".." in path.split("/"))):
        raise SystemExit(f"unsafe project path in resolved SDK lock: {path!r}")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise SystemExit(f"resolved SDK lock is not immutable at {path!r}")
    if path in seen_paths:
        raise SystemExit(f"duplicate project path in resolved SDK lock: {path!r}")
    seen_paths.add(path)

    repo = (sdk_root / path).resolve()
    try:
        repo.relative_to(sdk_root)
    except ValueError:
        if repo != sdk_root:
            raise SystemExit(f"locked project path escapes the SDK: {path!r}")
    if not repo.is_dir():
        raise SystemExit(f"locked project path is missing: {path!r}")
    if git(repo, "rev-parse", "--is-inside-work-tree").stdout.strip() != "true":
        raise SystemExit(f"locked project is not a Git worktree: {path!r}")
    head = git(repo, "rev-parse", "HEAD").stdout.strip()
    tree = git(repo, "rev-parse", "HEAD^{tree}").stdout.strip()
    target = targets.get(path)
    if target is not None:
        if target["base_revision"] != revision:
            raise SystemExit(f"patch/base lock disagreement at {path!r}")
        if head != revision and tree != target["patched_tree"]:
            raise SystemExit(
                f"locked project mismatch at {path!r}: expected base "
                f"{revision} or patched tree {target['patched_tree']}, found {head}"
            )
    else:
        if head != revision:
            raise SystemExit(
                f"locked project mismatch at {path!r}: expected {revision}, found {head}"
            )
        # Target projects receive a stricter, context-aware cleanliness check
        # below.  It is the only place where the exact apps.mk registration is
        # accepted for an already-patched SDK root.
        if (target is None and
            (git(repo, "diff", "--quiet", "--ignore-submodules", "--",
                 check=False).returncode != 0 or
             git(repo, "diff", "--cached", "--quiet", "--ignore-submodules",
                 "--", check=False).returncode != 0)):
            raise SystemExit(f"locked project has tracked changes: {path!r}")

missing_targets = sorted(set(targets) - seen_paths)
if missing_targets:
    raise SystemExit("patch projects missing from resolved SDK lock: " +
                     ", ".join(missing_targets))
if not seen_paths:
    raise SystemExit("resolved SDK lock contains no projects")
print(f"Verified {len(seen_paths)} projects from the resolved SDK lock.")
PY
    fail "SDK checkout does not match sdk/manifests/upstream-lock.xml"

declare -a PROJECT_IDS=() PROJECT_PATHS=() PROJECT_BASES=()
declare -a PROJECT_PATCHED_REVISIONS=() PROJECT_TREES=()
declare -a PROJECT_SERIES=() PROJECT_STATES=()
declare -a PROJECT_REPOS=() PROJECT_PREFLIGHTS=()
declare -a PATCH_FILES=()

load_patch_files()
{
    local series_rel=$1
    local series_file=$EDGEOS_ROOT/$series_rel
    local patch_dir
    local entry
    local -A seen=()

    [[ -f $series_file ]] || fail "missing series file: $series_rel"
    patch_dir=$(dirname -- "$series_file")
    PATCH_FILES=()
    while IFS= read -r entry || [[ -n $entry ]]; do
        entry=${entry%$'\r'}
        [[ -z $entry || $entry == \#* ]] && continue
        [[ $entry =~ ^[A-Za-z0-9][A-Za-z0-9._-]*[.]patch$ ]] ||
            fail "unsafe patch name in $series_rel: $entry"
        [[ -z ${seen[$entry]+present} ]] ||
            fail "duplicate patch in $series_rel: $entry"
        [[ -f $patch_dir/$entry ]] ||
            fail "missing patch listed by $series_rel: $entry"
        seen[$entry]=1
        PATCH_FILES+=("$patch_dir/$entry")
    done < "$series_file"
    ((${#PATCH_FILES[@]})) || fail "empty patch series: $series_rel"
}

tracked_clean()
{
    local repo=$1
    git -C "$repo" diff --quiet --ignore-submodules -- &&
        git -C "$repo" diff --cached --quiet --ignore-submodules --
}

operation_in_progress()
{
    local repo=$1
    local git_dir

    git_dir=$(git -C "$repo" rev-parse --absolute-git-dir) || return 0
    [[ -d $git_dir/rebase-apply || -d $git_dir/rebase-merge ||
       -d $git_dir/sequencer || -f $git_dir/MERGE_HEAD ||
       -f $git_dir/CHERRY_PICK_HEAD || -f $git_dir/REVERT_HEAD ||
       -f $git_dir/BISECT_LOG ]]
}

check_untracked_collisions()
{
    local id=$1
    local repo=$2
    local base=$3
    local preflight=$4

    python3 - "$id" "$repo" "$base" "$preflight" <<'PY'
import os
import pathlib
import subprocess
import sys

project_id, repo_text, base, preflight_text = sys.argv[1:]
repo = pathlib.Path(repo_text)
preflight = pathlib.Path(preflight_text)

def git(where, *arguments, check=True):
    result = subprocess.run(
        ["git", "-C", str(where), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise SystemExit(f"{project_id}: git {' '.join(arguments)} failed: {detail}")
    return result

commits = git(preflight, "rev-list", "--reverse", f"{base}..HEAD").stdout
added = set()
for commit_bytes in commits.splitlines():
    commit = commit_bytes.decode("ascii")
    output = git(
        preflight, "diff-tree", "--no-commit-id", "-r", "--name-only", "-z",
        "--no-renames", "--diff-filter=A", f"{commit}^", commit
    ).stdout
    added.update(item.decode("utf-8", "surrogateescape")
                 for item in output.split(b"\0") if item)

def base_type(path):
    result = git(preflight, "cat-file", "-t", f"{base}:{path}", check=False)
    if result.returncode == 0:
        return result.stdout.decode("ascii").strip()
    return None

def untracked_below(path):
    pathspec = ":(literal)" + path
    found = set()
    for arguments in (
        ("ls-files", "-z", "--others", "--exclude-standard", "--", pathspec),
        ("ls-files", "-z", "--others", "--ignored", "--exclude-standard",
         "--", pathspec),
    ):
        output = git(repo, *arguments).stdout
        found.update(item.decode("utf-8", "surrogateescape")
                     for item in output.split(b"\0") if item)
    return found

for path in sorted(added):
    pure = pathlib.PurePosixPath(path)
    if pure.is_absolute() or ".." in pure.parts:
        raise SystemExit(f"{project_id}: unsafe added path in patch: {path!r}")
    original_type = base_type(path)
    disk_path = repo.joinpath(*pure.parts)
    if original_type is None and os.path.lexists(disk_path):
        raise SystemExit(
            f"{project_id}: patch-added path already exists untracked: {path}"
        )
    descendants = untracked_below(path)
    if descendants and original_type != "blob":
        first = sorted(descendants)[0]
        raise SystemExit(
            f"{project_id}: patch-added path collides with untracked content: "
            f"{path} <- {first}"
        )
    for depth in range(1, len(pure.parts)):
        parent = repo.joinpath(*pure.parts[:depth])
        if os.path.lexists(parent) and (parent.is_symlink() or not parent.is_dir()):
            raise SystemExit(
                f"{project_id}: patch-added path has a blocking parent: "
                f"{'/'.join(pure.parts[:depth])} -> {path}"
            )
PY
}

expected_app_registration_change()
{
    local id=$1
    local repo=$2
    local apps_dir
    local mapping
    local -a changed_files

    [[ $id == sdk-root ]] || return 1
    apps_dir=$(cd -- "$SDK_ROOT/src/applications" 2>/dev/null && pwd -P) ||
        return 1
    [[ $(dirname -- "$EDGEOS_ROOT") == "$apps_dir" ]] || return 1
    git -C "$repo" diff --cached --quiet --ignore-submodules -- || return 1
    mapfile -t changed_files < <(
        git -C "$repo" diff --name-only --ignore-submodules --
    )
    ((${#changed_files[@]} == 1)) || return 1
    [[ ${changed_files[0]} == src/applications/apps.mk ]] || return 1

    mapping='subdirs-$(CONFIG_APP_ENABLE_LVGL_LAUNCHER) += '
    mapping+=$(basename -- "$EDGEOS_ROOT")
    git -C "$repo" show HEAD:src/applications/apps.mk |
        awk -v mapping="$mapping" '
            BEGIN { registered = 0 }
            /^[[:space:]]*subdirs-\$\(CONFIG_APP_ENABLE_LVGL_LAUNCHER\)[[:space:]]*\+=[[:space:]]*/ {
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
        ' | cmp -s - "$repo/src/applications/apps.mk"
}

PREFLIGHT_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/edgeos-sdk-patches.XXXXXX") ||
    fail "cannot create preflight directory"
cleanup()
{
    [[ -n ${PREFLIGHT_ROOT:-} && -d $PREFLIGHT_ROOT ]] &&
        rm -rf -- "$PREFLIGHT_ROOT"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'Checking EdgeOS SDK patch set against %s\n' "$SDK_ROOT"
for row in "${PROJECT_ROWS[@]}"; do
    IFS=$'\t' read -r id path base patched_revision patched_tree series_rel <<< "$row"
    repo=$(cd -- "$SDK_ROOT/$path" 2>/dev/null && pwd -P) ||
        fail "$id project path does not exist below the SDK"
    [[ $repo == "$SDK_ROOT" || $repo == "$SDK_ROOT/"* ]] ||
        fail "$id project path escapes the SDK: $path"
    for existing_repo in "${PROJECT_REPOS[@]}"; do
        [[ $repo != "$existing_repo" ]] ||
            fail "$id resolves to a duplicate project worktree: $repo"
    done
    [[ -d $repo/.git || -f $repo/.git ]] ||
        fail "$id is not a Git worktree at $repo"
    git -C "$repo" cat-file -e "$base^{commit}" 2>/dev/null ||
        fail "$id does not contain base revision $base"
    ! operation_in_progress "$repo" ||
        fail "$id has an unfinished Git operation"

    head=$(git -C "$repo" rev-parse HEAD)
    tree=$(git -C "$repo" rev-parse 'HEAD^{tree}')
    state=base
    if [[ $tree == "$patched_tree" ]]; then
        state=patched
    else
        [[ $head == "$base" ]] ||
            fail "$id revision mismatch: expected $base, found $head"
    fi
    if ! tracked_clean "$repo"; then
        if [[ $state == patched ]] &&
            expected_app_registration_change "$id" "$repo"; then
            printf '  %-24s allowing expected apps.mk registration\n' "$id"
        else
            fail "$id has tracked changes; commit or restore them before continuing"
        fi
    fi
    load_patch_files "$series_rel"
    work=$PREFLIGHT_ROOT/${#PROJECT_IDS[@]}-$id
    git clone --quiet --shared --no-checkout "$repo" "$work" ||
        fail "cannot create preflight clone for $id"
    git -C "$work" checkout --quiet --detach "$base" ||
        fail "cannot check out $id base revision"
    if ! git -C "$work" \
        -c user.name='DshanPI EdgeOS SDK' \
        -c user.email='sdk@dshanpi.com' \
        am --quiet --3way --committer-date-is-author-date \
        "${PATCH_FILES[@]}"; then
        fail "$id patch preflight failed"
    fi
    result_tree=$(git -C "$work" rev-parse 'HEAD^{tree}')
    result_head=$(git -C "$work" rev-parse HEAD)
    [[ $result_head == "$patched_revision" ]] ||
        fail "$id preflight revision mismatch: expected $patched_revision, found $result_head"
    [[ $result_tree == "$patched_tree" ]] ||
        fail "$id preflight tree mismatch: expected $patched_tree, found $result_tree"

    if [[ $state == patched ]]; then
        printf '  %-24s already patched; series reproduced (%s)\n' \
            "$id" "${head:0:12}"
    else
        printf '  %-24s applicable at %s\n' "$id" "${base:0:12}"
    fi

    PROJECT_IDS+=("$id")
    PROJECT_PATHS+=("$path")
    PROJECT_BASES+=("$base")
    PROJECT_PATCHED_REVISIONS+=("$patched_revision")
    PROJECT_TREES+=("$patched_tree")
    PROJECT_SERIES+=("$series_rel")
    PROJECT_STATES+=("$state")
    PROJECT_REPOS+=("$repo")
    PROJECT_PREFLIGHTS+=("$work")
done

for ((index = 0; index < ${#PROJECT_IDS[@]}; ++index)); do
    [[ ${PROJECT_STATES[index]} == patched ]] && continue
    if ! check_untracked_collisions \
        "${PROJECT_IDS[index]}" "${PROJECT_REPOS[index]}" \
        "${PROJECT_BASES[index]}" "${PROJECT_PREFLIGHTS[index]}"; then
        fail "${PROJECT_IDS[index]} has untracked content that would block the patch set"
    fi
done

if [[ $MODE == check ]]; then
    printf 'All %d SDK projects passed preflight; no files were changed.\n' \
        "${#PROJECT_IDS[@]}"
    exit 0
fi

printf 'Applying the complete EdgeOS SDK patch set...\n'
for ((index = 0; index < ${#PROJECT_IDS[@]}; ++index)); do
    [[ ${PROJECT_STATES[index]} == patched ]] && continue
    id=${PROJECT_IDS[index]}
    repo=$(cd -- "$SDK_ROOT/${PROJECT_PATHS[index]}" 2>/dev/null && pwd -P) ||
        fail "$id project path disappeared after preflight; earlier projects may remain patched"
    [[ $repo == "${PROJECT_REPOS[index]}" &&
       ($repo == "$SDK_ROOT" || $repo == "$SDK_ROOT/"*) ]] ||
        fail "$id project path changed after preflight; earlier projects may remain patched"
    base=${PROJECT_BASES[index]}
    expected_revision=${PROJECT_PATCHED_REVISIONS[index]}
    expected_tree=${PROJECT_TREES[index]}

    [[ $(git -C "$repo" rev-parse HEAD) == "$base" ]] ||
        fail "$id changed after preflight; earlier projects may remain patched; restore this project to the expected base and rerun --apply"
    ! operation_in_progress "$repo" ||
        fail "$id entered another Git operation after preflight; earlier projects may remain patched; finish that operation and rerun --apply"
    tracked_clean "$repo" ||
        fail "$id became dirty after preflight; earlier projects may remain patched; preserve or restore those changes and rerun --apply"
    if ! check_untracked_collisions "$id" "$repo" "$base" \
        "${PROJECT_PREFLIGHTS[index]}"; then
        fail "$id gained untracked content after preflight; earlier projects may remain patched"
    fi
    load_patch_files "${PROJECT_SERIES[index]}"
    if ! git -C "$repo" \
        -c user.name='DshanPI EdgeOS SDK' \
            -c user.email='sdk@dshanpi.com' \
            am --3way --committer-date-is-author-date "${PATCH_FILES[@]}"; then
        fail "$id apply failed; resolve it and run git am --continue, or inspect it before git am --abort; then rerun --apply (already patched projects are skipped)"
    fi
    result_head=$(git -C "$repo" rev-parse HEAD)
    result_tree=$(git -C "$repo" rev-parse 'HEAD^{tree}')
    [[ $result_head == "$expected_revision" ]] ||
        fail "$id produced an unexpected revision; earlier projects may remain patched; follow sdk/README.md recovery before rerunning --apply"
    [[ $result_tree == "$expected_tree" ]] ||
        fail "$id produced an unexpected tree; earlier projects may remain patched; follow sdk/README.md recovery before rerunning --apply"
    printf '  %-24s patched (%s)\n' "$id" \
        "$(git -C "$repo" rev-parse --short=12 HEAD)"
done

printf 'EdgeOS SDK patch set applied successfully.\n'
printf 'Next: %s/tools/integrate_canmv_sdk.sh\n' "$EDGEOS_ROOT"
printf 'Then: cd %s && make k230_canmv_dongshanpi_edgeos_defconfig\n' "$SDK_ROOT"
