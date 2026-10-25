# Models

`channel.tla` models a bounded single-producer, single-consumer channel that passes
ownership of each message from the sender, through the queue, to the receiver.
TLC checks the model, not the C. The C channel (`include/safec/chan.h`, SC-10) follows this table.

Run it with `make model`. The target needs Java 11 or later and `tla2tools.jar`
1.7.4 (TLC 2.19); `scripts/model.py` pins its SHA-256 and looks for it at
`$TLA2TOOLS`, `build/tools/tla2tools.jar`, then `~/.local/lib/tla2tools.jar`.
It checks `channel.cfg` once for each queue size in `MODEL_QUEUE_SIZES`
(default `1 3`) with four messages, and writes `build/model/queue-N.log`.
`tests/models/channel_test.py` plants a bug for each property and requires TLC to
report it.

## Actions

| Action | What it does | C function |
| --- | --- | --- |
| `Alloc(m)` | The sender takes a fresh message and owns it. Refused after `Close`. | Allocating a handle from the SC-09 handle table |
| `SenderUse(m)` | The sender reads or writes a message it holds. | Resolving the sender's handle through the SC-09 table |
| `Send(m)` | Appends `m` to a non-full, open queue and removes the sender as owner. | `sc_chan_send` (`SC_CHAN_FULL` and `SC_CHAN_CLOSED` refuse it), which invalidates the sender's handle |
| `Recv` | Moves the oldest message from the queue to the receiver. Works after `Close`, to drain. | `sc_chan_recv` (`SC_CHAN_NOSLOT` when the receiver's table is full: a C-only refusal the model does not have) |
| `Free(p, m)` | The receiver, or the sender for an unsent message, releases a message it owns. | Releasing the handle in the SC-09 table |
| `Close` | The sender shuts the channel down; no further `Alloc` or `Send`. | `sc_chan_close` |
| `Terminated` | Stutter once closed, drained and every message released. | None: `sc_chan_recv` reports closed and empty |

## Properties

| Property | Meaning | Planted bug that breaks it |
| --- | --- | --- |
| `NoDoubleOwner` | At most one party holds a message. | `Send` keeps the sender as owner |
| `NoUseAfterSend` | The sender never touches a message after sending it. | The same bug, with `NoDoubleOwner` off |
| `OwnedWhileLive` | A live message has an owner, and the queue owns what it holds. | `Free` leaves the owner set; `Send` hands straight to the receiver |
| `NothingAfterClose` | No `Alloc` or `Send` runs after `Close`. | `Send` or `Alloc` ignores `closed` |
| `FifoOrder` | The receiver gets messages in send order. | `Recv` takes the newest message |
| `TypeOK` | Includes `Len(queue) <= QueueSize`. | `Send` ignores the bound |
| `Drains` | Once closed, the queue empties and every message is released. Assumes weak fairness on `Recv` and `Free`. | The sender cannot free an unsent message after `Close` |
| Deadlock freedom (TLC default) | No state is stuck without a step. | `Recv` refused after `Close` |

`SenderUse` on an unsent message changes nothing, so it counts as a stutter step and
TLC's deadlock check cannot see a shutdown that stalls while the sender still holds a
message. `Drains` covers that case.

The model has one sender and one receiver. It does not model a receiver-side close,
blocking, or memory ordering; ThreadSanitizer covers the C for races (`make tsan`).
The first send after a stale handle is `SC_CHAN_STALE`, the C form of
`NoUseAfterSend`.

## Mailbox

`mailbox.tla` models the many-to-one counterpart: several senders, one receiver. The
C mailbox (`include/safec/mailbox.h`, SC-19) follows this table. Closing is the
receiver's action: it fixes the final count of accepted sends in one atomic step, so a
sender that races with it either delivers or keeps its handle. A send is two steps, as
in the C: it claims a place (`Send`), then writes the payload (`Publish`).

`scripts/model.py` checks `mailbox.cfg` at each sender count in `MODEL_SENDERS` (default
`2 3`) and each queue size in `MODEL_QUEUE_SIZES`, with four messages, and writes
`build/model/mailbox-senders-N-queue-M.log`. `tests/models/mailbox_test.py` plants a bug
for each property and requires TLC to report it.

| Action | What it does | C function |
| --- | --- | --- |
| `Alloc(s, m)` | Sender `s` takes a fresh message and owns it. Refused after `Close`. | Allocating a handle from the sender's SC-09 table |
| `SenderUse(s, m)` | A sender reads or writes a message it holds. | Resolving the sender's handle through its table |
| `Send(s, m)` | Claims the next place of a non-full, open mailbox and removes the sender as owner. | `sc_mbox_send`: the compare-and-swap on `tail` in `claim` (`SC_MBOX_FULL` and `SC_MBOX_CLOSED` refuse it, `SC_MBOX_STALE` comes first), then `sc_handle_free` |
| `Publish(m)` | The sender writes the payload and marks the place readable, closed or not. | The slot write and release store of `seq` at the end of `sc_mbox_send` |
| `Recv` | Moves the oldest place to the receiver once it is published. Works after `Close`, to drain. | `sc_mbox_recv` (`SC_MBOX_EMPTY` while the oldest place is unpublished; `SC_MBOX_NOSLOT` when the receiver's table is full: a C-only refusal the model does not have) |
| `Free(p, m)` | The receiver, or a sender for a message it holds, releases it. | Releasing the handle in the SC-09 table |
| `Close` | The receiver shuts the mailbox down; no further `Alloc` or `Send` claims a place. | `sc_mbox_close`: one `fetch_or` of the closed bit in `tail` |
| `ReportClosed` | Closed and every claimed place received. | `sc_mbox_recv` returning `SC_MBOX_CLOSED` |
| `Terminated` | Stutter once reported and every message released. | None |

| Property | Meaning | Planted bug that breaks it |
| --- | --- | --- |
| `NoDoubleOwner` | At most one party holds a message. | `Send` keeps the sender as owner; or a sender sends a message another sender holds |
| `NoUseAfterSend` | A sender never touches a message after sending it. | The first bug, with `NoDoubleOwner` off |
| `OwnedWhileLive` | A live message has an owner, and the queue owns what it holds. | Same as the channel |
| `NothingAfterClose` | No `Alloc` or `Send` runs after `Close`. | `Send` ignores `closed` |
| `FifoOrder` | The receiver gets messages in claim order, so each sender's in its send order. | `Recv` takes the newest place |
| `NoEarlyRecv` | The receiver never reads a place before its payload is published. | `Recv` ignores `published`: the ordering bug |
| `NoLostMessage` | Once `ReportClosed` ran, every accepted send was received. | `ReportClosed` ignores the queue |
| `TypeOK` | Includes `Len(queue) <= QueueSize`. | `Send` ignores the bound |
| `Drains` | Once closed, the mailbox empties and every message is released. Assumes weak fairness on `Publish`, `Recv` and `Free`. | `Publish` refused after `Close`; a sender cannot free after `Close` |

The model has no memory ordering. `NoEarlyRecv` stands for the release store of `seq`
paired with the acquire load in `sc_mbox_recv`; ThreadSanitizer (`make tsan`) checks
that pairing in the C, and `tests/gates/mailbox_test.py` plants a relaxed pair and
requires a data-race report. The C mailbox needs a power-of-two capacity (the model has
any size) and the receiver must be one thread at a time.

## Proofs for every size

TLC checks the models at small sizes. `channel_proofs.tla` and `mailbox_proofs.tla` prove
every safety invariant above (all but `Drains`) for any queue size, message set and,
for the mailbox, any set of senders. Each proves an inductive invariant whose key parts
are `sentLog = recvLog \o queue` and "a sent message never again belongs to a sender".
`make tlaps` (`scripts/tlaps.py`) checks both with tlapm and writes
`build/tlaps/<module>.log`; `tests/models/tlaps_test.py` plants a bug in each model and
requires its proof to fail. The mailbox proof assumes `"queue"` and `"receiver"` are not
sender names, which TLC's model values give for free.

## The C against the C11 memory model

The models say nothing about memory ordering. `make genmc` (`scripts/genmc.py`) runs
GenMC on `tests/genmc/`, whose harnesses include the real `stdlib/chan.c` and
`stdlib/mailbox.c` and assert the properties above at the C level: ownership moves with
each accepted send, FIFO, nothing lost once recv reports closed. GenMC explores every
execution RC11 allows at two or three senders and capacities up to four, and also fails
on a race on a non-atomic location (the ring, a slot's value, the handle tables).
`tests/gates/genmc_test.py` plants eighteen bugs, mostly weakened orderings, and
requires GenMC to reject each one.
