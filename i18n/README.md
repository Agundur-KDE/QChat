# Translations

English source strings use Qt `tr()`. German (`de`) and Persian (`fa`) are provided
as Qt Linguist TS files and compiled to embedded QM resources by CMake.

Persian is a draft, not a professionally reviewed translation. Review wording
and bidirectional layout before use. Preserve `%1` substitutions and literal
OpenPGP BEGIN/END identifiers. Fingerprints and ciphertext intentionally stay LTR.

Regenerate source strings with Qt 6 lupdate and edit with Qt Linguist. Qt's own
standard buttons may depend on installed Qt base translation catalogs.
