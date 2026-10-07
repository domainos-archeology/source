#!/usr/bin/env python3
"""mame_boot_direct.py - start our RFC image under MAME without sysboot.

The DN300 PROM in the emulator parks in its console input loop (PROM 0x604)
and never reaches the disk, and the emulator's gdbstub crashes on memory
packets, so this harness runs MAME headless with the Lua script
tools/mame_boot_direct.lua, which does what sysboot would have done: after
the PROM has initialised the hardware it writes the image's pages into RAM
with sysboot's placement (file page i at PPN 0x405 + i, pages at or above
the split moved to MOVE_TO_PPN + ...), builds the frame COLD_START pops
(docs/rfc-cold-start.md section 5), points PC at 0x101424 and runs.
Breakpoints on the kernel's crash and fault entries log a register and
stack dump to MAME's error.log; progress breakpoints log one line.  The
script stops when the PC stays put for two seconds or the frame budget
runs out, and prints where the CPU is.

    tools/mame_boot_direct.py --clean-volume --last-valid 0x4F900000
    tools/mame_boot_direct.py --image ~/src/domainos-archeology/sr10.2-install/install/ri.apollo.os.v.10.2/sau2/domain_os \
        --map ~/src/domainos-archeology/sau2-maps/domain_os.10.2.map   # the original as an oracle

Everything MAME writes (error.log, cfg, nvram) and the working-copy disk live
under <repo>/tmp/boot; the MAME tree is only read (binary, roms).
"""
import argparse
import os
import re
import subprocess
import sys

STOP_SYMBOLS = ['CRASH_SYSTEM', 'FIM_$CRASH', 'FIM_$BUS_ERR', 'FIM_$UII',
                'FIM_$PRIV_VIOL', 'FIM_$FLINE', 'FIM_$SPURIOUS_INT',
                'FIM_$PARITY_TRAP']
PROGRESS_SYMBOLS = ['OS_$INIT', 'OS_$START_PROC2', 'MST_$INIT', 'AREA_$INIT',
                    'MMAP_$INIT', 'AST_$INIT', 'PROC1_$INIT', 'TIME_$INIT',
                    'FIM_$INIT', 'DISK_$INIT', 'PMAP_$INIT', 'MMU_$INIT',
                    'NETWORK_$INIT', 'FILE_$INIT', 'VTOC_$INIT']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.path.dirname(here)
    ap.add_argument('--image', default=os.path.join(here, 'dist/sau2/domain_os'))
    ap.add_argument('--elf', default=os.path.join(here, 'build/sau2/domain_os.elf'))
    ap.add_argument('--mame-dir', default=os.path.expanduser('~/src/domainos-archeology/mame'))
    ap.add_argument('--mame-bin', default='./phoebus')
    ap.add_argument('--mame-args', default='dn300', help="machine (default dn300); the disk comes from --disk")
    ap.add_argument('--disk', default=os.path.join(repo, 'tmp/boot/dn300_sr10.3_ours.awd'),
                    help='the boot disk working copy (.awd); the kernel writes to it')
    ap.add_argument('--work-dir', default=os.path.join(repo, 'tmp/boot'),
                    help="MAME's working directory: error.log, cfg/, nvram/ land here, not in the MAME tree")
    ap.add_argument('--map', default=None, help='take STOP/PROGRESS symbols from this SAU2 link map instead of --elf '
                    '(for running the original 10.2 image as an oracle)')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--flags', type=lambda s: int(s, 0), default=0x0008)
    ap.add_argument('--out-dir', default=None, help='logs and dumps (default: the work dir)')
    ap.add_argument('--extra-stop', action='append', default=[])
    ap.add_argument('--extra-progress', action='append', default=[])
    ap.add_argument('--clean-volume', metavar='AWD', nargs='?', const='disk', help='before launching, clear the logical-volume label\'s '
                    'salvage flag (block 1 data byte 0xCE) in this .awd working copy: the kernel marks the volume '
                    'dirty at mount and the harness never dismounts, so each run would otherwise refuse the volume')
    ap.add_argument('--last-valid', type=lambda s: int(s, 0), default=None,
                    help='with --clean-volume: also set the label\'s last valid calendar time (0xE6) so CAL_$VERIFY '
                    'does not prompt (the emulated RTC runs decades behind the owner\'s runs)')
    ap.add_argument('--bp', action='append', default=[], help="raw MAME bpset arguments, e.g. '0xE2064E,(d6&0xffff)!=0x2700,{logerror \"...\";go}'")
    args = ap.parse_args()

    if args.out_dir is None:
        args.out_dir = args.work_dir
    os.makedirs(args.out_dir, exist_ok=True)
    os.makedirs(args.work_dir, exist_ok=True)
    if args.clean_volume:
        # .awd: 1056-byte blocks, 32-byte header; the LV label is block 1
        path = args.disk if args.clean_volume in ('', '-', 'disk') else args.clean_volume
        with open(path, 'r+b') as awd:
            awd.seek(1 * 1056 + 32 + 0xCE)
            awd.write(b'\x00\x00')
            if args.last_valid is not None:
                awd.seek(1 * 1056 + 32 + 0xE6)
                awd.write(args.last_valid.to_bytes(4, 'big'))
        print('label of %s: salvage flag cleared%s' % (path, '' if args.last_valid is None else ', last valid time 0x%08X' % args.last_valid))

    syms = {}
    if args.map:
        # the SAU2 link map: '     E0A458  FIM_$BUILD_DF' lines (code and data)
        nm_lines = []
        for line in open(args.map):
            m = re.match(r'^\s*(?:[A-Z]\d*\s+)?([0-9A-F]{6})\s+(\S+)', line)
            if m and not m.group(2).startswith('size'):
                syms.setdefault(m.group(2), int(m.group(1), 16))
                nm_lines.append('%08x T %s' % (int(m.group(1), 16), m.group(2)))
        nm = '\n'.join(nm_lines) + '\n'
    else:
        nm = subprocess.check_output(['m68k-elf-nm', args.elf]).decode()
        for line in nm.splitlines():
            p = line.split()
            if len(p) == 3 and p[1] in 'Tt':
                syms[p[2]] = int(p[0], 16)
    out_dir = os.path.abspath(args.out_dir)
    syms_path = os.path.join(out_dir, 'mame_boot.syms')
    open(syms_path, 'w').write(nm)

    def addrs(names):
        return ','.join(str(syms[n] if n in syms else int(n, 16)) for n in names
                        if n in syms or n.lower().startswith('0x'))

    # the PROM's exception handlers: until FIM_$INIT installs the kernel's
    # vectors, the trap page COLD built holds the ROM's (docs section 5,
    # step 10), so an early fault lands in the PROM; break there and log the
    # 68010 exception frame (sr, pc, format/vector word)
    rom = open(os.path.join(args.mame_dir, 'roms/dn300/300_BOOT.bin'), 'rb').read()
    handlers = sorted({int.from_bytes(rom[v * 4:v * 4 + 4], 'big') for v in range(2, 256)})
    handlers = [h for h in handlers if 0 < h < len(rom)]
    print('PROM exception handlers:', ' '.join('%X' % h for h in handlers))

    lua = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mame_boot_direct.lua')
    env = dict(os.environ, DYLD_FRAMEWORK_PATH='/Library/Frameworks', SDL_VIDEODRIVER='dummy',  # no window on macOS
               DOS_IMAGE=os.path.abspath(args.image), DOS_SYMS=syms_path,
               DOS_STOPS=addrs(STOP_SYMBOLS + args.extra_stop),
               DOS_PROGRESS=addrs(PROGRESS_SYMBOLS + args.extra_progress),
               DOS_FLAGS=str(args.flags), DOS_FRAMES=str(args.frames),
               DOS_PROM_HANDLERS=','.join(str(h) for h in handlers),
               DOS_RAW_BPS='|'.join(args.bp))
    mame_bin = args.mame_bin if os.path.isabs(args.mame_bin) else os.path.join(args.mame_dir, args.mame_bin)
    cmd = [mame_bin] + args.mame_args.split() + [
        '-disk1', os.path.abspath(args.disk),
        '-rompath', os.path.join(args.mame_dir, 'roms'),
        '-cfg_directory', os.path.join(args.work_dir, 'cfg'),
        '-nvram_directory', os.path.join(args.work_dir, 'nvram'),
        '-video', 'none', '-sound', 'none', '-log', '-skip_gameinfo', '-nothrottle',
        '-debug', '-debugger', 'none', '-seconds_to_run', str(args.frames // 60 + 30),
        '-autoboot_script', lua]
    elog = os.path.join(args.work_dir, 'error.log')
    if os.path.exists(elog):
        os.remove(elog)
    res = subprocess.run(cmd, cwd=args.work_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    text = res.stdout.decode(errors='replace')
    open(os.path.join(out_dir, 'mame_boot.mame.out'), 'w').write(text)
    for line in text.splitlines():
        if line.startswith('BOOT ') or line.startswith('D0=') or line.startswith('A0=') or 'rror' in line:
            print(line)
    print('mame exit code', res.returncode)
    if os.path.exists(elog):
        lines = open(elog, errors='replace').read().splitlines()
        keep = [l for l in lines if 'PROGRESS' in l or 'STOP' in l or 'EXC' in l or 'BP ' in l or 'bus_error' in l or 'translate' in l]
        print('error.log: %d lines, %d of interest; first 40:' % (len(lines), len(keep)))
        for l in keep[:40]:
            print('  ' + l)
        if len(keep) > 40:
            print('  ... last 10:')
            for l in keep[-10:]:
                print('  ' + l)


if __name__ == '__main__':
    main()
