# Versioning policy

minbot uses a simple pre-1.0 release sequence.

- The first usable release is `0.1` and is tagged `v0.1`.
- Compatible fixes follow as `0.1.1`, `0.1.2`, up to `0.1.9`.
- The next feature milestone after `0.1.9` is `0.2` and is tagged `v0.2`.
- The same pattern repeats for later `0.x` milestones.

Every published version must update `include/minbot/version.hpp` and
`CHANGELOG.md`. A Git tag is created only after the matching commit passes all
tests.
