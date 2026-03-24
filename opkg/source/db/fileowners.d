module db.fileowners;

import std.file : exists, readText, write, mkdirRecurse;
import std.path : buildPath;
import std.json : JSONValue, parseJSON;

private string ownersFilePath(string dbRoot) {
    return buildPath(dbRoot, "fileowners.json");
}

string[string] loadFileOwners(string dbRoot) {
    auto p = ownersFilePath(dbRoot);
    string[string] owners;
    if (!exists(p)) {
        return owners;
    }
    auto root = parseJSON(readText(p));
    if (!("owners" in root.object)) {
        return owners;
    }
    auto node = root.object["owners"];
    foreach (k, v; node.object) {
        owners[k] = v.str();
    }
    return owners;
}

void saveFileOwners(string dbRoot, ref const string[string] owners) {
    mkdirRecurse(dbRoot);
    auto p = ownersFilePath(dbRoot);
    JSONValue root;
    root.object = null;
    JSONValue mapNode;
    mapNode.object = null;
    foreach (k, v; owners) {
        mapNode.object[k] = JSONValue(v);
    }
    root.object["owners"] = mapNode;
    auto body = root.toPrettyString() ~ "\n";
    write(p, cast(const(ubyte)[])body);
}

int setFileOwner(string dbRoot, string path, string packageName) {
    auto owners = loadFileOwners(dbRoot);
    owners[path] = packageName;
    saveFileOwners(dbRoot, owners);
    return 0;
}

string getFileOwner(string dbRoot, string path) {
    auto owners = loadFileOwners(dbRoot);
    return (path in owners) ? owners[path] : "";
}

