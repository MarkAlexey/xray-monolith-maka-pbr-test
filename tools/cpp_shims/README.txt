Stub headers for tools/check_cpp.py ONLY. They stand in for MSVC-only library headers
(Concurrency Runtime, stdext hash containers) so clang on Linux can parse the engine. They
forward to std:: types and are never built into the game. Behaviour is irrelevant here; only
the names and template signatures the engine uses need to exist.
