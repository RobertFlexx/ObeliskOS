/*
 * Obelisk OS - Permission Inheritance Rules
 * From Axioms, Order.
 */

:- module(inheritance, [
    compute_inherited_permissions/5,
    inherits_from_parent/2,
    apply_inheritance_rules/4
]).

%% Dynamic inheritance rules
:- dynamic inheritance_rule/3.  % inheritance_rule(ParentPattern, ChildPattern, Rules)
:- dynamic directory_policy/2.  % directory_policy(Path, Policy)

%% compute_inherited_permissions/5
%% Compute permissions for a new file based on parent directory
compute_inherited_permissions(ParentPath, NewPath, Mode, UID, GID, Rules) :-
    % Get parent directory policy
    (   directory_policy(ParentPath, Policy)
    ->  extract_policy(Policy, Mode, UID, GID, Rules)
    ;   % Default inheritance
        default_inheritance(ParentPath, NewPath, Mode, UID, GID, Rules)
    ).

%% extract_policy/5 - Extract values from policy term
extract_policy(policy(Mode, UID, GID, Rules), Mode, UID, GID, Rules).
extract_policy(inherit_all, default, default, default, [inherit_all]).
extract_policy(inherit_read, default, default, default, [inherit_read]).
extract_policy(inherit_write, default, default, default, [inherit_write]).
extract_policy(private, 8'600, caller, caller, [private]).

%% default_inheritance/5 - Default inheritance behavior
default_inheritance(ParentPath, _NewPath, Mode, UID, GID, Rules) :-
    path_is_temp(ParentPath),
    !,
    Mode = 8'600,
    UID = caller,
    GID = caller,
    Rules = [private, sticky_scope].

default_inheritance(ParentPath, _NewPath, Mode, UID, GID, Rules) :-
    % Check for sticky bit, setgid, etc.
    (   has_setgid(ParentPath)
    ->  inherit_group(ParentPath, GID),
        Mode = default,
        UID = default,
        Rules = [setgid]
    ;   Mode = default,
        UID = default,
        GID = default,
        Rules = []
    ).

path_is_temp('/tmp').
path_is_temp('/var/tmp').

%% inherits_from_parent/2 - Check if child inherits from parent
inherits_from_parent(ChildPath, ParentPath) :-
    parent_directory(ChildPath, ParentPath).

inherits_from_parent(ChildPath, AncestorPath) :-
    parent_directory(ChildPath, ParentPath),
    ParentPath \= '/',
    inherits_from_parent(ParentPath, AncestorPath).

%% apply_inheritance_rules/4 - Apply inheritance rules
apply_inheritance_rules([], Mode, UID, GID, Mode, UID, GID).

apply_inheritance_rules([inherit_all|Rest], ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut) :-
    apply_inheritance_rules(Rest, ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut).

apply_inheritance_rules([inherit_read|Rest], ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut) :-
    (   ModeIn = default
    ->  Mode1 = 8'444
    ;   Mode1 = ModeIn
    ),
    apply_inheritance_rules(Rest, Mode1, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut).

apply_inheritance_rules([setgid|Rest], ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut) :-
    apply_inheritance_rules(Rest, ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut).

apply_inheritance_rules([private|Rest], _ModeIn, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut) :-
    apply_inheritance_rules(Rest, 8'600, UIDIn, GIDIn, ModeOut, UIDOut, GIDOut).

%% Stub predicates
has_setgid(_) :- fail.
inherit_group(_, 0).
parent_directory(Path, Parent) :-
    atom_string(Path, PathStr),
    string_codes(PathStr, Codes),
    (   append(ParentCodes, [0'/|_], Codes),
        ParentCodes \= []
    ->  string_codes(ParentStr, ParentCodes),
        atom_string(Parent, ParentStr)
    ;   Parent = '/'
    ).

%% Default policies for system directories
:- assertz(directory_policy('/tmp', policy(8'1777, default, default, [sticky]))).
:- assertz(directory_policy('/var/tmp', policy(8'1777, default, default, [sticky]))).
:- assertz(directory_policy('/home', inherit_all)).
