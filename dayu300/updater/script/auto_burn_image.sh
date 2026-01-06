# Copyright (c) 2025 Phytium Technology Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/sh
set -u

# Wait a bit for devices to settle

IMAGE_NAME="openharmony_img_no_uboot.img"
MNT_BASE="/tmp/updater_mounts"
mkdir -p "$MNT_BASE"

TMP_DIRS=""
cleanup() {
    if [ -n "$TMP_DIRS" ]; then
        for d in $TMP_DIRS; do
            umount "$d" 2>/dev/null || true
            [ -d "$d" ] && rmdir "$d" 2>/dev/null || true
        done
    fi
}

trap cleanup EXIT

# CLI options
DRY_RUN=0
AUTO_YES=0
usage() {
    cat <<EOF
Usage: $(basename "$0") [--dry-run|-n] [--yes|-y] [--help|-h]
  --dry-run, -n   Show actions but do not write image or reboot
  --yes, -y       Skip interactive confirmation and proceed
  --help, -h      Show this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run|-n)
            DRY_RUN=1
            shift
            ;;
        --yes|-y)
            AUTO_YES=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

# helper: get device node path (/dev/block/<name> if exists, else /dev/<name>)
devnode() {
    name="$1"
    if [ -e "/dev/block/$name" ]; then
        echo "/dev/block/$name"
    else
        echo "/dev/$name"
    fi
}

# helper: compute parent device name for a partition basename (sda2 -> sda; nvme0n1p2 -> nvme0n1)
parent_of() {
    part_basename="$1"
    case "$part_basename" in
        *p[0-9]*) parent="${part_basename%p[0-9]*}" ;;
        *) parent="${part_basename%[0-9]*}" ;;
    esac
    echo "$parent"
}

# Collect candidate block devices
DEVICES=""
for sysd in /sys/block/*; do
    [ -d "$sysd" ] || continue
    name=$(basename "$sysd")
    case "$name" in
        sd*|nvme*|mmcblk*) DEVICES="$DEVICES $name" ;;
        *) ;;
    esac
done

# Use shell word splitting to test for emptiness (avoid external awk)
set -- $DEVICES
if [ $# -eq 0 ]; then
    echo "No candidate block devices found. Exiting." >&2
    exit 1
fi

echo "Found block devices: $*"

# For each partition of each device, try mounting and searching for the image
SRC_PATH=""
SRC_PARENT_DEVICE=""
FOUND=0
for dev in $DEVICES; do
    # list partition device nodes (prefer /dev/block/...), pattern might be e.g. sda1, sda2 or nvme0n1p1
    devpath_prefix=$(devnode "$dev" | sed 's#/dev/##')
    # try globbing for partition nodes
    for partdev in /dev/${dev}* /dev/block/${dev}*; do
        [ -e "$partdev" ] || continue
        # skip the device node itself (exact match)
        base=$(basename "$partdev")
        if [ "$base" = "$dev" ]; then
            continue
        fi

        # try to mount read-only to temp dir
        mntdir=$(mktemp -d "$MNT_BASE/${dev}_XXXXXX" 2>/dev/null) || mntdir=""
        if [ -z "$mntdir" ]; then
            echo "mktemp failed for device $dev; skipping partition $partdev" >&2
            continue
        fi
        if mount -o ro -t auto "$partdev" "$mntdir" 2>/dev/null; then
            TMP_DIRS="$TMP_DIRS $mntdir"
            echo "Mounted $partdev -> $mntdir"
            # search for image file
            if [ -f "$mntdir/$IMAGE_NAME" ]; then
                SRC_PATH="$mntdir/$IMAGE_NAME"
                SRC_PARENT_DEVICE="$dev"
                echo "Found image at $SRC_PATH on device $dev"
                FOUND=1
                break
            fi
            # also search subdirectories
            found=$(find "$mntdir" -maxdepth 3 -type f -name "$IMAGE_NAME" 2>/dev/null | head -n1 || true)
            if [ -n "$found" ]; then
                SRC_PATH="$found"
                SRC_PARENT_DEVICE="$dev"
                echo "Found image at $SRC_PATH on device $dev"
                FOUND=1
                break
            fi
        else
            # not mountable (skip)
            rmdir "$mntdir" 2>/dev/null || true
        fi
    done
    if [ "$FOUND" -eq 1 ]; then
        break
    fi
done

if [ -z "$SRC_PATH" ]; then
    echo "No $IMAGE_NAME found on any mounted partition. Exiting." >&2
    exit 2
fi

# Determine target device according to rules:
# 1) Device with >=4 partitions and labels: 2->usr, 3->vendor, 4->data
# 2) Else, an unpartitioned disk (no partition children)
# 3) Else, randomly choose a device

TARGET_DEVICE=""

# Build candidate list excluding the device that contains the source image
CANDIDATES=""
for d in $DEVICES; do
    if [ -n "$SRC_PARENT_DEVICE" ] && [ "$d" = "$SRC_PARENT_DEVICE" ]; then
        continue
    fi
    CANDIDATES="$CANDIDATES $d"
done

# If no other candidate devices, abort to avoid accidental overwrite
if [ -z "$CANDIDATES" ]; then
    echo "No candidate target devices available (only source present). Exiting." >&2
    exit 3
fi

# 1) Prefer a device with >=4 partitions (common target disk)
for dev in $CANDIDATES; do
    pcount=0
    for p in /dev/${dev}* /dev/block/${dev}*; do
        [ -e "$p" ] || continue
        b=$(basename "$p")
        [ "$b" = "$dev" ] && continue
        pcount=$((pcount + 1))
    done
    if [ $pcount -ge 4 ]; then
        TARGET_DEVICE="$dev"
        echo "Selected target device $dev by partition-count rule"
        break
    fi
done

# 2) Else choose an unpartitioned disk
if [ -z "$TARGET_DEVICE" ]; then
    for dev in $CANDIDATES; do
        has_part=0
        for p in /sys/block/$dev/${dev}*; do
            [ -e "$p" ] || continue
            name=$(basename "$p")
            if [ "$name" != "$dev" ]; then
                has_part=1
                break
            fi
        done
        if [ $has_part -eq 0 ]; then
            TARGET_DEVICE="$dev"
            echo "Selected unpartitioned device $dev as target"
            break
        fi
    done
fi

# 3) Fallback: random choice among candidates
if [ -z "$TARGET_DEVICE" ]; then
    set -- $CANDIDATES
    count=$#
    if [ $count -gt 0 ]; then
        t=$(date +%s 2>/dev/null || printf "%s" "$(date)")
        idx=$((t % count))
        i=0
        for d in $CANDIDATES; do
            if [ $i -eq $idx ]; then
                TARGET_DEVICE="$d"
                break
            fi
            i=$((i + 1))
        done
        echo "No better target found; randomly selected $TARGET_DEVICE"
    fi
fi

# Ensure no other disks exist except source-parent and target
extra_count=0
for d in $DEVICES; do
    if [ "$d" = "$SRC_PARENT_DEVICE" ] || [ "$d" = "$TARGET_DEVICE" ]; then
        continue
    fi
        extra_count=$((extra_count + 1))
done
if [ $extra_count -gt 0 ]; then
    echo "Found other disk nodes besides source and target. Exiting to avoid accidental overwrite." >&2
    exit 3
fi

TARGET_NODE=$(devnode "$TARGET_DEVICE")

echo "Using image source: $SRC_PATH"
echo "Target block device: $TARGET_NODE"

if [ "$AUTO_YES" -eq 1 ]; then
    ans=YES
else
    printf "Proceed to write %s to %s? (type YES to continue): " "$SRC_PATH" "$TARGET_NODE"
    read ans
fi
if [ "$ans" != "YES" ]; then
    echo "Aborted by user.";
    exit 4
fi

echo "Starting dd..."
if [ "$DRY_RUN" -eq 1 ]; then
    echo "DRY RUN: would run: dd if=\"$SRC_PATH\" of=\"$TARGET_NODE\" bs=4M conv=fsync"
    echo "DRY RUN: would run: sync"
    echo "DRY RUN: would reboot the device"
    exit 0
else
    if dd if="$SRC_PATH" of="$TARGET_NODE" bs=4M conv=fsync; then
        echo "dd completed successfully. Running sync..."
        sync
        echo "Sleeping 3s then rebooting..."
        sleep 3
        reboot
    else
        echo "dd failed." >&2
        exit 5
    fi
fi

