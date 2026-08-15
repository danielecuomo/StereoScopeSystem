# SMS hot-op profiling safe build

This build intentionally keeps the working Z80 dispatcher/diagnostic baseline and the opcode profiler, but does NOT replace any opcode handlers with experimental fast implementations. The previous hot-op handler optimization caused the game to stop displaying, so those semantic changes were reverted.

The goal of this build is to obtain reliable HOT OP / HOT CB / HOT ED counts without changing emulator behavior.
