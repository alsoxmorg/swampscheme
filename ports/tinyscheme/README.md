# TinyScheme port plan

TinyScheme does not provide the same Guile-style module and foreign-extension
loader. The intended port is therefore a small C loadable primitive (or a
TinyScheme host patch) that registers the same `termbox` command dispatcher
and constants against the TinyScheme API.

The shared contract is documented in `ports/README.md` and the implementation
should continue to use the repository's `termbox2.h`. Keep the TinyScheme
adapter separate from the Guile extension: the runtimes have different value,
error, and registration APIs.

Porting checklist:

1. register `termbox` as a variadic primitive;
2. convert TinyScheme strings/numbers to Termbox arguments;
3. return flat event property lists;
4. export the same color and key constants;
5. add a TinyScheme build/run smoke test;
6. preserve `init -> draw -> present -> poll -> shutdown` lifecycle.
