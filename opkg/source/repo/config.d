module repo.config;

import std.file : exists, readText;
import std.string : splitLines, strip, startsWith;

enum RepoConfigPath = "/etc/opkg/repos.conf";

struct RepoConfigEntry {
    string name;
    string baseUrl;
    bool enabled = true;
}

RepoConfigEntry[] loadRepoConfig(string configPath) {
    RepoConfigEntry[] repos;
    if (!exists(configPath)) {
        return repos;
    }
    foreach (raw; splitLines(readText(configPath))) {
        auto line = raw.strip();
        if (line.length == 0 || line.startsWith("#")) {
            continue;
        }
        size_t i = 0;
        while (i < line.length && line[i] != ' ' && line[i] != '\t') i++;
        if (i == 0 || i >= line.length) {
            continue;
        }
        auto name = line[0 .. i];
        while (i < line.length && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= line.length) continue;
        auto baseUrl = line[i .. $];

        RepoConfigEntry e;
        e.name = name;
        e.baseUrl = baseUrl;
        e.enabled = true;
        repos ~= e;
    }
    return repos;
}

