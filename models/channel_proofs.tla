--------------------------- MODULE channel_proofs ---------------------------
(***************************************************************************)
(* TLAPS proof that the safety invariants of models/channel.tla hold for   *)
(* every queue size and every message set, not just the bounds TLC checks. *)
(* scripts/tlaps.py runs it; the liveness property Drains stays with TLC.  *)
(***************************************************************************)
EXTENDS channel, TLAPS, SequenceTheorems, FiniteSetTheorems

(* channel.tla's own ASSUME, named so the proofs can cite it. *)
ASSUME QueueSizeNat == QueueSize \in Nat \ {0}

NoDup(s) == \A i, j \in 1..Len(s) : s[i] = s[j] => i = j

(* The inductive invariant. The log equation sentLog = recvLog \o queue and  *)
(* "a sent message is never again a sender's" carry FIFO, no loss and no use *)
(* after send; the rest keeps ownership single and the queue duplicate-free. *)
Inv ==
    /\ TypeOK
    /\ \A m \in Msgs : owner[m] = {} \/ \E p \in Parties : owner[m] = {p}
    /\ \A m \in Msgs : (status[m] = "live") <=> (owner[m] # {})
    /\ \A m \in Msgs : status[m] = "unused" => m \notin Sent
    /\ \A i \in 1..Len(queue) : owner[queue[i]] = {"queue"}
    /\ NoDup(queue)
    /\ sentLog = recvLog \o queue
    /\ \A m \in Sent : owner[m] \cap {"sender"} = {}
    /\ usedAfterSend = {} /\ afterClose = {}

LEMMA InitInv == Init => Inv
<1> SUFFICES ASSUME Init PROVE Inv
    OBVIOUS
<1>1. <<>> \in Seq(Msgs) /\ Len(<<>>) = 0 /\ <<>> \o <<>> = <<>>
    BY EmptySeq, ConcatEmptySeq
<1>2. Sent = {}
    BY DEF Init, Sent
<1> QED
    BY <1>1, <1>2, QueueSizeNat DEF Init, Inv, TypeOK, NoDup

LEMMA NextInv == Inv /\ [Next]_vars => Inv'
<1> SUFFICES ASSUME Inv, [Next]_vars PROVE Inv'
    OBVIOUS
<1> USE DEF Inv, TypeOK, NoDup
<1>1. ASSUME NEW m \in Msgs, Alloc(m) PROVE Inv'
    BY <1>1 DEF Alloc, NoteIfClosed, Sent, Parties
<1>2. ASSUME NEW m \in Msgs, SenderUse(m) PROVE Inv'
    BY <1>2 DEF SenderUse, Sent, Parties
<1>3. ASSUME NEW m \in Msgs, Send(m) PROVE Inv'
    <2>1. owner[m] = {"sender"}
        BY <1>3 DEF Send, Parties
    <2>2. m \notin Sent
        BY <2>1 DEF Sent
    <2>3. \A i \in 1..Len(queue) : queue[i] # m
        BY <2>1
    <2>4. Append(queue, m) \in Seq(Msgs) /\ Len(Append(queue, m)) = Len(queue) + 1
          /\ \A i \in 1..Len(queue) : Append(queue, m)[i] = queue[i]
          /\ Append(queue, m)[Len(queue) + 1] = m
        BY AppendProperties
    <2>5. Append(recvLog \o queue, m) = recvLog \o Append(queue, m)
        BY AppendIsConcat, ConcatAssociative, ConcatProperties
    <2>6. Sent' = Sent \cup {m}
        <3>1. Range(Append(sentLog, m)) = Range(sentLog) \cup {m}
            BY AppendProperties
        <3> QED
            BY <1>3, <3>1 DEF Send, Sent, Range
    <2>7. TypeOK'
        <3>1. owner' \in [Msgs -> SUBSET Parties]
            BY <1>3 DEF Send, Parties
        <3>2. sentLog' \in Seq(Msgs)
            BY <1>3, AppendProperties DEF Send
        <3>3. queue' \in Seq(Msgs) /\ Len(queue') <= QueueSize
            BY <1>3, <2>4, LenProperties, QueueSizeNat DEF Send
        <3>4. afterClose' = afterClose
            BY <1>3 DEF Send, NoteIfClosed
        <3> QED
            BY <1>3, <3>1, <3>2, <3>3, <3>4 DEF Send
    <2>8. (\A i \in 1..Len(queue) : owner[queue[i]] = {"queue"})'
        BY <1>3, <2>1, <2>3, <2>4 DEF Send
    <2>9. NoDup(queue)'
        BY <1>3, <2>3, <2>4 DEF Send
    <2>10. (\A x \in Sent : owner[x] \cap {"sender"} = {})'
        <3>1. owner'[m] = {"queue"}
            BY <1>3, <2>1 DEF Send
        <3>0. Sent \subseteq Msgs
            BY DEF Sent
        <3>2. \A x \in Sent : x # m /\ owner'[x] = owner[x]
            BY <1>3, <2>2, <3>0 DEF Send
        <3> QED
            BY <2>6, <3>1, <3>2
    <2> QED
        BY <1>3, <2>1, <2>2, <2>5, <2>6, <2>7, <2>8, <2>9, <2>10
        DEF Send, NoteIfClosed, Parties
<1>5. ASSUME Recv PROVE Inv'
    <2> DEFINE h == Head(queue)
    <2>1. queue # <<>> /\ queue \in Seq(Msgs)
        BY <1>5 DEF Recv
    <2>2. h = queue[1] /\ Len(queue) >= 1 /\ h \in Msgs
        BY <2>1, HeadTailProperties, LenProperties, EmptySeq
    <2>3. owner[h] = {"queue"}
        BY <2>2
    <2>4. Tail(queue) \in Seq(Msgs) /\ Len(Tail(queue)) = Len(queue) - 1
          /\ \A i \in 1..Len(Tail(queue)) : Tail(queue)[i] = queue[i + 1]
        BY <2>1, HeadTailProperties
    <2>5. \A i \in 1..Len(Tail(queue)) : Tail(queue)[i] # h
        BY <2>2, <2>4
    <2>6. Append(recvLog, h) \o Tail(queue) = recvLog \o queue
        <3>1. queue = <<h>> \o Tail(queue)
            BY <2>1, <2>2, <2>4, ConcatProperties, SeqEqual
        <3>2. Append(recvLog, h) = recvLog \o <<h>>
            BY <2>2, AppendIsConcat
        <3> QED
            BY <2>2, <2>4, <3>1, <3>2, ConcatAssociative
    <2>7. Sent' = Sent
        BY <1>5 DEF Recv, Sent
    <2>8. TypeOK'
        <3>1. Len(Tail(queue)) <= QueueSize
            BY <2>1, <2>2, <2>4, LenProperties, QueueSizeNat
        <3>2. recvLog' \in Seq(Msgs)
            BY <1>5, <2>2, AppendProperties DEF Recv
        <3>3. owner' \in [Msgs -> SUBSET Parties]
            BY <1>5 DEF Recv, Parties
        <3> QED
            BY <1>5, <2>4, <3>1, <3>2, <3>3, <2>2 DEF Recv
    <2>9. (\A i \in 1..Len(queue) : owner[queue[i]] = {"queue"})'
        BY <1>5, <2>4, <2>5 DEF Recv
    <2>10. (\A x \in Msgs : owner[x] = {} \/ \E p \in Parties : owner[x] = {p})'
        BY <1>5, <2>3 DEF Recv, Parties
    <2>11. (\A x \in Msgs : (status[x] = "live") <=> (owner[x] # {}))'
        BY <1>5, <2>3 DEF Recv
    <2>12. (\A x \in Sent : owner[x] \cap {"sender"} = {})'
        <3>1. owner'[h] = {"receiver"}
            BY <1>5, <2>2, <2>3 DEF Recv
        <3>2. \A x \in Msgs : x # h => owner'[x] = owner[x]
            BY <1>5 DEF Recv
        <3>3. Sent \subseteq Msgs
            BY DEF Sent
        <3> QED
            BY <2>7, <3>1, <3>2, <3>3
    <2> QED
        BY <1>5, <2>1, <2>4, <2>6, <2>7, <2>8, <2>9, <2>10, <2>11, <2>12
        DEF Recv
<1>6. ASSUME NEW p \in Parties, NEW m \in Msgs, Free(p, m) PROVE Inv'
    <2>1. owner[m] = {p} /\ p # "queue"
        BY <1>6 DEF Free, Parties
    <2>2. \A i \in 1..Len(queue) : queue[i] # m
        BY <2>1
    <2> QED
        BY <1>6, <2>1, <2>2 DEF Free, Sent, Parties
<1>7. ASSUME Close PROVE Inv'
    BY <1>7 DEF Close, Sent
<1>9. ASSUME UNCHANGED vars PROVE Inv'
    BY <1>9 DEF vars, Sent
<1> QED
    BY <1>1, <1>2, <1>3, <1>5, <1>6, <1>7, <1>9 DEF Next, Terminated

THEOREM Safety ==
    Spec => [](TypeOK /\ NoDoubleOwner /\ NoUseAfterSend /\ OwnedWhileLive
               /\ NothingAfterClose /\ FifoOrder)
<1>1. Spec => []Inv
    BY InitInv, NextInv, PTL DEF Spec
<1>2. Inv => NoDoubleOwner
    BY FS_EmptySet, FS_Singleton DEF Inv, NoDoubleOwner
<1>3. Inv => FifoOrder
    <2> SUFFICES ASSUME Inv PROVE FifoOrder
        OBVIOUS
    <2>1. recvLog \in Seq(Msgs) /\ queue \in Seq(Msgs)
        BY DEF Inv, TypeOK
    <2>2. CASE recvLog = <<>>
        BY <2>1, <2>2, SubSeqEmpty DEF Inv, FifoOrder
    <2>3. CASE recvLog # <<>>
        <3>1. SubSeq(recvLog \o queue, 1, Len(recvLog)) = SubSeq(recvLog, 1, Len(recvLog))
            BY <2>1, <2>3, SubSeqOfConcat1, LenProperties, EmptySeq
        <3>2. SubSeq(recvLog, 1, Len(recvLog)) = recvLog
            BY <2>1, SubSeqRestrict, SubSeqProperties, SeqEqual
        <3> QED
            BY <3>1, <3>2 DEF Inv, FifoOrder
    <2> QED
        BY <2>2, <2>3
<1>5. Inv => OwnedWhileLive
    BY DEF Inv, OwnedWhileLive
<1>6. Inv => TypeOK /\ NoUseAfterSend /\ NothingAfterClose
    BY DEF Inv, NoUseAfterSend, NothingAfterClose
<1> QED
    BY <1>1, <1>2, <1>3, <1>5, <1>6, PTL
=============================================================================
