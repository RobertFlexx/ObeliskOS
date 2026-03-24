/*
 * Obelisk OS - Access Control Policy Rules
 * From Axioms, Order.
 */

:- module(access, [
    evaluate_access/5,
    can_read/3,
    can_write/3,
    can_execute/3,
    can_delete/3
]).

:- use_module('../sandbox').

%% Dynamic facts for runtime rules
:- dynamic access_rule/4.       % access_rule(Pattern, Operation, Condition, Decision)
:- dynamic role/2.              % role(UID, RoleName)
:- dynamic role_permission/3.   % role_permission(RoleName, PathPattern, Operations)

%% evaluate_access/5 - Main access evaluation predicate
%% evaluate_access(+UID, +GID, +Path, +Operation, -Decision)
evaluate_access(UID, GID, Path, Operation, Decision) :-
    % Built-in critical protections for NAS integrity.
    (   critical_path_denied(UID, Path, Operation)
    ->  Decision = deny
    ;   % Check deny rules first (they take precedence)
        (   check_deny_rules(UID, GID, Path, Operation)
        ->  Decision = deny
        ;   % Check allow rules
            (   check_allow_rules(UID, GID, Path, Operation)
            ->  Decision = allow
            ;   % Fall back to default
                Decision = default
            )
        )
    ).

%% check_deny_rules/4 - Check if any deny rule matches
check_deny_rules(UID, GID, Path, Operation) :-
    access_rule(PathPattern, Operation, deny, Condition),
    path_matches(Path, PathPattern),
    evaluate_condition(Condition, UID, GID, Path),
    !.

%% check_allow_rules/4 - Check if any allow rule matches
check_allow_rules(UID, GID, Path, Operation) :-
    % Check role-based permissions
    (   role(UID, Role),
        role_permission(Role, PathPattern, Operations),
        path_matches(Path, PathPattern),
        member(Operation, Operations)
    ->  true
    ;   % Check direct access rules
        access_rule(PathPattern, Operation, allow, Condition),
        path_matches(Path, PathPattern),
        evaluate_condition(Condition, UID, GID, Path)
    ).

%% can_read/3 - Check read permission
can_read(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, read, allow).

can_read(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, read, default),
    % Fall back to ownership check
    file_owner(Path, UID).

%% can_write/3 - Check write permission
can_write(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, write, allow).

can_write(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, write, default),
    file_owner(Path, UID).

%% can_execute/3 - Check execute permission
can_execute(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, execute, allow).

can_execute(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, execute, default),
    file_owner(Path, UID),
    file_is_executable(Path).

%% can_delete/3 - Check delete permission
can_delete(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, delete, allow).

can_delete(UID, GID, Path) :-
    evaluate_access(UID, GID, Path, delete, default),
    parent_directory(Path, ParentPath),
    can_write(UID, GID, ParentPath).

%% path_matches/2 - Check if path matches pattern
path_matches(Path, Path) :- !.
path_matches(Path, Pattern) :-
    atom_concat(Prefix, '*', Pattern),
    atom_concat(Prefix, _, Path),
    !.
path_matches(Path, Pattern) :-
    atom_concat('*', Suffix, Pattern),
    atom_concat(_, Suffix, Path),
    !.

%% evaluate_condition/4 - Evaluate a condition
evaluate_condition(true, _, _, _) :- !.
evaluate_condition(owner, UID, _, Path) :-
    !,
    file_owner(Path, UID).
evaluate_condition(group_member, _, GID, Path) :-
    !,
    file_group(Path, GID).
evaluate_condition(time_range(Start, End), _, _, _) :-
    !,
    current_hour(Hour),
    Hour >= Start,
    Hour =< End.
evaluate_condition(and(C1, C2), UID, GID, Path) :-
    !,
    evaluate_condition(C1, UID, GID, Path),
    evaluate_condition(C2, UID, GID, Path).
evaluate_condition(or(C1, C2), UID, GID, Path) :-
    !,
    (   evaluate_condition(C1, UID, GID, Path)
    ;   evaluate_condition(C2, UID, GID, Path)
    ).
evaluate_condition(not(C), UID, GID, Path) :-
    !,
    \+ evaluate_condition(C, UID, GID, Path).

%% Stub predicates (to be implemented with kernel integration)
file_owner(_, 0).           % Stub: assume root owns everything
file_group(_, 0).           % Stub: assume root group
file_is_executable(_).      % Stub: assume executable
parent_directory(Path, Parent) :-
    atom_string(Path, PathStr),
    string_codes(PathStr, Codes),
    (   append(ParentCodes, [0'/|_], Codes)
    ->  string_codes(ParentStr, ParentCodes),
        atom_string(Parent, ParentStr)
    ;   Parent = '/'
    ).
current_hour(12).           % Stub: assume noon

critical_path_denied(UID, Path, Operation) :-
    UID =\= 0,
    member(Operation, [write, delete, chmod, chown]),
    path_matches(Path, '/boot*').
critical_path_denied(UID, Path, Operation) :-
    UID =\= 0,
    member(Operation, [write, delete, chmod, chown]),
    path_matches(Path, '/etc*').
critical_path_denied(UID, Path, delete) :-
    UID =\= 0,
    path_matches(Path, '/var/snapshots*').

%% Default rules
:- assertz(role(0, root)).
:- assertz(role_permission(root, '/*', [read, write, execute, delete])).

%% Example rules (can be loaded from configuration)
:- assertz(access_rule('/etc/shadow', read, deny, not(owner))).
:- assertz(access_rule('/tmp/*', write, allow, true)).
:- assertz(access_rule('/home/*', read, allow, owner)).
:- assertz(access_rule('/var/backups/*', delete, deny, not(owner))).
:- assertz(access_rule('/srv/shares/*', read, allow, true)).
