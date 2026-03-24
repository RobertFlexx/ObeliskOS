module util.log;

import std.stdio : writeln;

void logInfo(string msg) {
    writeln("opkg: ", msg);
}

void logWarn(string msg) {
    writeln("opkg: warning: ", msg);
}

void logError(string msg) {
    writeln("opkg: error: ", msg);
}

