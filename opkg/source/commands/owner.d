module commands.owner;

import std.stdio : writeln;
import util.paths : opkgDbRootPath;
import util.fs : normalizeManifestPath;
import db.fileowners : getFileOwner;

int cmdOwner(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg owner: missing file path");
        writeln("usage: opkg owner <path>");
        return 1;
    }
    auto dbRoot = opkgDbRootPath();
    auto path = normalizeManifestPath(args[0].idup);
    auto owner = getFileOwner(dbRoot, path);
    if (owner.length == 0) {
        writeln(path, ": not owned by any installed package");
        return 1;
    }
    writeln(path, " -> ", owner);
    return 0;
}
