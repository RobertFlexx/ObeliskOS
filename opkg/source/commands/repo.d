module commands.repo;

import std.stdio : writeln;
import std.file : exists, mkdirRecurse, write, dirEntries, SpanMode;
import std.path : buildPath, baseName, extension;
import std.array : appender;
import std.json : JSONValue;

import util.paths : opkgRepoConfigPath;
import repo.config : loadRepoConfig;
import opkgpkg.archive : unpackOpkV1, cleanupUnpacked;
import util.hash : sha256Hex;

private void printUsage() {
    writeln("usage:");
    writeln("  opkg repo                 List configured repositories");
    writeln("  opkg repo init <dir>      Create a static repo layout");
    writeln("  opkg repo index <dir>     Rebuild index.json from packages/*.opk");
}

private string jsonArrayOfStrings(const(string)[] values) {
    auto b = appender!string();
    b.put("[");
    foreach (i, v; values) {
        if (i > 0) b.put(",");
        b.put(JSONValue(v).toString());
    }
    b.put("]");
    return b.data;
}

private int repoInit(string dir) {
    auto pkgDir = buildPath(dir, "packages");
    mkdirRecurse(pkgDir);
    auto idxPath = buildPath(dir, "index.json");
    if (!exists(idxPath)) {
        write(idxPath, "[]\n");
    }
    auto readmePath = buildPath(dir, "README.txt");
    if (!exists(readmePath)) {
        write(
            readmePath,
            "This is an opkg static repo.\n"
            ~ "Required layout:\n"
            ~ "  index.json\n"
            ~ "  packages/*.opk\n"
            ~ "Run: opkg repo index " ~ dir ~ "\n"
        );
    }
    writeln("==> initialized repo at ", dir);
    writeln("==> packages dir: ", pkgDir);
    writeln("==> index path:   ", idxPath);
    return 0;
}

private int repoIndex(string dir) {
    auto pkgDir = buildPath(dir, "packages");
    if (!exists(pkgDir)) {
        writeln("opkg repo index: missing directory ", pkgDir);
        writeln("hint: run `opkg repo init ", dir, "` first");
        return 1;
    }

    auto b = appender!string();
    b.put("[\n");

    int count = 0;
    foreach (entry; dirEntries(pkgDir, SpanMode.shallow)) {
        if (!entry.isFile || extension(entry.name) != ".opk") {
            continue;
        }
        auto pkg = unpackOpkV1(entry.name);
        scope (exit) cleanupUnpacked(pkg);

        if (count > 0) b.put(",\n");
        b.put("  {\n");
        b.put("    \"name\": " ~ JSONValue(pkg.metadata.name).toString() ~ ",\n");
        b.put("    \"version\": " ~ JSONValue(pkg.metadata.versionString).toString() ~ ",\n");
        b.put("    \"arch\": " ~ JSONValue(pkg.metadata.arch).toString() ~ ",\n");
        b.put("    \"filename\": " ~ JSONValue(baseName(entry.name)).toString() ~ ",\n");
        b.put("    \"checksum\": " ~ JSONValue("sha256:" ~ sha256Hex(entry.name)).toString() ~ ",\n");
        b.put("    \"depends\": " ~ jsonArrayOfStrings(pkg.metadata.depends) ~ ",\n");
        b.put("    \"summary\": " ~ JSONValue(pkg.metadata.summary).toString() ~ "\n");
        b.put("  }");
        count++;
    }
    b.put("\n]\n");

    auto outPath = buildPath(dir, "index.json");
    write(outPath, b.data);
    writeln("==> indexed ", count, " package(s)");
    writeln("==> wrote ", outPath);
    return 0;
}

int cmdRepo(const(string)[] args) {
    if (args.length == 0) {
        auto path = opkgRepoConfigPath();
        auto repos = loadRepoConfig(path);
        if (repos.length == 0) {
            writeln("No repositories configured in ", path);
            return 1;
        }
        writeln("Configured repositories:");
        foreach (r; repos) {
            writeln("  - ", r.name, " => ", r.baseUrl);
        }
        return 0;
    }

    if (args[0] == "init") {
        if (args.length != 2) {
            writeln("usage: opkg repo init <dir>");
            return 1;
        }
        return repoInit(args[1]);
    }

    if (args[0] == "index") {
        if (args.length != 2) {
            writeln("usage: opkg repo index <dir>");
            return 1;
        }
        return repoIndex(args[1]);
    }

    if (args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        printUsage();
        return 0;
    }

    writeln("opkg repo: unknown subcommand: ", args[0]);
    printUsage();
    return 1;
}
