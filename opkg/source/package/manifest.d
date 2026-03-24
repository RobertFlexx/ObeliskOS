module opkgpkg.manifest;

struct ManifestEntry {
    string path;
    ulong size;
    string checksum;
}

alias PackageManifest = ManifestEntry[];

