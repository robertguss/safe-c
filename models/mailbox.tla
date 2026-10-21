------------------------------ MODULE mailbox ------------------------------
(***************************************************************************)
(* A bounded multi-producer, single-consumer mailbox that passes ownership *)
(* of each message from one of several senders, through the queue, to the  *)
(* receiver. Senders run concurrently, so the model interleaves their      *)
(* steps; a send is two steps, claim a place and publish the payload, as   *)
(* in the C. TLC checks this model only; it says nothing about memory      *)
(* ordering. models/README.md maps each action to the C function that      *)
(* implements it.                                                          *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS
    Senders,    \* the sender threads, a set of model values
    QueueSize,  \* mailbox capacity
    Msgs        \* the messages the senders can allocate, a set of model values

ASSUME QueueSize \in Nat \ {0}
ASSUME IsFiniteSet(Senders) /\ Senders # {}
ASSUME IsFiniteSet(Msgs) /\ Msgs # {}

Parties == Senders \cup {"queue", "receiver"}

VARIABLES
    owner,          \* owner[m]: the parties holding message m
    status,         \* status[m]: "unused", "live" or "freed"
    queue,          \* claimed places, oldest first; published or not
    published,      \* messages whose payload the sender has written
    sentLog,        \* history: messages passed to Send, in claim order
    recvLog,        \* history: messages Recv returned, in order
    usedAfterSend,  \* history: messages a sender touched after sending them
    afterClose,     \* history: sender actions that ran after Close
    earlyRecv,      \* history: messages received before they were published
    closed,         \* the receiver has shut the mailbox down
    reported        \* the receiver has seen "closed and drained"

vars == <<owner, status, queue, published, sentLog, recvLog, usedAfterSend, afterClose,
          earlyRecv, closed, reported>>

Sent == {sentLog[i] : i \in DOMAIN sentLog}
Received == {recvLog[i] : i \in DOMAIN recvLog}

(* A history variable records a sender action that ran after Close. *)
NoteIfClosed(action) ==
    afterClose' = IF closed THEN afterClose \cup {action} ELSE afterClose

TypeOK ==
    /\ owner \in [Msgs -> SUBSET Parties]
    /\ status \in [Msgs -> {"unused", "live", "freed"}]
    /\ queue \in Seq(Msgs) /\ Len(queue) <= QueueSize
    /\ published \subseteq Msgs
    /\ sentLog \in Seq(Msgs) /\ recvLog \in Seq(Msgs)
    /\ usedAfterSend \subseteq Msgs
    /\ afterClose \subseteq {"Alloc", "Send"}
    /\ earlyRecv \subseteq Msgs
    /\ closed \in BOOLEAN /\ reported \in BOOLEAN

Init ==
    /\ owner = [m \in Msgs |-> {}]
    /\ status = [m \in Msgs |-> "unused"]
    /\ queue = <<>>
    /\ published = {}
    /\ sentLog = <<>>
    /\ recvLog = <<>>
    /\ usedAfterSend = {}
    /\ afterClose = {}
    /\ earlyRecv = {}
    /\ closed = FALSE
    /\ reported = FALSE

(* Sender s takes a fresh message and owns it. *)
Alloc(s, m) ==
    /\ ~closed
    /\ status[m] = "unused"
    /\ status' = [status EXCEPT ![m] = "live"]
    /\ owner' = [owner EXCEPT ![m] = {s}]
    /\ NoteIfClosed("Alloc")
    /\ UNCHANGED <<queue, published, sentLog, recvLog, usedAfterSend, earlyRecv, closed, reported>>

(* A sender reads or writes a message it holds. For an unsent message this *)
(* step changes nothing, so it is a stutter and TLC's deadlock check cannot *)
(* see a stuck shutdown; Drains can.                                        *)
SenderUse(s, m) ==
    /\ s \in owner[m]
    /\ usedAfterSend' = IF m \in Sent THEN usedAfterSend \cup {m} ELSE usedAfterSend
    /\ UNCHANGED <<owner, status, queue, published, sentLog, recvLog, afterClose, earlyRecv,
                   closed, reported>>

(* Send claims the next place in one atomic step and takes the message from *)
(* its sender. The payload is not readable until Publish.                   *)
Send(s, m) ==
    /\ ~closed
    /\ s \in owner[m]
    /\ Len(queue) < QueueSize
    /\ queue' = Append(queue, m)
    /\ owner' = [owner EXCEPT ![m] = (@ \ {s}) \cup {"queue"}]
    /\ sentLog' = Append(sentLog, m)
    /\ NoteIfClosed("Send")
    /\ UNCHANGED <<status, published, recvLog, usedAfterSend, earlyRecv, closed, reported>>

(* The sender writes the payload and marks the place readable. A sender    *)
(* that claimed a place always gets to do this, closed or not.             *)
Publish(m) ==
    /\ m \in {queue[i] : i \in DOMAIN queue}
    /\ m \notin published
    /\ published' = published \cup {m}
    /\ UNCHANGED <<owner, status, queue, sentLog, recvLog, usedAfterSend, afterClose, earlyRecv,
                   closed, reported>>

(* Recv takes the oldest place once it is published. It still works after *)
(* Close, to drain.                                                       *)
Recv ==
    /\ queue # <<>>
    /\ Head(queue) \in published
    /\ LET m == Head(queue) IN
           /\ owner' = [owner EXCEPT ![m] = (@ \ {"queue"}) \cup {"receiver"}]
           /\ recvLog' = Append(recvLog, m)
           /\ earlyRecv' = IF m \in published THEN earlyRecv ELSE earlyRecv \cup {m}
    /\ queue' = Tail(queue)
    /\ UNCHANGED <<status, published, sentLog, usedAfterSend, afterClose, closed, reported>>

(* A holder releases a message it owns: the receiver once it is done, or  *)
(* a sender for a message it never sent or that a refused send left it.   *)
Free(p, m) ==
    /\ p \in Senders \cup {"receiver"}
    /\ p \in owner[m]
    /\ owner' = [owner EXCEPT ![m] = @ \ {p}]
    /\ status' = [status EXCEPT ![m] = "freed"]
    /\ UNCHANGED <<queue, published, sentLog, recvLog, usedAfterSend, afterClose, earlyRecv,
                   closed, reported>>

(* Shutdown is the receiver's: one atomic step after which no sender can   *)
(* claim a place. Places claimed before it stay in the queue.              *)
Close ==
    /\ ~closed
    /\ closed' = TRUE
    /\ UNCHANGED <<owner, status, queue, published, sentLog, recvLog, usedAfterSend, afterClose,
                   earlyRecv, reported>>

(* Recv's closed answer: closed, and every claimed place has been received. *)
ReportClosed ==
    /\ closed
    /\ queue = <<>>
    /\ reported' = TRUE
    /\ UNCHANGED <<owner, status, queue, published, sentLog, recvLog, usedAfterSend, afterClose,
                   earlyRecv, closed>>

(* Shutdown is complete once closed, drained, reported and every message  *)
(* released.                                                              *)
Terminated ==
    /\ reported
    /\ \A m \in Msgs : owner[m] = {}

Next ==
    \/ \E s \in Senders, m \in Msgs : Alloc(s, m) \/ SenderUse(s, m) \/ Send(s, m)
    \/ \E m \in Msgs : Publish(m)
    \/ Recv
    \/ \E p \in Parties, m \in Msgs : Free(p, m)
    \/ Close
    \/ ReportClosed
    \/ Terminated /\ UNCHANGED vars

(* Fairness: each sender finishes a claimed send, the receiver keeps       *)
(* draining, and each holder eventually releases what it holds. Nothing    *)
(* forces Close.                                                            *)
Fairness ==
    /\ \A m \in Msgs : WF_vars(Publish(m))
    /\ WF_vars(Recv)
    /\ WF_vars(ReportClosed)
    /\ \A p \in Parties, m \in Msgs : WF_vars(Free(p, m))

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
(* At most one party holds a message at any time. *)
NoDoubleOwner == \A m \in Msgs : Cardinality(owner[m]) <= 1

(* A sender never touches a message after sending it. *)
NoUseAfterSend == usedAfterSend = {}

(* A live message always has an owner, and the queue owns what it holds. *)
OwnedWhileLive ==
    /\ \A m \in Msgs : (status[m] = "live") <=> (owner[m] # {})
    /\ \A i \in 1..Len(queue) : "queue" \in owner[queue[i]]

(* After Close no sender can allocate or send. *)
NothingAfterClose == afterClose = {}

(* The receiver gets messages in the order sends claimed their places; so  *)
(* the messages of one sender arrive in the order that sender sent them.   *)
FifoOrder == recvLog = SubSeq(sentLog, 1, Len(recvLog))

(* The receiver never reads a place before its payload is published. *)
NoEarlyRecv == earlyRecv = {}

(* Once recv says closed, every send the mailbox accepted was received. *)
NoLostMessage == reported => (queue = <<>> /\ Sent = Received)

(* Once closed, the mailbox always finishes shutting down. *)
Drains == closed ~> Terminated
=============================================================================
