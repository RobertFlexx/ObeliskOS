module util.hash;

import std.file : read;
import std.digest.sha : sha256Of;
import std.digest : toHexString;
import std.string : toLower;

string sha256Hex(string path) {
    auto data = read(path);
    auto digest = sha256Of(data);
    return toLower(toHexString(digest).idup);
}

