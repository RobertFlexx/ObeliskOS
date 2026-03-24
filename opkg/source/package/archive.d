module opkgpkg.archive;

import std.file : exists, readText, mkdirRecurse, rmdirRecurse;
import std.path : buildPath;
import std.process : execute;
import std.exception : enforce;
import std.string : splitLines, strip;
import std.conv : to;

import opkgpkg.metadata : PackageMetadata, parseMetaJson;
import util.fs : normalizeManifestPath;

struct OpkUnpacked {
    PackageMetadata metadata;
    string[] payloadFiles;
    string workDir;
    string payloadTarPath;
}

private string mkWorkDir() {
    import std.datetime.systime : Clock;
    import core.stdc.stdlib : rand;
    auto n = Clock.currTime.stdTime;
    auto dir = "/tmp/opkg-" ~ n.to!string ~ "-" ~ rand().to!string;
    mkdirRecurse(dir);
    return dir;
}

private void runTar(string[] args, string errorPrefix) {
    auto res = execute(args);
    if (res.status != 0) {
        throw new Exception(errorPrefix ~ ": " ~ res.output.strip());
    }
}

private void verifyPayloadArchiveMembers(string payloadTarPath) {
    auto listOut = execute(["tar", "-tf", payloadTarPath]);
    enforce(listOut.status == 0, "failed to inspect files.tar");
    foreach (line; splitLines(listOut.output)) {
        auto t = line.strip();
        if (t.length == 0 || t[$ - 1] == '/') {
            continue;
        }
        // normalizeManifestPath throws on traversal entries like "../".
        auto normalizedPath = normalizeManifestPath(t);
        cast(void)normalizedPath;
    }
}

int packOpkV1(string sourceDir, string outOpkPath) {
    auto metaPath = buildPath(sourceDir, "meta.json");
    auto filesTarPath = buildPath(sourceDir, "files.tar");
    auto rootfsPath = buildPath(sourceDir, "rootfs");
    enforce(exists(metaPath), "build failed: missing meta.json");
    bool tempFilesTar = false;
    if (!exists(filesTarPath)) {
        enforce(exists(rootfsPath), "build failed: missing files.tar (or rootfs/)");
        runTar(["tar", "-cf", filesTarPath, "-C", rootfsPath, "."], "failed to pack rootfs into files.tar");
        tempFilesTar = true;
    }
    scope (exit) {
        if (tempFilesTar && exists(filesTarPath)) {
            import std.file : remove;
            remove(filesTarPath);
        }
    }
    runTar(["tar", "-cf", outOpkPath, "-C", sourceDir, "meta.json", "files.tar"], "failed to build .opk");
    return 0;
}

OpkUnpacked unpackOpkV1(string opkPath) {
    enforce(exists(opkPath), "package file not found: " ~ opkPath);
    auto work = mkWorkDir();
    runTar(["tar", "-xf", opkPath, "-C", work], "failed to unpack .opk");

    auto metaPath = buildPath(work, "meta.json");
    auto filesTarPath = buildPath(work, "files.tar");
    enforce(exists(metaPath), "malformed package: missing meta.json");
    enforce(exists(filesTarPath), "malformed package: missing files.tar");
    verifyPayloadArchiveMembers(filesTarPath);

    auto meta = parseMetaJson(readText(metaPath));
    auto listOut = execute(["tar", "-tf", filesTarPath]);
    enforce(listOut.status == 0, "failed to read files.tar");

    string[] manifest;
    foreach (line; splitLines(listOut.output)) {
        auto t = line.strip();
        if (t.length == 0 || t[$ - 1] == '/') {
            continue;
        }
        manifest ~= normalizeManifestPath(t);
    }

    OpkUnpacked result;
    result.metadata = meta;
    result.payloadFiles = manifest;
    result.workDir = work;
    result.payloadTarPath = filesTarPath;
    return result;
}

void extractPayloadToRoot(string payloadTarPath, string rootPath) {
    verifyPayloadArchiveMembers(payloadTarPath);
    runTar(
        ["tar", "--no-same-owner", "--no-same-permissions", "-xf", payloadTarPath, "-C", rootPath],
        "failed to extract package payload"
    );
}

void cleanupUnpacked(ref OpkUnpacked pkg) {
    if (pkg.workDir.length > 0 && exists(pkg.workDir)) {
        rmdirRecurse(pkg.workDir);
    }
    pkg.workDir = "";
}

int unpackOpkV1(string opkPath, string outDir) {
    auto p = unpackOpkV1(opkPath);
    scope (exit) cleanupUnpacked(p);
    extractPayloadToRoot(p.payloadTarPath, outDir);
    return 0;
}

