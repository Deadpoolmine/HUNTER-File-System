#!/usr/bin/bash

#SECTION: Color Preset
CLR_BLACK="\033[30m"
CLR_RED="\033[31m"
CLR_GREEN="\033[32m"
CLR_YELLOW="\033[33m"
CLR_BLUD="\033[34m"
CLR_PURPLE="\033[35m"
CLR_BLUE="\033[36m"
CLR_GREY="\033[37m"

CLR_END="\033[0m"
#!SECTION

ORIGINAL=$PWD
WORK_DIR=$(dirname "$0")
MNT_POINT=/mnt/pmem0

# build project
cd "$WORK_DIR" || exit
# parse config
if [ ! "$1" ]; then
    config_path="./config.example.json"
else
    config_path="$1"
fi

config_json=$(cat "$config_path")

function get_build_options() {
    echo "$config_json" | jq -r ".build.$1"
}

function get_modules_options() {
    echo "$config_json" | jq -r ".modules.$1"
}

function get_fs_options() {
    echo "$config_json" | jq -r ".fs.$1"
}

HK_ENABLE_LFS=$(get_build_options HK_ENABLE_LFS)
HK_ENABLE_ASYNC=$(get_build_options HK_ENABLE_ASYNC)
HK_ENABLE_IDX_ALLOC_PREDICT=$(get_build_options HK_ENABLE_IDX_ALLOC_PREDICT)
HK_ENABLE_DECOUPLE_WORKER=$(get_build_options HK_ENABLE_DECOUPLE_WORKER)
HK_CHECKPOINT_INTERVAL=$(get_build_options HK_CHECKPOINT_INTERVAL)

sudo make -j"$(nproc)" HK_ENABLE_LFS="$HK_ENABLE_LFS" HK_ENABLE_ASYNC="$HK_ENABLE_ASYNC" HK_ENABLE_IDX_ALLOC_PREDICT="$HK_ENABLE_IDX_ALLOC_PREDICT" HK_ENABLE_DECOUPLE_WORKER="$HK_ENABLE_DECOUPLE_WORKER" HK_CHECKPOINT_INTERVAL="$HK_CHECKPOINT_INTERVAL"
sudo dmesg -C

fs_init=$(get_fs_options init)
fs_wprotect=$(get_fs_options wprotect)

init_str=""

if ((fs_init == 1)); then
    init_str+="init"
fi

if ((fs_wprotect == 1)); then
    init_str+=",wprotect"
fi

# inserting
echo "umounting..."
sudo umount $MNT_POINT

echo "Removing the old kernel module..."
sudo rmmod hunter >/dev/null 2>&1

echo "Inserting the new kernel module..."
sudo insmod hunter.ko \
    measure_timing="$(get_modules_options measure_timing)" \
    wprotect="$(get_modules_options wprotect)"

sleep 1

echo "Mounting..."

sudo mount -t HUNTER -o "$init_str" -o dax /dev/pmem0 $MNT_POINT
echo "Mount with configs: "
echo "$config_json" | jq
echo -e "$CLR_GREEN""> HK_ENABLE_LFS: $HK_ENABLE_LFS""$CLR_END"
echo -e "$CLR_GREEN""> HK_ENABLE_ASYNC: $HK_ENABLE_ASYNC""$CLR_END"
echo -e "$CLR_GREEN""> HK_ENABLE_IDX_ALLOC_PREDICT: $HK_ENABLE_IDX_ALLOC_PREDICT""$CLR_END"
echo -e "$CLR_GREEN""> HK_ENABLE_DECOUPLE_WORKER: $HK_ENABLE_DECOUPLE_WORKER""$CLR_END"
echo -e "$CLR_GREEN""> HK_CHECKPOINT_INTERVAL: $HK_CHECKPOINT_INTERVAL""$CLR_END"
echo -e "$CLR_GREEN""> HUNTER Mounted!""$CLR_END"
cd "$ORIGINAL" || exit
