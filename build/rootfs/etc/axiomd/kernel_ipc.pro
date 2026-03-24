/*
 * Obelisk OS - Kernel IPC Module
 * From Axioms, Order.
 */

:- module(kernel_ipc, [
    ipc_init/0,
    ipc_shutdown/0,
    ipc_send/1,
    ipc_receive/2,
    register_with_kernel/0
]).

%% IPC state
:- dynamic ipc_initialized/1.
:- dynamic message_queue_id/2.

%% Queue keys (must match kernel definitions)
kernel_to_daemon_key(0xAX10D001).
daemon_to_kernel_key(0xAX10D002).

%% ipc_init/0 - Initialize IPC with kernel
ipc_init :-
    write('kernel_ipc: Initializing IPC...'), nl,
    
    % Get message queue IDs
    kernel_to_daemon_key(K2DKey),
    daemon_to_kernel_key(D2KKey),
    
    % Open queues (these would be actual syscalls)
    % For now, simulate
    assertz(message_queue_id(kernel_to_daemon, K2DKey)),
    assertz(message_queue_id(daemon_to_kernel, D2KKey)),
    
    % Register with kernel
    register_with_kernel,
    
    assertz(ipc_initialized(true)),
    write('kernel_ipc: IPC initialized'), nl.

%% ipc_shutdown/0 - Shutdown IPC
ipc_shutdown :-
    write('kernel_ipc: Shutting down IPC...'), nl,
    retractall(ipc_initialized(_)),
    retractall(message_queue_id(_, _)),
    write('kernel_ipc: IPC shutdown complete'), nl.

%% register_with_kernel/0 - Register daemon with kernel
register_with_kernel :-
    write('kernel_ipc: Registering with kernel...'), nl,
    % This would send a registration message to the kernel
    % The kernel would then mark the daemon as available
    write('kernel_ipc: Registered with kernel'), nl.

%% ipc_send/1 - Send message to kernel
ipc_send(Message) :-
    ipc_initialized(true),
    !,
    message_queue_id(daemon_to_kernel, QueueID),
    % This would be an actual msgqueue_send syscall
    write('kernel_ipc: Sending message to queue '), write(QueueID), nl,
    write('kernel_ipc: Message: '), write(Message), nl.

ipc_send(_) :-
    write('kernel_ipc: Error - IPC not initialized'), nl,
    fail.

%% ipc_receive/2 - Receive message from kernel with timeout
ipc_receive(Message, Timeout) :-
    ipc_initialized(true),
    !,
    message_queue_id(kernel_to_daemon, QueueID),
    % This would be an actual msgqueue_recv syscall
    % For simulation, we'll just fail (no messages)
    % In real implementation, this blocks until message or timeout
    write('kernel_ipc: Waiting for message on queue '), 
    write(QueueID), write(' (timeout: '), write(Timeout), write('ms)'), nl,
    fail.  % Simulate no message available

ipc_receive(_, _) :-
    write('kernel_ipc: Error - IPC not initialized'), nl,
    fail.

%% Utility predicates for message encoding/decoding
encode_query(Query, Bytes) :-
    % Convert Prolog term to binary format
    term_to_atom(Query, Atom),
    atom_codes(Atom, Bytes).

decode_query(Bytes, Query) :-
    % Convert binary format to Prolog term
    atom_codes(Atom, Bytes),
    atom_to_term(Atom, Query, _).

encode_response(Response, Bytes) :-
    term_to_atom(Response, Atom),
    atom_codes(Atom, Bytes).
