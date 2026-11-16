# Releasing

How to ship:

1. Wait for master's full run (a push to master runs every platform and every test, and publishes nothing) to go green.
2. Run `tools/ship.sh` (`--dry-run` first to see the number): it checks that run and pushes the tag `build-<N>`, N one above the latest release.
3. The tag's run builds, signs, notarizes and publishes the release "Build N", marked latest, which the self-updaters and Sparkle's appcast find.

A run started by hand (Actions, Build, Run workflow) with a version does the same; with `dry_run`, or from any branch but master, it publishes nothing and keeps the would-be release as an artifact. A failing master run opens an issue labelled `ci-broken`, and the next green run closes it.
