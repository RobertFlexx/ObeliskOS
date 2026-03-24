module opkgpkg.verify;

import std.stdio : writeln;
import opkgpkg.metadata : PackageMetadata;

int verifyPackageMetadata(ref const PackageMetadata meta) {
    if (meta.name.length == 0 || meta.versionString.length == 0 || meta.arch.length == 0) {
        return 1;
    }
    return 0;
}

int verifyPackageChecksum(string opkPath, string expectedChecksum) {
    writeln("opkg: checksum verify scaffold for ", opkPath);
    if (expectedChecksum.length == 0) {
        return 1;
    }
    return 0;
}

