# Preserved Yoga macOS installer scripts

These are the existing v2 GPU installer and v3 HookCase repair workflow, not a
newly validated clean-install package. Full original archives are release assets.
See the [installation guide](../../readme.md) and
[recovery instructions](../../docs/yoga/RECOVERY.md).

This directory alone has no `payload/` or `SHA256SUMS`, so it is **not sufficient
to install graphics**. Public source copies have serial/workspace information
redacted. Release archives retain the original external-partition/build guards.
Do not bypass a target mismatch after formatting.

The v3 repair additionally requires the original failed v2 backup at
`/Library/Application Support/YogaXeGPU/backups/20261023-221106` and its matching
failure markers. It does not automatically support a fresh install's backup.
The corrected bundle under `repaired/` is preserved separately; that directory
is not a complete GPU installer.
