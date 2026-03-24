module repo.mirror;

import repo.config : RepoConfigEntry;

string pickMirror(ref const RepoConfigEntry repo) {
    return repo.baseUrl;
}

