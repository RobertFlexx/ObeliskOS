module commands.install;

import std.stdio : writeln;
import std.file : exists, readText, write, mkdirRecurse, remove, dirEntries, SpanMode;
import std.path : buildPath, baseName, extension;
import std.string : toLower, split, startsWith;
import std.net.curl : get;
import std.conv : to;

import util.paths : opkgRootPath, opkgDbRootPath, opkgRepoConfigPath, opkgArch;
import util.fs : ensureDbDirs, isOpkPath;
import opkgpkg.archive : unpackOpkV1, extractPayloadToRoot, cleanupUnpacked;
import db.installed : addInstalledRecord, isInstalled;
import db.fileowners : loadFileOwners, saveFileOwners;
import repo.config : RepoConfigEntry, loadRepoConfig;
import repo.index : RepoIndexEntry, parseRepoIndexJson;
import util.hash : sha256Hex;

private int versionCmp(string a, string b) {
    auto pa = a.split(".");
    auto pb = b.split(".");
    auto n = pa.length > pb.length ? pa.length : pb.length;
    for (size_t i = 0; i < n; i++) {
        long va = 0;
        long vb = 0;
        if (i < pa.length) {
            try va = pa[i].to!long(); catch (Exception) va = 0;
        }
        if (i < pb.length) {
            try vb = pb[i].to!long(); catch (Exception) vb = 0;
        }
        if (va < vb) return -1;
        if (va > vb) return 1;
    }
    return 0;
}

private bool parseChecksumSha256(string field, out string hex) {
    auto low = toLower(field);
    if (!low.startsWith("sha256:")) {
        return false;
    }
    auto h = low[7 .. $];
    if (h.length != 64) return false;
    foreach (c; h) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    hex = h;
    return true;
}

private int installLocalOpkFile(string targetPath) {
    auto root = opkgRootPath();
    auto dbRoot = opkgDbRootPath();
    auto arch = opkgArch();
    ensureDbDirs(dbRoot);

    auto pkg = unpackOpkV1(targetPath);
    scope (exit) cleanupUnpacked(pkg);

    if (isInstalled(dbRoot, pkg.metadata.name)) {
        writeln("opkg install: package already installed: ", pkg.metadata.name);
        return 1;
    }

    if (pkg.metadata.arch != arch && pkg.metadata.arch != "noarch") {
        writeln(
            "opkg install: package architecture mismatch: ",
            pkg.metadata.arch, " (system: ", arch, ")"
        );
        return 1;
    }

    auto owners = loadFileOwners(dbRoot);
    foreach (path; pkg.payloadFiles) {
        if (path in owners && owners[path] != pkg.metadata.name) {
            writeln("opkg install: file conflict: ", path, " owned by ", owners[path]);
            return 1;
        }
    }

    foreach (c; pkg.metadata.conflicts) {
        if (isInstalled(dbRoot, c)) {
            writeln("opkg install: conflict: ", pkg.metadata.name, " conflicts with installed package ", c);
            return 1;
        }
    }

    extractPayloadToRoot(pkg.payloadTarPath, root);
    addInstalledRecord(dbRoot, pkg.metadata, pkg.payloadFiles);
    foreach (path; pkg.payloadFiles) {
        owners[path] = pkg.metadata.name;
    }
    saveFileOwners(dbRoot, owners);
    writeln("==> Installed ", pkg.metadata.name, " ", pkg.metadata.versionString, " (", pkg.metadata.arch, ")");
    return 0;
}

private int installFromRepoName(string pkgName) {
    auto dbRoot = opkgDbRootPath();
    ensureDbDirs(dbRoot);
    auto arch = opkgArch();

    auto repos = loadRepoConfig(opkgRepoConfigPath());
    if (repos.length == 0) {
        writeln("opkg install: no repos configured");
        return 1;
    }

    RepoConfigEntry[string] repoByName;
    foreach (r; repos) {
        repoByName[r.name] = r;
    }

    bool seenName = false;
    RepoConfigEntry bestRepo;
    RepoIndexEntry bestEntry;
    bool haveBest = false;
    auto repoCacheDir = buildPath(dbRoot, "repos");
    if (!exists(repoCacheDir)) {
        writeln("opkg install: no cached repositories, run: opkg update");
        return 1;
    }

    foreach (cacheFile; dirEntries(repoCacheDir, SpanMode.shallow)) {
        if (!cacheFile.isFile || extension(cacheFile.name) != ".json") {
            continue;
        }
        auto repoName = baseName(cacheFile.name)[0 .. $ - 5];
        if (!(repoName in repoByName)) {
            continue;
        }
        auto entries = parseRepoIndexJson(readText(cacheFile.name));
        foreach (e; entries) {
            if (e.name != pkgName) {
                continue;
            }
            seenName = true;
            if (e.arch != arch) {
                continue;
            }
            if (!haveBest || versionCmp(bestEntry.packageVersion, e.packageVersion) < 0) {
                bestRepo = repoByName[repoName];
                bestEntry = e;
                haveBest = true;
            }
        }
    }

    if (!haveBest) {
        if (seenName) {
            writeln("opkg install: package found but no matching architecture: ", arch);
        } else {
            writeln("opkg install: package not found: ", pkgName);
        }
        return 1;
    }

    string expectedSha;
    if (!parseChecksumSha256(bestEntry.checksum, expectedSha)) {
        writeln("opkg install: invalid or missing checksum for package ", bestEntry.name);
        return 1;
    }

    auto url = bestRepo.baseUrl ~ "/packages/" ~ bestEntry.filename;
    writeln("==> Resolving ", pkgName, " from repo ", bestRepo.name);
    writeln("==> Downloading ", url);
    char[] data;
    try {
        data = get(url);
    } catch (Exception e) {
        writeln("opkg install: download failed: ", url, " (", e.msg, ")");
        return 1;
    }

    auto cacheDir = buildPath(dbRoot, "cache");
    mkdirRecurse(cacheDir);
    auto cachedOpkPath = buildPath(cacheDir, bestEntry.filename);
    write(cachedOpkPath, cast(const(ubyte)[])data);

    auto got = sha256Hex(cachedOpkPath);
    if (got != expectedSha) {
        writeln("opkg install: checksum mismatch for ", bestEntry.filename);
        writeln("  expected sha256:", expectedSha);
        writeln("  got      sha256:", got);
        remove(cachedOpkPath);
        return 1;
    }

    writeln("==> Verified checksum sha256:", expectedSha);
    return installLocalOpkFile(cachedOpkPath);
}

int cmdInstall(const(string)[] args) {
    if (args.length < 1) {
        writeln("opkg install: missing package name or .opk path");
        writeln("usage: opkg install <pkg|file.opk>");
        return 1;
    }

    auto target = args[0].idup;
    try {
        if (isOpkPath(target)) {
            return installLocalOpkFile(target);
        }
        return installFromRepoName(target);
    } catch (Exception e) {
        writeln("opkg install: ", e.msg);
        return 1;
    }
}
