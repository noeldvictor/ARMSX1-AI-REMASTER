# ARMSX Future Roadmap

This file is for deferred work only. It does not describe current runtime behavior.

## Deferred Features

- Save states
  - Build a versioned full-system serializer for `psx_t` and every emulated device.
  - Recreate host-side renderer and audio resources after load instead of serializing them.
- GLES renderer work
  - Keep the current software PSX GPU and add an optional GL/GLES presentation backend.
  - Re-evaluate a true hardware PS1 renderer separately if that ever becomes a goal.
- Netplay
  - Start with determinism validation and lockstep input sync before considering rollback.
  - Treat save-state quality as a prerequisite for any serious online recovery flow.
- Texture dumping and replacement
  - Add stable texture hashing, dump tooling, manifests, and replacement lookup in the software pipeline.
  - Revisit hardware-renderer-specific texture pack ideas only if a hardware GPU path exists later.
