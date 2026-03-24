module repo.index;

import std.json : JSONValue, parseJSON;
import std.exception : enforce;

struct RepoIndexEntry {
    string name;
    string packageVersion;
    string arch;
    string filename;
    string checksum;
    string[] depends;
    string summary;
}

alias RepoIndex = RepoIndexEntry[];

private string[] parseStringArray(JSONValue v) {
    string[] result;
    foreach (it; v.array()) {
        result ~= it.str();
    }
    return result;
}

RepoIndex parseRepoIndexJson(string rawJson) {
    auto root = parseJSON(rawJson);
    RepoIndex result;
    foreach (node; root.array()) {
        RepoIndexEntry e;
        enforce("name" in node.object, "index entry missing name");
        enforce("version" in node.object, "index entry missing version");
        enforce("arch" in node.object, "index entry missing arch");
        enforce("filename" in node.object, "index entry missing filename");
        enforce("checksum" in node.object, "index entry missing checksum");
        enforce("summary" in node.object, "index entry missing summary");
        e.name = node.object["name"].str();
        e.packageVersion = node.object["version"].str();
        e.arch = node.object["arch"].str();
        e.filename = node.object["filename"].str();
        e.checksum = node.object["checksum"].str();
        e.summary = node.object["summary"].str();
        if ("depends" in node.object) {
            e.depends = parseStringArray(node.object["depends"]);
        }
        result ~= e;
    }
    return result;
}

