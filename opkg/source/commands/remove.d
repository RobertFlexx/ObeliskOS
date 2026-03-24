module commands.remove;

import std.stdio : writeln;
import std.file : exists, remove;

import util.paths : opkgRootPath, opkgDbRootPath;
import util.fs : ensureAbsoluteInsideRoot;
import db.installed : loadInstalledRecord, removeInstalledRecord;
import db.fileowners : loadFileOwners, saveFileOwners;

int cmdRemove(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg remove: missing package name");
        writeln("usage: opkg remove <pkg>");
        return 1;
    }

    auto pkgName = args[0].idup;
    auto root = opkgRootPath();
    auto dbRoot = opkgDbRootPath();

    try {
        auto rec = loadInstalledRecord(dbRoot, pkgName);
        auto owners = loadFileOwners(dbRoot);

        foreach (path; rec.installedFiles) {
            if (!(path in owners)) {
                continue;
            }
            if (owners[path] != pkgName) {
                writeln("opkg remove: refusing to remove ", path, " (owned by ", owners[path], ")");
                return 1;
            }
        }

        foreach (path; rec.installedFiles) {
            auto absPath = ensureAbsoluteInsideRoot(root, path);
            if (exists(absPath)) {
                remove(absPath);
            }
            if (path in owners) {
                owners.remove(path);
            }
        }

        saveFileOwners(dbRoot, owners);
        removeInstalledRecord(dbRoot, pkgName);
        writeln("Removed ", pkgName);
        return 0;
    } catch (Exception e) {
        writeln("opkg remove: ", e.msg);
        return 1;
    }
}
