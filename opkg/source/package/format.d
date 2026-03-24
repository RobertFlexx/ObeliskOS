module opkgpkg.format;

enum OPKVersion = "1";
enum MetaFileName = "meta.json";
enum PayloadFileName = "files.tar";

/*
 * .opk v1 physical layout:
 *   <package>.opk (tar archive)
 *     - meta.json
 *     - files.tar
 */

