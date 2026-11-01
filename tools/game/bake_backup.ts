// NOT ORIGINAL - the backups apply_layout.ts --install keeps of the files it replaces, and --uninstall putting them back.
//
// Each install of a bake first copies every file it is about to write (the level's .scn.xbx / .col.xbx / .qb, its
// scripts .qb, the overlay's Levels.qb copies, bake_report.txt) into a new folder:
//   <game>\thug2-decomp-bake-backups\<level>\<yyyymmdd-hhmmss-mmm>[-n]\  - the copies (1.bin, 2.bin ...) and manifest.json
// The manifest names each file and whether it was there before the install (a file the install created is deleted on
// restore). Restoring takes the newest backup of the level, puts its files back and removes it - so restoring again
// takes the one before: the installs are undone one by one, newest first. The backups live outside the overlay
// (make_menu_level.ts empties the overlay, not them); restoring after a rebuild of the overlay puts back files from
// before it - the report says when a backup is older than the overlay's marker file.

import * as fs from "node:fs";
import * as path from "node:path";

export const BACKUPS = "thug2-decomp-bake-backups";

export interface BackupEntry {
    file: string; // the file's full path
    existed: boolean; // there before the install (else: the install made it)
    copy: string | null; // the copy's name in the backup folder (null when it didn't exist)
}
export interface Manifest {
    level: string;
    made: string; // ISO time
    entries: BackupEntry[];
}

// The folder a level's backups are kept in.
export function levelBackupDir(game: string, level: string): string {
    return path.join(game, BACKUPS, level.toLowerCase());
}

function stamp(now: Date): string {
    const two = (n: number) => String(n).padStart(2, "0");
    return (
        `${now.getFullYear()}${two(now.getMonth() + 1)}${two(now.getDate())}-` +
        `${two(now.getHours())}${two(now.getMinutes())}${two(now.getSeconds())}-` +
        String(now.getMilliseconds()).padStart(3, "0")
    );
}

// Copies of `files` (as they are now) into a new backup of `level`; returns the backup's folder.
export function backUp(game: string, level: string, files: string[], now: Date = new Date()): string {
    const root = levelBackupDir(game, level);
    let dir = path.join(root, stamp(now));
    for (let n = 2; fs.existsSync(dir); ++n) dir = path.join(root, `${stamp(now)}-${n}`);
    fs.mkdirSync(dir, { recursive: true });
    const entries: BackupEntry[] = [];
    const seen = new Set<string>();
    for (const file of files) {
        const full = path.resolve(file);
        if (seen.has(full.toLowerCase())) continue;
        seen.add(full.toLowerCase());
        if (fs.existsSync(full)) {
            const copy = `${entries.length + 1}.bin`;
            fs.copyFileSync(full, path.join(dir, copy));
            entries.push({ file: full, existed: true, copy });
        } else {
            entries.push({ file: full, existed: false, copy: null });
        }
    }
    const manifest: Manifest = { level, made: now.toISOString(), entries };
    fs.writeFileSync(path.join(dir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
    return dir;
}

// A level's backups, oldest first (the folders' names sort by time).
export function listBackups(game: string, level: string): string[] {
    const root = levelBackupDir(game, level);
    if (!fs.existsSync(root)) return [];
    return fs
        .readdirSync(root)
        .filter((name) => fs.existsSync(path.join(root, name, "manifest.json")))
        .sort()
        .map((name) => path.join(root, name));
}

export interface Restored {
    backup: string; // the folder restored (now removed)
    made: string;
    restored: string[]; // files put back
    deleted: string[]; // files the install had made, deleted
    left: number; // the level's backups left
}

// The newest backup of `level` put back and removed (null: none left).
export function restoreLatest(game: string, level: string): Restored | null {
    const backups = listBackups(game, level);
    const latest = backups.at(-1);
    if (!latest) return null;
    const manifest = JSON.parse(fs.readFileSync(path.join(latest, "manifest.json"), "utf8")) as Manifest;
    const restored: string[] = [];
    const deleted: string[] = [];
    for (const entry of manifest.entries) {
        if (entry.existed && entry.copy) {
            fs.mkdirSync(path.dirname(entry.file), { recursive: true });
            fs.copyFileSync(path.join(latest, entry.copy), entry.file);
            restored.push(entry.file);
        } else if (fs.existsSync(entry.file)) {
            fs.rmSync(entry.file);
            deleted.push(entry.file);
        }
    }
    fs.rmSync(latest, { recursive: true });
    return { backup: latest, made: manifest.made, restored, deleted, left: backups.length - 1 };
}
