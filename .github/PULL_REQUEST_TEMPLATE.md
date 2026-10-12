<!-- 中文或英文都可以。先读 CONTRIBUTING.zh-CN.md。 / Chinese or English are both fine. Read CONTRIBUTING.md first. -->

## What changed

<!-- What this change does, in a few sentences. -->

## Why

<!-- The problem it solves. Link the issue if there is one. -->

## Checks run

<!-- Tick what you actually ran on this change and paste the last line of output where there is one. Leave unticked what you did not run. -->

No game files needed:

- [ ] `python3 dm/i18n/scan_literals.py`
- [ ] `python3 dm/ios/tools/dm_i18n.py --check`

Game files and a workspace prepared by `./setup.sh` needed:

- [ ] `make -C dm/host/tests run`
- [ ] `make -C dm/gfx/tests run`
- [ ] `make -C dm/ios/tests test`
- [ ] `dm/verify/regress.sh` (with `--fresh` if `dm/tools/patch_nso.py` changed)
- [ ] `cd dm/ios && ./run.sh build`
- [ ] `dm/ios/tools/smoke_mac.sh 1`
- [ ] `dm/ios/tools/check_hud_gen.sh`

Anything else you ran, and on what (Mac host, iOS package on a Mac, which device):

## Visible changes

- [ ] Nothing on screen changes
- [ ] Something on screen changes — screenshots taken after the change are attached below
- [ ] New on-screen text is in `dm/i18n/ui_text.tsv`, and `dm/host/dm_i18n_tab.h` was regenerated

## No game content

- [ ] This change contains no game files, nothing generated from game files, no keys, and no built package
- [ ] It does not carry the local rewrites `./setup.sh` makes to `dm/gfx/tools/dmsh_chain.lock`, `dm/gfx/nvn/dmsh_chain.h` and `dm/gfx/nvn/c1_full.tsv`

## Comments

- [ ] New and changed comments are in both Chinese and English
