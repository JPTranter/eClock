# Development Session — 2026-08-28 (UF2 root cause found on-device, release hygiene)

## Session Summary

Root-caused why the release UF2 never flashed, verified the drag-and-drop path
end-to-end on the physical board, fixed the converter and the release notes,
cleaned up the bad old release, and shipped a fresh release whose UF2 was
downloaded and flashed to hardware to prove integrity.

## The core discovery: the UF2 format must be canonical

The repo's `hex_to_uf2.py` was writing a NON-CANONICAL 36-byte header variant:
familyID at byte 32, magic-end at bytes 36-43, payload starting at byte 44.
The bootloader silently ignored such files: the copy landed on `XIAO-BOOT`, but
**no flash and no reset** — nothing happened, which is why "UF2 never worked".

The fix is the **canonical spec layout**:
- 8-word header (32 bytes): start0, start1, flags, targetAddr, payloadSize,
  blockNo, numBlocks, familyID (familyID at byte 24, NOT 32).
- Payload (256 bytes) starts at byte 32 — the vector table's stack pointer is
  the first thing there.
- `magicEnd` (`0x0AB16F30`) at byte 508. NOT inline after the header.

Once the converter wrote canonical blocks, the same firmware image flashed and
auto-rebooted **on the first attempt**.

## Verified on hardware (2026-08-28)

- Board: Seeed XIAO nRF52840 Plus, factory Seeed UF2 bootloader
  `0.9.2-29-g6a9a6a3`, SoftDevice S140 7.3.0, app region `0x27000`.
- `D:/CURRENT.UF2` is the bootloader's dump of the ENTIRE flash (MBR + S140 +
  bootloader + app), 1.9 MB. Never copy over it — it is the restore image.
- The bootloader **auto-reboots into the app ~1 s after a complete, valid UF2
  write**. The filename does NOT matter (spec: only a file named `CURRENT.UF2`
  is refused). The "rename to FLASH.UF2 (8.3, all-caps)" advice everywhere in
  the docs/release notes was a myth — the bootloader proved it by flashing a
  file named `FLASH.UF2`, and the spec says name is irrelevant.
- The app's USB CDC is normally **quiet** on boot because the shipped firmware
  removes `Serial.begin()` for power saving (LESSONS_LEARNT.md §9). Presence of
  the app CDC port (VID 2886 / PID 8045) alone confirms the reboot.

## The on-machine quirks that bit us

1. **Stale `FLASH.UF2` on the drive breaks overwriting.** After one (ignored)
   attempt, copying again failed with `OSError: [Errno 9] Bad file descriptor`.
   Removing the stale file from the drive fixed it. The flash tool deletes a
   pre-existing `FLASH.UF2` before copying.
2. **The bootloader drive only mounts sometimes.** It mounted this session (so
   we could test), but the old notes about it not remounting are still real on
   this machine — serial DFU remains the most repeatable path overall.
3. **The old `hex_to_uf2.py` wrote output to paths it could not create** (no
   `build/` dir). Added `os.makedirs(dirname(out), exist_ok=True)`.

## New tool: firmware/tools/uf2_flash.py

Backup + copy + verify in one command:

```bash
python tools/uf2_flash.py <file>.uf2
```

1. Backs up `D:/CURRENT.UF2` (whole-flash) to `firmware/backups/`.
2. Deletes any stale `FLASH.UF2` on the drive.
3. Copies the .uf2 as `FLASH.UF2` (fsync'd).
4. Watches for the app CDC port (PID 8045) to appear = flash + auto-reboot
   confirmed. Exit 0 only on that.

## Release hygiene

- The old release **v0.1.0** shipped a UF2 from the broken converter — its
  firmware was unflashable via drag-and-drop. Deleted the GitHub release and its
  tag (the tag contains the bad artifact forever; deleting the tag removes it).
- `v0.2.0` is the first release whose `.uf2` was **downloaded from GitHub and
  flashed to the physical board** via the new tool — real integrity proof, not
  just a header check.

## Next steps

- Bench power measurements remain the project's #1 risk
  (`docs/research/power-budget-analysis.md`).
- Enclosure, refresh-interval decision, daytime full-refresh decision —
  unchanged from STATUS.md.

## Files changed

- `firmware/include/clock_version.h` — version `0.2.0` (was `uf2test`).
- `firmware/tools/hex_to_uf2.py` — canonical UF2 layout (familyID@24,
  magicEnd@508), makedirs fix.
- `firmware/tools/uf2_flash.py` — new: backup + copy + watch for auto-reboot.
- `docs/SETUP.md`, `README.md`, `docs/STATUS.md`, `.github/workflows/release.yml`
  — replaced stale "UF2 doesn't work / rename 8.3" guidance.
- `.gitignore` — ignore `firmware/backups/`.
