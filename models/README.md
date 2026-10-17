# Models

`channel.tla` models a bounded single-producer, single-consumer channel that passes
ownership of each message from the sender, through the queue, to the receiver.
TLC checks the model, not the C. The C channel (SC-10) follows this table, and each
action gets one test there.

Run it with `make model`. The target needs Java 11 or later and `tla2tools.jar`
1.7.4 (TLC 2.19); `scripts/model.py` pins its SHA-256 and looks for it at
`$TLA2TOOLS`, `build/tools/tla2tools.jar`, then `~/.local/lib/tla2tools.jar`.
It checks `channel.cfg` once for each queue size in `MODEL_QUEUE_SIZES`
(default `1 3`) with four messages, and writes `build/model/queue-N.log`.
`tests/models/channel_test.py` plants a bug for each property and requires TLC to
report it.

## Actions

| Action | What it does | C function (planned) |
| --- | --- | --- |
| `Alloc(m)` | The sender takes a fresh message and owns it. Refused after `Close`. | Allocating a handle from the SC-09 handle table |
| `SenderUse(m)` | The sender reads or writes a message it holds. | Resolving the sender's handle through the SC-09 table |
| `Send(m)` | Appends `m` to a non-full, open queue and removes the sender as owner. | `sc_chan_send`, which invalidates the sender's handle |
| `Recv` | Moves the oldest message from the queue to the receiver. Works after `Close`, to drain. | `sc_chan_recv` |
| `Free(p, m)` | The receiver, or the sender for an unsent message, releases a message it owns. | Releasing the handle in the SC-09 table |
| `Close` | The sender shuts the channel down; no further `Alloc` or `Send`. | `sc_chan_close` |
| `Terminated` | Stutter once closed, drained and every message released. | None: `sc_chan_recv` reports closed and empty |

## Properties

| Property | Meaning | Planted bug that breaks it |
| --- | --- | --- |
| `NoDoubleOwner` | At most one party holds a message. | `Send` keeps the sender as owner |
| `NoUseAfterSend` | The sender never touches a message after sending it. | The same bug, with `NoDoubleOwner` off |
| `OwnedWhileLive` | A live message has an owner, and the queue owns what it holds. | `Free` leaves the owner set |
| `TypeOK` | Includes `Len(queue) <= QueueSize`. | `Send` ignores the bound |
| Deadlock freedom (TLC default) | Shutdown can always finish: only `Terminated` may stutter. | `Recv` refused after `Close` |

The model has one sender and one receiver. It does not model a receiver-side close,
blocking, or memory ordering; ThreadSanitizer covers the C for races (SC-10).
