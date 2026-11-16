---
name: sync-release
description: Merge upstream, adapt fork features, and publish verified Pico/S31 GitHub releases. Use for upstream sync and release requests, not questions about past changes.
---

<!-- SPDX-License-Identifier: Apache-2.0 -->

# Sync and release

Invoke with `$sync-release`. Follow the user's requested scope; sync-only requests do not include publication.

Defaults: `origin` = `ytyt-yt/read-pico-reader`; `upstream` = `wegooo-cell/read-pico-reader`. Verify remotes before pushing. Publish to the fork only; no website/Pages/online OTA deployment or device flashing. Preserve historical releases and follow repository hardware restrictions.

1. Read `AGENTS.md`, `docs/ONBOARDING.md`, `docs/HANDOFF.md`, and the latest `docs/HANDOFF.local.md` entry if present. Check existing releases, preserve local changes, fetch both remotes, and work on a task branch based on current `origin/main`. If nothing remains unreleased, report that without creating a duplicate release unless a rebuild was requested.

2. Merge using `git merge --no-ff upstream/main`. Preserve both histories and adapt affected fork features, including comics, random wallpapers, and Pico Clear ripple. Check shared cover queues, settings, and quick switches. Edit maintained sources, not generated board views; inspect actual waveform changes before describing them.

3. Run relevant tests from `.github/workflows/build.yml` for both boards, using `tools/board_source_view.py --board <board> --tests` as needed. Fix failures without weakening checks. Commit, check `git push --dry-run`, push only to origin, and integrate into main without rewriting history. Require successful CI for the final main commit before publication.

4. Read `docs/BOARDS.md`. Build clean final main through `tools/build_firmware.py --board all`, with matching SDKs and isolated board outputs. Keep upstream versions from `boards/<board>/board.json`; inject a unique `-fork.YYYYMMDD` suffix into the actual firmware version, respecting its length limit. Pin the commit and version injection in a reproducible recipe. Store outputs under ignored `build/fork-release-<stamp>/`; preserve published `flash/` files.

5. Verify board/chip/project/version, image checksums, both OTA slot limits, source-view consistency, and partition compatibility. Test the original TF updater against the full candidate image, including version/board/size rejection and failure protection. Keep hardware and partition changes outside this workflow; distinguish host checks from device validation.

6. Package 12 assets per board from the same build:
   - `Pico-update.bin` or `Metalio-S31-update.bin`, versioned Bin, TF ZIP, complete USB ZIP;
   - `bootloader.bin`, `partitions.bin`, `ota_data_initial.bin`, `manifest.json`;
   - `SHA256SUMS.txt`, `RELEASE-NOTES.md`, `BUILD-INFO.json`, `BUILD-RECIPE.py`.

   Match application bytes across Bin/ZIPs; hash every asset except the checksum file itself. Describe upstream changes, fork adaptations, feature entry points, TF upgrade steps, and validation limits. Tag final main with annotated `v<version>` for Pico and `metalio-s31-v<version>` for S31. Upload drafts, verify assets, then publish: Pico Latest, S31 non-Latest. Never overwrite conflicting tags or release assets.

7. Download all published assets and compare bytes, hashes, versions, tag targets, and Latest status. Update ignored `docs/HANDOFF.local.md`. Report both release links, source commit, merge status, concrete changes, validation, and any unfinished work.
