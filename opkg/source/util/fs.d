module util.fs;

import std.file : exists, mkdirRecurse;
import std.path : buildPath;
private string trimLeadingSlash(string s) {
    size_t i = 0;
    while (i < s.length && s[i] == '/') {
        i++;
    }
    return s[i .. $];
}

private string normalizePathLite(string s) {
    if (s.length == 0) return "/";
    string result;
    result.reserve(s.length);
    bool lastSlash = false;
    foreach (c; s) {
        if (c == '/') {
            if (!lastSlash) {
                result ~= c;
                lastSlash = true;
            }
        } else {
            result ~= c;
            lastSlash = false;
        }
    }
    if (result.length > 1 && result[$ - 1] == '/') {
        result = result[0 .. $ - 1];
    }
    return result.length == 0 ? "/" : result;
}

bool pathExists(string path) {
    return exists(path);
}

string normalizeManifestPath(string path) {
    import std.string : replace;

    auto p = path;
    while (p.length >= 2 && p[0 .. 2] == "./") {
        p = p[2 .. $];
    }
    p = trimLeadingSlash(p);
    p = "/" ~ p;
    auto r = normalizePathLite(p);
    while (r.length >= 3 && r[0 .. 3] == "/./") {
        r = r[2 .. $];
    }
    r = replace(r, "/./", "/");
    if (r.length > 2 && r[$ - 2 .. $] == "/.") {
        r = r[0 .. $ - 2];
    }
    return normalizePathLite(r);
}

string buildDbRoot(string rootPath) {
    auto cleanRoot = rootPath.length == 0 ? "/" : normalizePathLite(rootPath);
    auto sub = "var/lib/opkg";
    if (cleanRoot == "/") {
        return "/" ~ sub;
    }
    return buildPath(cleanRoot, sub);
}

void ensureDbDirs(string dbRoot) {
    mkdirRecurse(buildPath(dbRoot, "installed"));
    mkdirRecurse(buildPath(dbRoot, "repos"));
}

string ensureAbsoluteInsideRoot(string rootPath, string relPath) {
    auto root = normalizePathLite(rootPath.length == 0 ? "/" : rootPath);
    auto rel = trimLeadingSlash(relPath);
    auto result = root == "/" ? ("/" ~ rel) : buildPath(root, rel);
    return normalizePathLite(result);
}

bool isOpkPath(string path) {
    return path.length > 4 && path[$ - 4 .. $] == ".opk";
}

