module commands.files;

import std.stdio : writeln;

import util.paths : opkgDbRootPath;
import db.installed : loadInstalledRecord;

int cmdFiles(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg files: missing package name");
        writeln("usage: opkg files <pkg>");
        return 1;
    }
    auto pkgName = args[0].idup;
    auto dbRoot = opkgDbRootPath();
    try {
        auto rec = loadInstalledRecord(dbRoot, pkgName);
        foreach (path; rec.installedFiles) {
            writeln(path);
        }
        return 0;
    } catch (Exception e) {
        writeln("opkg files: ", e.msg);
        return 1;
    }
}
