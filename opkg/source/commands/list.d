module commands.list;

import std.stdio : writeln;

import util.paths : opkgDbRootPath;
import db.installed : listInstalledPackages, loadInstalledRecord;

int cmdList(const(string)[] args) {
    if (args.length != 0) {
        writeln("usage: opkg list");
        return 1;
    }
    auto dbRoot = opkgDbRootPath();
    try {
        auto pkgs = listInstalledPackages(dbRoot);
        if (pkgs.length == 0) {
            writeln("No packages installed.");
            return 0;
        }
        writeln("Installed packages:");
        foreach (name; pkgs) {
            auto rec = loadInstalledRecord(dbRoot, name);
            writeln("  - ", rec.metadata.name, " ", rec.metadata.versionString, " (", rec.metadata.arch, ")");
            writeln("      ", rec.metadata.summary);
        }
        return 0;
    } catch (Exception e) {
        writeln("opkg list: ", e.msg);
        return 1;
    }
}
