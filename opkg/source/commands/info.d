module commands.info;

import std.stdio : writeln;
import std.array : join;

import util.paths : opkgDbRootPath;
import db.installed : loadInstalledRecord;

int cmdInfo(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg info: missing package name");
        writeln("usage: opkg info <pkg>");
        return 1;
    }
    auto pkgName = args[0].idup;
    auto dbRoot = opkgDbRootPath();
    try {
        auto rec = loadInstalledRecord(dbRoot, pkgName);
        auto m = rec.metadata;
        writeln("Name: ", m.name);
        writeln("Version: ", m.versionString);
        writeln("Arch: ", m.arch);
        writeln("Summary: ", m.summary);
        writeln("Description: ", m.description);
        writeln("Maintainer: ", m.maintainer);
        writeln("Section: ", m.section);
        if (m.depends.length > 0) writeln("Depends: ", m.depends.join(", "));
        if (m.provides.length > 0) writeln("Provides: ", m.provides.join(", "));
        if (m.conflicts.length > 0) writeln("Conflicts: ", m.conflicts.join(", "));
        writeln("Installed files: ", rec.installedFiles.length);
        return 0;
    } catch (Exception e) {
        writeln("opkg info: ", e.msg);
        return 1;
    }
}
