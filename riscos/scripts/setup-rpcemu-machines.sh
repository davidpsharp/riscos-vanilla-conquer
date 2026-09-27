#!/usr/bin/env bash
# Create the StrongARM Risc PC test machines in RPCEmu Extended:
#   "VC SA RO371": RISC OS 3.71, cloned from the "RISC OS 3.71" machine
#   "VC SA RO530": RISC OS 5.30, cloned from "RISC OS 5.30" (see --fetch-riscos)
# Each machine's HostFS gets a "vc" link to ~/vcport-work/hostfs (outside the
# Synology-synced source tree).
# Re-running refreshes the config but keeps the existing disc and CMOS.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DATADIR=${RPCEMU_DATADIR:-$HOME/rpcemu/rpcemu-extended}
MEM=${VC_MEM_MB:-128}
HOSTFS=${VC_WORK:-$HOME/vcport-work}/hostfs
mkdir -p "$HOSTFS"

# make_machine <name> <template> <rom> <vnc port> <hostcmd port> <mac>
make_machine() {
    local name=$1 template=$2 rom=$3 vnc=$4 hostcmd=$5 mac=$6
    local mdir="$DATADIR/machines/$name" tdir="$DATADIR/machines/$template"
    [ -d "$tdir" ] || { echo "template machine '$template' missing" >&2; exit 1; }
    mkdir -p "$mdir/hostfs"
    [ -f "$mdir/cmos.ram" ] || cp "$tdir/cmos.ram" "$mdir/"
    if [ -f "$tdir/hd4.hdf" ] && [ ! -f "$mdir/hd4.hdf" ]; then
        cp "$tdir/hd4.hdf" "$mdir/"
    fi
    # Copy the template's HostFS contents (RISC OS 5 boots from HostFS).
    if [ -z "$(ls -A "$mdir/hostfs" 2>/dev/null | grep -v '^vc$')" ]; then
        cp -R "$tdir/hostfs/." "$mdir/hostfs/" 2>/dev/null || true
    fi
    ln -sfn "$HOSTFS" "$mdir/hostfs/vc"
    cat > "$DATADIR/configs/$name.cfg" <<CFG
[General]
name=$name
hd4_path=
hostfs_path=
rom_dir=$rom
mem_size=$MEM
model=RPCSA
vram_size=2
sound_enabled=1
refresh_rate=60
cdrom_enabled=0
mouse_following=1
network_type=off
macaddress=$mac
cpu_idle=0
show_fullscreen_message=0
screen_size_x=800
screen_size_y=600
suspend_on_exit=0
vnc_enabled=1
vnc_port=$vnc
hostcmd_enabled=1
hostcmd_socket=$hostcmd
clipboard_enabled=0
netcap_enabled=0
debug_enabled=1
debug_socket=
CFG
    echo "machine '$name': vnc $vnc, hostcmd tcp:127.0.0.1:$hostcmd, hostfs $mdir/hostfs"
}

make_machine "VC SA RO371" "RISC OS 3.71" ROM371 5920 5921 52:56:43:00:03:71
make_machine "VC SA RO530" "RISC OS 5.30" ROM530 5930 5931 52:56:43:00:05:30
