module util.errors;

enum OpkgError : int {
    ok = 0,
    invalidArgs = 1,
    notFound = 2,
    io = 3,
    format = 4
}

