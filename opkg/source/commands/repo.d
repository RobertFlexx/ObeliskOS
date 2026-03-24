module commands.repo;

import std.stdio : writeln;
import util.paths : opkgRepoConfigPath;
import repo.config : loadRepoConfig;

int cmdRepo(const(string)[] args) {
    if (args.length != 0) {
        writeln("usage: opkg repo");
        return 1;
    }
    auto path = opkgRepoConfigPath();
    auto repos = loadRepoConfig(path);
    if (repos.length == 0) {
        writeln("No repositories configured in ", path);
        return 1;
    }
    foreach (r; repos) {
        writeln(r.name, " ", r.baseUrl);
    }
    return 0;
}
