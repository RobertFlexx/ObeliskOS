module util.fs;

import std.file : exists, mkdirRecurse;
import std.path : buildPath;
import std.array : appender;
import std.exception : enforce;
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
    auto raw = trimLeadingSlash(normalizePathLite(path));
    enforce(raw.length > 0, "invalid empty manifest path");

    auto builder = appender!string();
    builder.put("/");

    size_t i = 0;
    bool first = true;
    while (i < raw.length) {
        size_t j = i;
        while (j < raw.length && raw[j] != '/') j++;
        auto seg = raw[i .. j];

        if (seg.length == 0 || seg == ".") {
            // Skip.
        } else {
            enforce(seg != "..", "path traversal entry is not allowed");
            foreach (c; seg) {
                enforce(c != '\0', "manifest path contains NUL");
            }
            if (!first) builder.put("/");
            builder.put(seg);
            first = false;
        }

        i = j + 1;
    }

    auto normalized = builder.data;
    enforce(normalized.length > 1, "invalid manifest path");
    return normalized;
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
    auto rel = trimLeadingSlash(normalizeManifestPath(relPath));
    auto result = root == "/" ? ("/" ~ rel) : buildPath(root, rel);
    return normalizePathLite(result);
}

bool isOpkPath(string path) {
    return path.length > 4 && path[$ - 4 .. $] == ".opk";
}

