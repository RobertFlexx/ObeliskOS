/*
 * Obelisk OS - Block Allocation Policy
 * From Axioms, Order.
 */

:- module(allocation, [
    get_allocation_policy/3,
    preferred_disk/2,
    stripe_policy/2
]).

%% Dynamic allocation rules
:- dynamic allocation_rule/3.   % allocation_rule(PathPattern, FileType, Policy)
:- dynamic disk_class/2.        % disk_class(DiskName, Class)

%% get_allocation_policy/3 - Get allocation policy for a file
get_allocation_policy(Path, FileType, Policy) :-
    % Check specific rules first
    (   allocation_rule(PathPattern, FileType, Policy),
        path_matches(Path, PathPattern)
    ->  true
    ;   % Apply default rules based on file type
        default_allocation_policy(Path, FileType, Policy)
    ).

%% default_allocation_policy/3 - Default allocation based on file type
default_allocation_policy(_Path, executable, allocate_on(fast_disk, stripe_size(64))) :-
    !.

default_allocation_policy(Path, FileType, allocate_on(bulk_disk, stripe_size(1024))) :-
    is_large_media(Path, FileType),
    !.

default_allocation_policy(Path, database, allocate_on(fast_disk, stripe_size(4))) :-
    is_metadata_path(Path),
    !.

default_allocation_policy(_Path, log, allocate_on(bulk_disk, stripe_size(64))) :-
    !.

default_allocation_policy(Path, _FileType, allocate_on(fast_disk, stripe_size(16))) :-
    is_metadata_path(Path),
    !.

default_allocation_policy(_, _, allocate_on(any, stripe_size(0))).

%% is_large_media/2 - Check if file is large media
is_large_media(Path, _) :-
    file_extension(Path, Ext),
    member(Ext, [mp4, mkv, avi, mov, wmv, flv, webm, mp3, flac, wav]).

is_metadata_path(Path) :-
    path_matches(Path, '/etc/*');
    path_matches(Path, '/boot/*');
    path_matches(Path, '/var/lib/*');
    path_matches(Path, '/var/db/*').

%% file_extension/2 - Get file extension
file_extension(Path, Ext) :-
    atom_string(Path, PathStr),
    (   sub_string(PathStr, _, _, After, ".")
    ->  sub_string(PathStr, _, After, 0, ExtStr),
        atom_string(Ext, ExtStr)
    ;   Ext = none
    ).

%% preferred_disk/2 - Get preferred disk for a class
preferred_disk(fast_disk, Disk) :-
    disk_class(Disk, ssd),
    !.
preferred_disk(fast_disk, Disk) :-
    disk_class(Disk, nvme),
    !.
preferred_disk(bulk_disk, Disk) :-
    disk_class(Disk, hdd),
    !.
preferred_disk(_, any).

%% stripe_policy/2 - Get striping policy
stripe_policy(large_file, stripe_size(1024)).
stripe_policy(database, stripe_size(4)).
stripe_policy(default, stripe_size(0)).

%% path_matches/2 - Check if path matches pattern (same as in access.pro)
path_matches(Path, Path) :- !.
path_matches(Path, Pattern) :-
    atom_concat(Prefix, '*', Pattern),
    atom_concat(Prefix, _, Path),
    !.
path_matches(Path, Pattern) :-
    atom_concat('*', Suffix, Pattern),
    atom_concat(_, Suffix, Path),
    !.

%% Default disk configuration
:- assertz(disk_class(sda, ssd)).
:- assertz(disk_class(sdb, hdd)).

%% Default allocation rules
:- assertz(allocation_rule('/var/log/*', log, allocate_on(bulk_disk, stripe_size(64)))).
:- assertz(allocation_rule('/tmp/*', temporary, allocate_on(fast_disk, stripe_size(0)))).
:- assertz(allocation_rule('/usr/bin/*', executable, allocate_on(fast_disk, stripe_size(64)))).
:- assertz(allocation_rule('/var/snapshots/*', backup, allocate_on(bulk_disk, stripe_size(2048)))).
