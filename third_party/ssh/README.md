# Embedded SSH provenance

This is the minimal SSH cryptography subset of [staticnet](https://github.com/azonenberg/staticnet),
pinned to commit `0b3e18c6077a9c3531a960316b67a549ec6fa541` (retrieved 2026-10-05).
Its BSD-3-Clause license is retained in `staticnet/LICENSE`, and source files retain
their original notices. `src/ssh_transport.cpp` adapts the upstream
`ssh/SSHTransportServer.cpp` exchange/framing design and also carries that license.

Bundled upstream files:

- `crypt/CryptoEngine.{cpp,h}`: BSD-3-Clause key derivation, Ed25519/X25519 abstraction.
- `contrib/tweetnacl_25519.{cpp,h}`: upstream's reduced TweetNaCl implementation;
  [TweetNaCl is public domain](https://tweetnacl.cr.yp.to/).
- `contrib/base64.{cpp,h}`: libb64, public domain as stated in its source headers.

Local changes to these files:

- Remove dependencies on staticnet's network headers; include standard headers.
- Encode SSH `mpint` shared secrets canonically, including leading zero stripping.
- Replace two left shifts of signed negative carries in TweetNaCl with equivalent
  multiplications, removing undefined C++ behavior found by UBSan.
- Match `crypto_sign_open`'s declaration to its `u64` (`uint64_t`) definition.
  The upstream `unsigned long long` declaration creates a different C++ symbol
  on Linux LP64 platforms, where `uint64_t` is `unsigned long`.

The TCP/IP stack, hardware drivers, SFTP, upstream circular FIFO, and unsafe
variable-length packet accessor classes are **not included**. The adapter uses
lwIP already present in this firmware, checked bounds, fixed-size buffers, and
one SSH channel with explicit receive/send window accounting. AES-GCM and SHA-256
use the Pico SDK's existing mbedTLS (Apache-2.0); entropy uses `pico_rand`.
Authenticated decryption uses a separate 2 KiB scratch buffer to respect the
mbedTLS API's non-overlap contract. The advertised channel packet size is 1 KiB;
encrypted transport packets are capped at 2 KiB, initial negotiation at 8 KiB.

The server implements SSH-2 version negotiation, Curve25519/SHA-256 key exchange,
Ed25519 host identity, AES-128-GCM encryption, password authentication, a PTY/shell
channel, terminal resize, and bounded input/output. There is no Unix shell,
port forwarding, exec, SFTP, compression, or public-key user authentication.
Client rekey requests receive an explicit disconnect asking the user to reconnect;
sessions are also capped at 12 hours or 256 MiB to bound key/sequence usage.

Authentication is limited to three password failures and a 60-second handshake.
After 30 seconds without SSH traffic, an authenticated client is sent an SSH
keepalive; failure to answer within 60 seconds releases the single connection.
There is no user inactivity timeout: thinking at a prompt keeps working.
Host identity is supplied by the firmware's persistent key storage, rather than
being embedded in the binary. Tests use an explicitly test-only seed/password.
`tests/test_ssh_transport.py` builds the exact engine against the SDK mbedTLS source
and tests it using the host's ordinary OpenSSH client, plus parser fuzzing under
AddressSanitizer and UndefinedBehaviorSanitizer.
The same command also exercises the actual lwIP adapter with SDK `pbuf` chain,
reference-count, and free implementations. TCP PCB operations are test doubles;
coverage includes transient buffer pressure, bounded prefix consumption of
coalesced TCP chains, shared references, error ownership, reconnect ordering,
and shutdown. An actual-engine regression covers an SSH packet tail sharing a
TCP chain with the next packet, and a connection that closes before its first
poll releases the session slot. This is separate from hardware testing.

ARM GCC `-fstack-usage` places the active signing call chain at approximately
5.1 KiB plus its small application/interrupt callers. SSH builds reserve a
12 KiB core-0 stack; the previous 2 KiB stack is insufficient for this backend.
