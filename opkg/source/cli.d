module cli;

import std.stdio : writeln;

import commands.install;
import commands.remove;
import commands.list;
import commands.info;
import commands.files;
import commands.owner;
import commands.search;
import commands.update;
import commands.build;
import commands.repo;

int runCli(string[] args) {
    if (args.length <= 1) {
        printUsage();
        return 1;
    }

    const cmd = args[1];
    const rest = args.length > 2 ? args[2 .. $] : [];

    switch (cmd) {
        case "install": return cmdInstall(rest);
        case "remove":  return cmdRemove(rest);
        case "list":    return cmdList(rest);
        case "info":    return cmdInfo(rest);
        case "files":   return cmdFiles(rest);
        case "owner":   return cmdOwner(rest);
        case "search":  return cmdSearch(rest);
        case "update":  return cmdUpdate(rest);
        case "build":   return cmdBuild(rest);
        case "repo":    return cmdRepo(rest);
        case "-h":
        case "--help":
        case "help":
            printUsage();
            return 0;
        default:
            writeln("opkg: unknown command: ", cmd);
            printUsage();
            return 1;
    }
}

void printUsage() {
    writeln("Usage: opkg <command> [args]");
    writeln("");
    writeln("Commands:");
    writeln("  update                 Refresh repository indexes");
    writeln("  search <name>          Search packages by name");
    writeln("  install <pkg|file.opk> Install package from repo or local file");
    writeln("  remove <pkg>           Remove installed package");
    writeln("  list                   List installed packages");
    writeln("  info <pkg>             Show package metadata");
    writeln("  files <pkg>            List files installed by package");
    writeln("  owner <path>           Show owning package for file path");
    writeln("  repo                   Show configured repositories");
    writeln("  build <dir> [out.opk]  Build .opk from package directory");
}
