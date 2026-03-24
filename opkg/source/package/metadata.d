module opkgpkg.metadata;

import std.json : JSONValue, parseJSON;
import std.array : appender;
import std.exception : enforce;
import std.string : format;

struct PackageMetadata {
    string name;
    string versionString;
    string arch;
    string summary;
    string description;
    string[] depends;
    string[] provides;
    string[] conflicts;
    string maintainer;
    string section;
}

private string[] parseStringArray(JSONValue v) {
    string[] result;
    foreach (it; v.array()) {
        result ~= it.str();
    }
    return result;
}

private string getRequiredString(JSONValue root, string key) {
    enforce(key in root.object, format("missing metadata field: %s", key));
    auto s = root.object[key].str();
    enforce(s.length > 0, format("field %s must not be empty", key));
    return s;
}

private string serializeStringArray(const(string)[] items) {
    auto b = appender!string();
    b.put("[");
    foreach (i, item; items) {
        if (i > 0) {
            b.put(",");
        }
        b.put(JSONValue(item).toString());
    }
    b.put("]");
    return b.data;
}

PackageMetadata parseMetaJson(string rawJson) {
    auto root = parseJSON(rawJson);
    auto _obj = root.object;

    PackageMetadata meta;
    meta.name = getRequiredString(root, "name");
    meta.versionString = getRequiredString(root, "version");
    meta.arch = getRequiredString(root, "arch");
    meta.summary = getRequiredString(root, "summary");
    meta.description = getRequiredString(root, "description");
    meta.maintainer = getRequiredString(root, "maintainer");
    meta.section = getRequiredString(root, "section");

    if ("depends" in root.object) {
        meta.depends = parseStringArray(root.object["depends"]);
    }
    if ("provides" in root.object) {
        meta.provides = parseStringArray(root.object["provides"]);
    }
    if ("conflicts" in root.object) {
        meta.conflicts = parseStringArray(root.object["conflicts"]);
    }

    return meta;
}

string serializeMetaJson(ref const PackageMetadata meta) {
    enforce(meta.name.length > 0, "metadata name must not be empty");
    enforce(meta.versionString.length > 0, "metadata version must not be empty");
    enforce(meta.arch.length > 0, "metadata arch must not be empty");
    enforce(meta.summary.length > 0, "metadata summary must not be empty");
    enforce(meta.description.length > 0, "metadata description must not be empty");
    enforce(meta.maintainer.length > 0, "metadata maintainer must not be empty");
    enforce(meta.section.length > 0, "metadata section must not be empty");

    return format(
        "{\n" ~
        "  \"name\": %s,\n" ~
        "  \"version\": %s,\n" ~
        "  \"arch\": %s,\n" ~
        "  \"summary\": %s,\n" ~
        "  \"description\": %s,\n" ~
        "  \"depends\": %s,\n" ~
        "  \"provides\": %s,\n" ~
        "  \"conflicts\": %s,\n" ~
        "  \"maintainer\": %s,\n" ~
        "  \"section\": %s\n" ~
        "}\n",
        JSONValue(meta.name).toString(),
        JSONValue(meta.versionString).toString(),
        JSONValue(meta.arch).toString(),
        JSONValue(meta.summary).toString(),
        JSONValue(meta.description).toString(),
        serializeStringArray(meta.depends),
        serializeStringArray(meta.provides),
        serializeStringArray(meta.conflicts),
        JSONValue(meta.maintainer).toString(),
        JSONValue(meta.section).toString()
    );
}

