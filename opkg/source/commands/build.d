module commands.build;

import std.stdio : writeln;
import std.path : buildPath, baseName;
import std.exception : enforce;
import opkgpkg.archive : packOpkV1;

int cmdBuild(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg build: missing package directory");
        writeln("usage: opkg build <dir> [out.opk]");
        return 1;
    }
    auto srcDir = args[0].idup;
    auto outPath = args.length >= 2 ? args[1].idup : buildPath(srcDir, baseName(srcDir) ~ ".opk");
    try {
        packOpkV1(srcDir, outPath);
        writeln("Built ", outPath);
        return 0;
    } catch (Exception e) {
        writeln("opkg build: ", e.msg);
        return 1;
    }
}
