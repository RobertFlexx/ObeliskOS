module db.installed;

import std.file : exists, mkdirRecurse, readText, write;
import std.path : buildPath, baseName, extension;
import std.algorithm : sort;
import std.json : JSONValue, parseJSON;
import std.exception : enforce;

import opkgpkg.metadata : PackageMetadata, parseMetaJson, serializeMetaJson;

struct InstalledRecord {
    PackageMetadata metadata;
    string[] installedFiles;
}

private string recordPath(string dbRoot, string packageName) {
    return buildPath(dbRoot, "installed", packageName ~ ".json");
}

InstalledRecord loadInstalledRecord(string dbRoot, string packageName) {
    auto p = recordPath(dbRoot, packageName);
    enforce(exists(p), "package is not installed: " ~ packageName);
    auto root = parseJSON(readText(p));
    enforce("meta" in root.object, "installed record missing meta");
    enforce("installed_files" in root.object, "installed record missing installed_files");

    auto meta = parseMetaJson(root.object["meta"].toString());
    auto arr = root.object["installed_files"];
    string[] files;
    foreach (it; arr.array()) {
        files ~= it.str();
    }

    InstalledRecord rec;
    rec.metadata = meta;
    rec.installedFiles = files;
    return rec;
}

bool isInstalled(string dbRoot, string packageName) {
    return exists(recordPath(dbRoot, packageName));
}

string[] listInstalledPackages(string dbRoot) {
    auto dir = buildPath(dbRoot, "installed");
    if (!exists(dir)) {
        return [];
    }
    string[] names;
    import std.file : dirEntries, SpanMode;
    foreach (entry; dirEntries(dir, SpanMode.shallow)) {
        if (!entry.isFile) {
            continue;
        }
        if (extension(entry.name) != ".json") {
            continue;
        }
        names ~= baseName(entry.name)[0 .. $ - 5];
    }
    sort(names);
    return names;
}

int addInstalledRecord(string dbRoot, ref const PackageMetadata meta, string[] installedFiles) {
    auto installedDir = buildPath(dbRoot, "installed");
    mkdirRecurse(installedDir);
    auto p = recordPath(dbRoot, meta.name);
    auto body =
        "{\n" ~
        "  \"meta\": " ~ serializeMetaJson(meta) ~ ",\n" ~
        "  \"installed_files\": " ~ JSONValue(installedFiles).toString() ~ "\n" ~
        "}\n";
    write(p, cast(const(ubyte)[])body);
    return 0;
}

int removeInstalledRecord(string dbRoot, string packageName) {
    import std.file : remove;
    auto p = recordPath(dbRoot, packageName);
    if (!exists(p)) {
        return 1;
    }
    remove(p);
    return 0;
}

