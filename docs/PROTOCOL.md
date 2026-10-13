# Native Minecraft 1.8.9 transport (protocol 47)

`mc::net::Client` is a native C++17 Windows TCP implementation. It does not launch
Java and does not emulate a newer protocol through ViaVersion. The supplied MCP
`PacketBuffer`, `MessageDeserializer2`, `NettyCompression*`, `CryptManager`,
`NetworkManager`, `NetHandlerLoginClient`, and login/status packet classes are the
reference.

Implemented:

- Big-endian scalar fields, signed 32-bit VarInt and 64-bit VarLong, UTF-8 strings
  with Java UTF-16 character-count limits, bounded byte arrays.
- Three-byte/21-bit packet length, TCP fragmentation and coalescing, explicit
  2 MiB decompression maximum, exact zlib length/checksum validation, bounded
  receive/send queues. Compression control takes effect before the next frame,
  including when it was received in the same TCP read.
- IPv4/IPv6 TCP, bounded DNS/connect operations, default-port Minecraft SRV
  lookup with fallback, nonblocking send/receive, TCP_NODELAY, partial sends,
  login/read timeouts, per-poll work limits.
- Protocol 47 handshake, Login Start, Disconnect, Enable Compression, Login
  Success, encryption request/response. Successful offline-mode login is allowed
  only when the server itself does not request online authentication.
- RSA X.509 public-key decoding and PKCS#1 v1.5 encryption, secure random
  128-bit shared secret, Minecraft's signed two's-complement SHA-1 hexadecimal
  server hash, continuous AES-128-CFB8 streams with key-as-IV. The encryption
  response is plaintext; subsequent wire bytes are encrypted.
- Online-mode `POST https://sessionserver.mojang.com/session/minecraft/join`
  with supplied Minecraft access token and profile UUID. WinHTTP validates TLS
  certificate chain and hostname, TLS 1.2 is required, redirects are disabled,
  only HTTP 204 succeeds. The request runs in a bounded worker rather than
  blocking rendering. Credentials and service response bodies are never logged.
- Server-list status request and ping/pong matching; result preserves full JSON.
- Play Keep Alive is answered internally and is not returned by `poll()`.
  Play Set Compression (0x46) is likewise applied internally before decoding
  subsequent coalesced frames.
  Play Disconnect is returned once, then state becomes Disconnected and `error()`
  contains the server's chat-component JSON.

The application supplies Client Settings, brand and other gameplay packets.
`Game` in `game.cpp` owns that layer. A successful login is not evidence of
movement parity, full gameplay support, or compatibility with an anti-cheat.

## Authentication

`Auth::fromEnvironment()` reads `MC_ACCESS_TOKEN` and `MC_UUID`. The UUID can be
32 hexadecimal digits or the standard hyphenated form. Both values must be
present and valid if either variable is set. The token must be a current
Minecraft Services access token for the selected legitimate Java Edition
profile; a Microsoft account token by itself is not sufficient.

The project does not yet implement Microsoft device-code login, token refresh,
account selection, profile-name lookup or secure persistent token storage. Pass
the authenticated profile's actual username to `connect`. Do not place tokens
in command-line arguments, checked-in scripts, diagnostics or screenshots.
Missing/rejected credentials fail closed on an online-mode server. No server
authentication bypass is implemented.

## Validation

`tests/protocol_tests.cpp` verifies known wire encodings, 10,000 randomized
signed variable-integer round trips, truncation and overflow failures, UTF-16
limits, fragmented/coalesced frames, compression transitions, packet-size
boundaries and malformed zlib rejection (24,280 checks at this revision).

`tests/protocol_integration.py` uses an independent Python server on loopback,
covering 11 scenarios: offline compression disabled/zero/64/1,000,000,
encrypted authentication at zero/64, missing and rejected authorization,
malformed compression, status and incorrect pong. Python's cryptography
implementation decrypts RSA fields, independently computes the signed SHA-1
hash, and verifies both directions of AES-CFB8, keepalive and compressed payloads.

The integration executable is compiled separately with `MC_PROTOCOL_TESTING`.
That macro enables an explicit mock session-service hook only in the test
binary. The production client has no hook and always calls the real service.
Real online-mode credentials and service authorization have not been exercised.

Build example with the locally available GCC 8.3 toolchain:

```powershell
$env:PATH = 'D:\Strawberry\c\bin;' + $env:PATH
& D:\Strawberry\c\bin\g++.exe -std=c++17 -O2 -DMC_PROTOCOL_TESTING -I include src/protocol.cpp tests/protocol_tests.cpp -o tests/protocol_tests.exe -lcrypto -lz -lws2_32 -lwinhttp -ldnsapi
& .\tests\protocol_tests.exe
python tests/protocol_integration.py
```

The Python integration suite requires `cryptography`. The C++ implementation
uses the local zlib and OpenSSL 1.1.1 import libraries; runtime DLLs must accompany
the executable or be on PATH. WinHTTP and DNSAPI are Windows system libraries.

On 2026-10-08, an authorized read-only native status probe of
`mc.tinypx.net:25565` succeeded: A record `43.248.189.37`, no Minecraft SRV
record, returned version `TinyBungeeCord`, protocol `47`, and measured ping
14 ms. Status does not reveal the proxy's backend implementation or online-mode
requirement. This result alone does not establish gameplay compatibility.

Later authorized testing established a proxy-specific admission flow: an
immediate fresh connection with the same username after the initial encryption
request was accepted through normal offline Login Success. The client did not
send a fabricated session response; the server chose the second connection's
login mode. There is no vanilla "second join" bit or protocol bypass.

The resulting play capture contained an End-dimension S21 with 49,408 payload
bytes where the dimension's section mask requires 41,216. Original Java
`Chunk.fillChunk` consumes the required no-skylight prefix (including its biome
offset) and ignores the remaining 8,192 bytes. The world module now follows this
behavior with bounded input and a captured Java differential regression; it
does not infer skylight from the extra bytes. This is separate from exact zlib
frame validation.

## Explicit limits

- Only Java protocol 47 is implemented; no Bedrock, Forge/FML handshake, legacy
  pre-1.7 server-list query, or proxy-specific protocol extensions.
- The public API accepts separate host and port. GUI/CLI parsing of bracketed
  IPv6 plus port is the application's responsibility. IDN-to-punycode conversion
  is not implemented; pass an ASCII/punycode hostname.
- SRV selection takes the first resolver record, as the 1.8.9 Java source does.
  The socket's overall connect deadline includes SRV/DNS work. Timed-out Windows
  DNS worker calls can finish in the background and own all of their state.
- Malformed UTF-8, overflowing VarInts, trailing zlib bytes and inflated-size
  mismatches are rejected more strictly than some historical Netty paths. Valid
  vanilla packets retain identical byte semantics.
- Gameplay packet parsing and unsupported gameplay behavior are tracked in
  `PLAY.md` and the project coverage inventory, not implied by transport support.
