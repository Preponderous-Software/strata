# The drive channel (`src/core/drive/`)

Injected events, the frame clock and the drive channel (ABI 1.12; the wire
protocol is in [docs/DRIVE.md](../../../docs/DRIVE.md)), laid out as a core
with ports and adapters. The core holds the protocol and does no I/O; the
bytes and the time come in through two small interfaces.

```text
Engine (src/engine.cpp) ──> service ──> Drive (core) ──> ports <── adapters
                                         │                 HarnessLink      UnixSocketLink
                         settings ───────┘                 MonotonicClock   SteadyClock
                         model (values)
```

| File | Layer | What it holds |
|---|---|---|
| `model.hpp` | model | `ClockMode`, `DriveSettings`, `Received`, `Sent`, the limits |
| `settings.{hpp,cpp}` | core | `parseDriveSettings`: the `STRATA_DRIVE*` values to settings and warnings, no `getenv` |
| `drive.{hpp,cpp}` | core | `Drive`: event queue, free and stepped clocks, grants, every op of the protocol |
| `ports/harness_link.hpp` | port | `HarnessLink`: `receive(block)` and `send(data)` |
| `ports/monotonic_clock.hpp` | port | `MonotonicClock`: `now()` in seconds |
| `adapters/unix_socket_link.{hpp,cpp}` | adapter | `UnixSocketLink`: the client end of a Unix stream socket (POSIX) |
| `adapters/steady_clock.{hpp,cpp}` | adapter | `SteadyClock`: `std::chrono::steady_clock` |
| `service.{hpp,cpp}` | composition root | `createDrive()`, `connectFromEnvironment(drive)`: reads the environment, connects, logs |

| Port | Production adapter | Test fake (`tests/unit/fakes/`) |
|---|---|---|
| `HarnessLink` | `UnixSocketLink` | `FakeHarnessLink` |
| `MonotonicClock` | `SteadyClock` | `FakeMonotonicClock` |

Each port's contract is in its header. `tests/unit/test_drive_ports.cpp` runs
the same contract against the adapter and the fake.

**Rules.** The model, `settings`, `drive` and `ports/` never include
`adapters/` or `service`; each port and adapter header declares one class.
`tests/python/test_drive_layers.py` enforces both. `Hooks` (drawable size,
screenshots, frame stats) stay a set of callbacks the engine passes in.

**Adding a transport** (say, a Windows named pipe or a browser message
channel): one new file pair in `adapters/` implementing `HarnessLink`, and one
branch in `service.cpp` that builds it. The core does not change.
