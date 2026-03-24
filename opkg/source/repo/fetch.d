module repo.fetch;

import std.file : mkdirRecurse, write;
import std.path : buildPath;
import std.net.curl : get;
import std.json : parseJSON;
import repo.config : RepoConfigEntry;

int fetchRepoIndex(ref const RepoConfigEntry repo, string repoStateDir) {
    auto url = repo.baseUrl ~ "/index.json";
    auto data = get(url);
    auto txt = cast(string)data;
    auto parsed = parseJSON(txt);
    auto _arr = parsed.array;

    mkdirRecurse(repoStateDir);
    auto outPath = buildPath(repoStateDir, repo.name ~ ".json");
    write(outPath, data);
    return 0;
}

