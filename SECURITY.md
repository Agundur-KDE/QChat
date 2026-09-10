# Security model — QChat 0.1

QChat is an experimental OpenPGP text helper. It is not ready to be relied on as
a protection against targeted state surveillance or device forensics.

## Intended protection

Confidentiality and sender authentication for copied message blocks, assuming
uncompromised endpoints, trustworthy software and a correctly verified public
key. GPGME delegates all cryptography to the installed GnuPG implementation.
Application code does not implement cryptographic algorithms or receive a
user's passphrase. Pinentry and gpg-agent handle secret-key access.

Each message is encrypted to the selected confirmed contact and the sender,
and signed. `GPGME_ENCRYPT_ALWAYS_TRUST` is used only because contact verification
is maintained in the app rather than the OpenPGP web of trust. The UI refuses
unconfirmed recipients. Decrypted content is shown after successful decryption,
but the UI clearly marks unsigned or unverified messages as unauthenticated.
Unencrypted signed-only inputs, unauthenticated legacy ciphertext and
unusable/expired/revoked keys fail closed. A valid signature from an unknown or
unconfirmed key is shown as content but is not treated as an authenticated identity.

No automatic key retrieval/import over the network is enabled. QChat has no
network transport code. Native dependencies are not themselves a network sandbox.
Import parses a single public card, validates it in an isolated temporary
keyring, rejects private material, then re-exports only public key data before
importing into the persistent keyring. Size limits restrict input and output.

## At-rest exposure

Public keys, contact names, fingerprints and verification state remain on disk.
They are not encrypted by QChat. GnuPG's own private-key files are passphrase
protected. A UI lock is not a cryptographic container for contact metadata.
Full-disk encryption configured before sensitive use is strongly relevant.

QChat does not intentionally persist message plaintext, draft text or history.
This does not mean no traces exist: Qt allocations, swap/hibernation, clipboard
history, filesystem backups, the external channel and endpoint malware remain.
Core dumps are disabled and Linux PR_SET_DUMPABLE is cleared on a best-effort
basis. These measures do not defeat privileged adversaries or guarantee memory
zeroization. QString/QByteArray copies cannot be comprehensively wiped here.

The dedicated gpg-agent has short caches and external caching disabled. App lock
attempts to stop that agent and clears visible content; failures are reported.
A timer uses a fixed session deadline independent of user activity. Workers may
briefly retain content while finishing; an epoch check discards stale results.
Locking is not a secure erase operation. There is no destructive panic action.

udev detection requests a lock after a new USB device appears. It is not device
authorization and has a race window. Use a separately configured system policy
such as USBGuard if needed. DBus screen-lock notifications vary by desktop.
Sleep notifications do not hold an inhibitor and cannot guarantee cleanup before
sleep. A suspended or unlocked encrypted machine is not equivalent to a powered-off
machine. Enforcement and cache behavior require testing on each target platform.

## Explicit non-goals and unresolved work

- No anonymity, traffic masking, anti-replay protocol or real-world identity proof.
- No forward secrecy: later compromise of a usable private key can expose recorded
  traffic. A local purge cannot revoke copies on other systems.
- No defense against root, malware, physical coercion or already unlocked devices.
- No authenticated encrypted local metadata store. Local file modification can
  change verification state; filesystem permissions do not stop a same-user attacker.
- No certified secure deletion or plausible-deniability claim.
- No backup/restore, renewal or revocation UI; identity expiry is one year.
- No encrypted first-contact card workflow yet; exchange public contact cards and
  verify them through an authenticated route before normal messages.
- No protection against maliciously crafted input exploiting a dependency bug.
  Keep Qt, GnuPG, GPGME and the OS patched.
- No Flatpak package yet; sandbox integration must be designed and tested rather
  than granting broad host filesystem/process access.
- Persian is a draft translation and needs native review, including safety wording.

Before sensitive deployment: independent code/security review, target-platform
passphrase/agent tests, USB/desktop-lock tests, packaging review, threat-model review
with the person using it, and a decision whether an established messenger is a
better fit. Do not interpret the included tests as that review.
