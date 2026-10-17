------------------------------ MODULE channel ------------------------------
(***************************************************************************)
(* A bounded single-producer, single-consumer channel that passes          *)
(* ownership of each message from the sender, through the queue, to the    *)
(* receiver. TLC checks this model only; it says nothing about the C.      *)
(* models/README.md maps each action to the C function that implements it. *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
    QueueSize,  \* queue capacity
    Msgs        \* the messages the sender can allocate, a set of model values

ASSUME QueueSize \in Nat \ {0}
ASSUME IsFiniteSet(Msgs) /\ Msgs # {}

Parties == {"sender", "queue", "receiver"}

VARIABLES
    owner,          \* owner[m]: the parties holding message m
    status,         \* status[m]: "unused", "live" or "freed"
    queue,          \* messages in flight, oldest first
    sentLog,        \* history: messages passed to Send, in order
    recvLog,        \* history: messages Recv returned, in order
    usedAfterSend,  \* history: messages the sender touched after sending them
    afterClose,     \* history: sender actions that ran after Close
    closed          \* the sender has shut the channel down

vars == <<owner, status, queue, sentLog, recvLog, usedAfterSend, afterClose, closed>>

Sent == {sentLog[i] : i \in DOMAIN sentLog}

(* A history variable records a sender action that ran after Close. *)
NoteIfClosed(action) ==
    afterClose' = IF closed THEN afterClose \cup {action} ELSE afterClose

TypeOK ==
    /\ owner \in [Msgs -> SUBSET Parties]
    /\ status \in [Msgs -> {"unused", "live", "freed"}]
    /\ queue \in Seq(Msgs) /\ Len(queue) <= QueueSize
    /\ sentLog \in Seq(Msgs) /\ recvLog \in Seq(Msgs)
    /\ usedAfterSend \subseteq Msgs
    /\ afterClose \subseteq {"Alloc", "Send"}
    /\ closed \in BOOLEAN

Init ==
    /\ owner = [m \in Msgs |-> {}]
    /\ status = [m \in Msgs |-> "unused"]
    /\ queue = <<>>
    /\ sentLog = <<>>
    /\ recvLog = <<>>
    /\ usedAfterSend = {}
    /\ afterClose = {}
    /\ closed = FALSE

(* The sender takes a fresh message and owns it. *)
Alloc(m) ==
    /\ ~closed
    /\ status[m] = "unused"
    /\ status' = [status EXCEPT ![m] = "live"]
    /\ owner' = [owner EXCEPT ![m] = {"sender"}]
    /\ NoteIfClosed("Alloc")
    /\ UNCHANGED <<queue, sentLog, recvLog, usedAfterSend, closed>>

(* The sender reads or writes a message it holds. Holding is the only     *)
(* guard: the C sender can touch whatever its handle still resolves to.   *)
(* For an unsent message this step changes nothing, so it is a stutter    *)
(* and TLC's deadlock check cannot see a stuck shutdown; Drains can.      *)
SenderUse(m) ==
    /\ "sender" \in owner[m]
    /\ usedAfterSend' = IF m \in Sent THEN usedAfterSend \cup {m}
                                      ELSE usedAfterSend
    /\ UNCHANGED <<owner, status, queue, sentLog, recvLog, afterClose, closed>>

(* Send moves the message into the queue and takes it from the sender. *)
Send(m) ==
    /\ ~closed
    /\ "sender" \in owner[m]
    /\ Len(queue) < QueueSize
    /\ queue' = Append(queue, m)
    /\ owner' = [owner EXCEPT ![m] = (@ \ {"sender"}) \cup {"queue"}]
    /\ sentLog' = Append(sentLog, m)
    /\ NoteIfClosed("Send")
    /\ UNCHANGED <<status, recvLog, usedAfterSend, closed>>

(* Recv takes the oldest message; it still works after Close, to drain. *)
Recv ==
    /\ queue # <<>>
    /\ LET m == Head(queue) IN
           /\ owner' = [owner EXCEPT ![m] = (@ \ {"queue"}) \cup {"receiver"}]
           /\ recvLog' = Append(recvLog, m)
    /\ queue' = Tail(queue)
    /\ UNCHANGED <<status, sentLog, usedAfterSend, afterClose, closed>>

(* A holder releases a message it owns: the receiver once it is done, or *)
(* the sender for a message it never sent.                               *)
Free(p, m) ==
    /\ p \in {"sender", "receiver"}
    /\ p \in owner[m]
    /\ owner' = [owner EXCEPT ![m] = @ \ {p}]
    /\ status' = [status EXCEPT ![m] = "freed"]
    /\ UNCHANGED <<queue, sentLog, recvLog, usedAfterSend, afterClose, closed>>

(* Shutdown: the sender closes the channel; no further Alloc or Send. *)
Close ==
    /\ ~closed
    /\ closed' = TRUE
    /\ UNCHANGED <<owner, status, queue, sentLog, recvLog, usedAfterSend, afterClose>>

(* Shutdown is complete once the queue is drained and every message is   *)
(* released.                                                             *)
Terminated ==
    /\ closed
    /\ queue = <<>>
    /\ \A m \in Msgs : owner[m] = {}

Next ==
    \/ \E m \in Msgs : Alloc(m) \/ SenderUse(m) \/ Send(m)
    \/ Recv
    \/ \E p \in Parties, m \in Msgs : Free(p, m)
    \/ Close
    \/ Terminated /\ UNCHANGED vars

(* Fairness: the receiver keeps draining, and each holder eventually     *)
(* releases what it holds. Nothing forces Close.                         *)
Fairness ==
    /\ WF_vars(Recv)
    /\ \A p \in Parties, m \in Msgs : WF_vars(Free(p, m))

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* At most one party holds a message at any time. *)
NoDoubleOwner == \A m \in Msgs : Cardinality(owner[m]) <= 1

(* The sender never touches a message after sending it. *)
NoUseAfterSend == usedAfterSend = {}

(* A live message always has an owner, and the queue owns what it holds. *)
OwnedWhileLive ==
    /\ \A m \in Msgs : (status[m] = "live") <=> (owner[m] # {})
    /\ \A i \in 1..Len(queue) : "queue" \in owner[queue[i]]

(* After Close the sender can neither allocate nor send. *)
NothingAfterClose == afterClose = {}

(* The receiver gets messages in the order they were sent. *)
FifoOrder == recvLog = SubSeq(sentLog, 1, Len(recvLog))

(* Once closed, the channel always finishes shutting down. *)
Drains == closed ~> Terminated
=============================================================================
