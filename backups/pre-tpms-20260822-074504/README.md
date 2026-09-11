# Pre-TPMS Backup 20260822-074504

This backup marks the working tree before adding DJTPMS support.

Contents:
- `tracked-changes.patch`: binary-capable patch for tracked file changes.
- `gsm_stub.h`, `vlink_ble_stream.cpp`, `vlink_ble_stream.h`: untracked source files present before TPMS work.
- `.vscode/`: untracked editor settings present before TPMS work.

Restore notes:
- Review the current working tree before applying this backup.
- Apply tracked changes with `git apply backups/pre-tpms-20260822-074504/tracked-changes.patch`.
- Copy the saved untracked files back into their original locations if needed.
