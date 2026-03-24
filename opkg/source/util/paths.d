module util.paths;

import std.process : environment;
import util.fs : buildDbRoot;

enum DefaultDbRoot = "/var/lib/opkg";
enum DefaultCacheRoot = "/var/cache/opkg";
enum DefaultRepoConfig = "/etc/opkg/repos.conf";
enum DefaultArch = "x86_64";

string opkgRootPath() {
    if ("OPKG_ROOT" in environment) {
        auto v = environment["OPKG_ROOT"];
        if (v.length > 0) {
            return v;
        }
    }
    return "/";
}

string opkgDbRootPath() {
    if ("OPKG_DB_ROOT" in environment) {
        auto v = environment["OPKG_DB_ROOT"];
        if (v.length > 0) {
            return v;
        }
    }
    return buildDbRoot(opkgRootPath());
}

string opkgRepoConfigPath() {
    if ("OPKG_REPOS_CONF" in environment) {
        auto v = environment["OPKG_REPOS_CONF"];
        if (v.length > 0) {
            return v;
        }
    }
    auto root = opkgRootPath();
    if (root == "/") {
        return DefaultRepoConfig;
    }
    return root ~ "/etc/opkg/repos.conf";
}

string opkgArch() {
    if ("OPKG_ARCH" in environment) {
        auto v = environment["OPKG_ARCH"];
        if (v.length > 0) {
            return v;
        }
    }
    return DefaultArch;
}

