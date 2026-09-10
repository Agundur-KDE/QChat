# QChat

Local Qt 6 application for encrypting and decrypting short text messages with OpenPGP.
**No transport, no server, no user account, no message history.**

Status: working experimental prototype, **not independently security audited**.
Do not rely on it as protection against state seizure of devices.

## Running

On the development machine:

```sh
/home/alec/projects/QChat/build/qchat --language en
```

Languages are `en`, `de`, and `fa`. Persian is a complete **translation draft**
that must be reviewed by a native speaker before sensitive use. The Persian UI
is laid out right to left; check codes and encrypted blocks remain left to right.
Choose the language at the top of the window; it takes effect after restart.

## Workflow for two people

1. Both people set up QChat. GnuPG asks for a passphrase in a separate Pinentry
   window. No email address or real name is required. The passphrase protects the
   private identity; QChat itself never receives it.
2. Both copy **My contact card** and send it through their existing channel.
   Contact cards are public, but they can reveal contact relationships.
3. Use **Add contact** to choose a local nickname and paste the card. A contact
   card contains only the public key.
4. Use **Check contact** to compare the complete check code through an
   independent route, such as an HTTPS website that is already authenticated.
   Both sides must check their respective association. The same unprotected chat
   alone is not an independent verification route. A check mark does not replace
   the actual comparison.
5. Choose a contact, write the text, select **Encrypt message**, copy the
   encrypted block, and send it through the existing channel. QChat signs it
   automatically. Your own identity is also included as a recipient so you can
   read the encrypted copy later.
6. To read a message, paste the complete block into **Read** and select
   **Decrypt message**. Decrypted content is shown; a valid signature from a
   checked contact is additionally marked as authenticated. The selected contact
   in the writing view does **not** determine the detected sender.

QChat may request the GnuPG passphrase more than once: when opening the app,
signing, and decrypting. The agent is configured to cache it only briefly.
Contacts are stored locally as an OpenPGP message encrypted to the local identity
and loaded only after unlocking. QChat does not write plaintext to disk, but Qt,
the editor, and the operating system may still retain copies. Clipboard managers
may keep external history.

## Locking

- **Esc / Lock now** clears the views and best-effort stops only the GnuPG agent
  belonging to the QChat directory.
- A fixed session ends after **3 minutes**, including while typing or moving the
  mouse. Unsaved text is lost. This is not an inactivity timer.
- Results from operations still running are discarded after locking.
- On Linux, a new USB device triggers a lock when udev monitoring is available.
  This is detection after connection, **not preventive device blocking**. Docks
  and reconnects may also trigger it. Sandboxes may not provide the event, and it
  does not protect against devices that were already present.
- Standard GNOME/MATE/freedesktop lock signals and logind sleep notifications
  are handled when the desktop provides them.
- There is **no secure-delete button and no guarantee of forensic erasure**.
  Preventive USB policy requires a separate tool such as USBGuard.

## Building on Ubuntu / Trisquel

Requires Qt >= 6.2, CMake >= 3.21, GPGME, GnuPG 2.2+, graphical Pinentry,
libudev, and a C++17 compiler. On distributions providing these packages:

```sh
sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-tools-dev \
  qt6-tools-dev-tools libgpgme-dev libudev-dev gnupg pinentry-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/qchat
```

The exact Ubuntu/Trisquel version has **not** been tested here. Native testing
was done with Qt 6.11.2, GPGME 2.2.0, and GnuPG 2.5.22 on openSUSE. The binary
built here is not a distribution package. GnuPG/Pinentry, the dedicated agent,
and optional system interfaces still need their own integration and testing in
the experimental Flatpak.

## Beta releases

Pushing a tag matching `v*` starts the GitHub Actions beta-release workflow. It
runs the build and test suite and publishes three Linux artifacts to a private
GitHub release:

- `.tgz`: a compressed directory bundle containing the QChat binary and desktop
  entry;
- `.bin`: a self-extracting form of the same bundle;
- `.flatpak`: an experimental Flatpak bundle.

The Flatpak is a packaging experiment and has not replaced native testing of
GnuPG, Pinentry, the dedicated agent, or the optional system interfaces.

## Data and limitations

QChat uses `QStandardPaths::AppLocalDataLocation`, which is usually
`~/.local/share/QChat/QChat/` on Linux. It does **not** use the normal
`~/.gnupg` directory.

- `gnupg/`: public keys, passphrase-protected private keys, GnuPG metadata, and
  local configuration.
- `contacts.json`: locally encrypted nicknames, public fingerprints, and check
  status. The filename, existence, and size remain visible metadata.
- `preferences.ini`: language; `qchat.lock`: lock file preventing parallel
  instances.

Directory permissions are restricted to the current user. This is not a
replacement for full-disk encryption. See [SECURITY.md](SECURITY.md) for the
threat model.

There is no automatic backup, private-key export, key rotation, or restore UI.
Losing the identity or its passphrase may make old messages unreadable. Created
identities expire after one year. A new key is treated as a new, unchecked
contact card.

## Development and tests

Tests use only temporary throwaway identities. Their empty passphrases belong to
the test environment and are not used for normal setup. GnuPG needs local agent
sockets; heavily restricted sandboxes may prevent the tests from running.

```sh
QT_QPA_PLATFORM=offscreen QCHAT_TEST_ARTIFACTS="$PWD/artifacts" ./build/window_test
./build/qchat --smoke-test --language fa
lupdate6 src -ts i18n/qchat_de.ts i18n/qchat_fa.ts
```

On some distributions the translation tool is called `lupdate` or is located
under `/usr/lib/qt6/bin/`. The tests cover cryptographic exchange, wrong
recipients, corrupted messages, missing or unknown signatures, rejected private
key imports, contact checking, locking during active operations, and German/
Persian rendering. They are not a security audit.
