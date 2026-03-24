module commands.update;

import std.stdio : writeln;
import std.path : buildPath;

import util.paths : opkgDbRootPath, opkgRepoConfigPath;
import util.fs : ensureDbDirs;
import repo.config : loadRepoConfig;
import repo.fetch : fetchRepoIndex;

int cmdUpdate(const(string)[] args) {
    if (args.length != 0) {
        writeln("usage: opkg update");
        return 1;
    }
    auto dbRoot = opkgDbRootPath();
    ensureDbDirs(dbRoot);
    auto configPath = opkgRepoConfigPath();
    auto repos = loadRepoConfig(configPath);
    if (repos.length == 0) {
        writeln("opkg update: no repos configured in ", configPath);
        return 1;
    }

    auto repoStateDir = buildPath(dbRoot, "repos");
    int okCount = 0;
    writeln("Refreshing repository indexes...");
    foreach (repo; repos) {
        try {
            fetchRepoIndex(repo, repoStateDir);
            writeln("  [ok] ", repo.name, "  ", repo.baseUrl);
            okCount++;
        } catch (Exception e) {
            writeln("  [fail] ", repo.name, ": ", e.msg);
        }
    }
    if (okCount == 0) {
        writeln("opkg update: no repos updated");
        return 1;
    }
    writeln("opkg update: updated ", okCount, " repo(s)");
    return 0;
}
