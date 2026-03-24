/*
 * Obelisk OS - Axiom Policy Daemon (axiomd)
 * From Axioms, Order.
 *
 * Main entry point for the Prolog policy daemon.
 * File extension: .pro
 */

:- module(axiomd, [
    start/0,
    stop/0,
    handle_query/2
]).

:- use_module(policy/access).
:- use_module(policy/inheritance).
:- use_module(policy/allocation).
:- use_module(kernel_ipc).
:- use_module(sandbox).

%% Daemon state
:- dynamic daemon_running/1.
:- dynamic policy_cache/5.      % policy_cache(UID, GID, Path, Decision, ExpiresAt)

%% Configuration
config(cache_timeout, 5).       % seconds
config(max_recursion, 1000).
config(log_level, info).

%% start/0 - Initialize and start the daemon
start :-
    write('axiomd: Obelisk Policy Daemon starting...'), nl,
    
    % Initialize subsystems
    sandbox_init,
    ipc_init,
    load_policies,
    
    % Mark as running
    assertz(daemon_running(true)),
    
    write('axiomd: Daemon started successfully'), nl,
    write('axiomd: From Axioms, Order.'), nl,
    
    % Enter main loop
    main_loop.

%% stop/0 - Stop the daemon
stop :-
    write('axiomd: Shutting down...'), nl,
    retractall(daemon_running(_)),
    ipc_shutdown,
    write('axiomd: Daemon stopped'), nl.

%% main_loop/0 - Main processing loop
main_loop :-
    daemon_running(true),
    !,
    (   ipc_receive(Query, Timeout)
    ->  handle_query(Query, Response),
        ipc_send(Response)
    ;   true  % Timeout, continue
    ),
    main_loop.

main_loop :-
    % Daemon stopped
    write('axiomd: Main loop exiting'), nl.

%% handle_query/2 - Process a policy query
handle_query(query(QueryID, access_check(UID, GID, Path, Operation)), Response) :-
    !,
    write('axiomd: Processing access query '), write(QueryID), nl,
    
    % Check cache first
    (   check_cache(UID, GID, Path, CachedResult)
    ->  Result = CachedResult
    ;   % Evaluate policy
        (   evaluate_access(UID, GID, Path, Operation, Decision)
        ->  Result = Decision,
            cache_decision(UID, GID, Path, Decision)
        ;   Result = deny
        )
    ),
    
    Response = response(QueryID, access_result(Result)).

handle_query(query(QueryID, allocation_policy(Path, FileType)), Response) :-
    !,
    write('axiomd: Processing allocation query '), write(QueryID), nl,
    
    (   get_allocation_policy(Path, FileType, Policy)
    ->  Response = response(QueryID, Policy)
    ;   Response = response(QueryID, allocate_on(any, stripe_size(0)))
    ).

handle_query(query(QueryID, inherit_permissions(ParentPath, NewPath)), Response) :-
    !,
    write('axiomd: Processing inheritance query '), write(QueryID), nl,
    
    (   compute_inherited_permissions(ParentPath, NewPath, Mode, UID, GID, Rules)
    ->  Response = response(QueryID, permissions(Mode, UID, GID, Rules))
    ;   Response = response(QueryID, permissions(default, default, default, []))
    ).

handle_query(ping, pong) :-
    !,
    write('axiomd: Ping received, sending pong'), nl.

handle_query(Query, response(error, unknown_query)) :-
    write('axiomd: Unknown query type: '), write(Query), nl.

%% Cache operations
check_cache(UID, GID, Path, Result) :-
    policy_cache(UID, GID, Path, Result, ExpiresAt),
    now_seconds(Now),
    Now =< ExpiresAt,
    !.

cache_decision(UID, GID, Path, Decision) :-
    config(cache_timeout, TTL),
    now_seconds(Now),
    ExpiresAt is Now + TTL,
    retractall(policy_cache(UID, GID, Path, _, _)),
    assertz(policy_cache(UID, GID, Path, Decision, ExpiresAt)).

clear_cache :-
    retractall(policy_cache(_, _, _, _, _)).

clear_cache(Path) :-
    retractall(policy_cache(_, _, Path, _, _)).

now_seconds(Now) :-
    (   catch(get_time(T), _, fail)
    ->  Now is floor(T)
    ;   Now = 0
    ).

%% load_policies/0 - Load policy files
load_policies :-
    write('axiomd: Loading policies...'), nl,
    % Policies are loaded via use_module
    write('axiomd: Policies loaded'), nl.

%% Logging utilities
log(Level, Message) :-
    config(log_level, ConfigLevel),
    log_level_value(Level, LevelVal),
    log_level_value(ConfigLevel, ConfigVal),
    LevelVal >= ConfigVal,
    !,
    write('axiomd ['), write(Level), write(']: '),
    write(Message), nl.

log(_, _).

log_level_value(debug, 0).
log_level_value(info, 1).
log_level_value(warning, 2).
log_level_value(error, 3).
