module commands.search;

import std.stdio : writeln;
import std.file : exists, readText, dirEntries, SpanMode;
import std.path : buildPath, baseName, extension;
import std.string : toLower;
import std.algorithm : canFind;

import util.paths : opkgDbRootPath;
import repo.index : parseRepoIndexJson;

int cmdSearch(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg search: missing search term");
        writeln("usage: opkg search <name>");
        return 1;
    }

    auto term = toLower(args[0].idup);
    auto dbRoot = opkgDbRootPath();
    auto reposDir = buildPath(dbRoot, "repos");
    if (!exists(reposDir)) {
        writeln("opkg search: no cached repositories, run: opkg update");
        return 1;
    }

    int matches = 0;
    foreach (entry; dirEntries(reposDir, SpanMode.shallow)) {
        if (!entry.isFile || extension(entry.name) != ".json") {
            continue;
        }
        auto repoName = baseName(entry.name)[0 .. $ - 5];
        try {
            auto idx = parseRepoIndexJson(readText(entry.name));
            foreach (pkg; idx) {
                auto hay = (pkg.name ~ " " ~ pkg.summary).toLower();
                if (hay.canFind(term)) {
                    writeln(repoName, ": ", pkg.name, " ", pkg.packageVersion, " ", pkg.arch, " - ", pkg.summary);
                    matches++;
                }
            }
        } catch (Exception e) {
            writeln("opkg search: skipped invalid repo cache ", entry.name, ": ", e.msg);
        }
    }

    if (matches == 0) {
        writeln("No matching packages found for: ", args[0]);
        return 1;
    }
    return 0;
}
