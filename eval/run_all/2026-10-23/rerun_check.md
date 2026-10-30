# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-23T13:24:22Z to 2026-10-23T13:33:26Z; repository at e2169f5aaf2f27446ca624705b2b259e6644e6e2, working tree clean at the start and end.

**Everything compared is as committed.**

| step | ran | as committed | compared |
|---|---|---|---|
| streams | yes | yes | every playlist's SHA-256 and every probed stream's parameters |
| clips | yes | yes | clips.txt: each clip's command, tags and MD5 of its frames |
| diag-before-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| color-cases-before | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| diag-after-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| color-cases-after | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| scalers-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| models-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| ffmpeg-scale-default | yes | yes | the output of each command of checks.txt after check 1 |
| ffmpeg-libvmaf-filter | yes | yes | the output of each command of checks.txt after check 1 |
| pooling | yes | yes | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (the runs made after D-016): built from commit e2169f5aaf2f27446ca624705b2b259e6644e6e2, sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`.
- mirage_vmaf `before` (the runs made before D-016): built from commit 83c8ed6b497783cc6dd265637e7b2298399ba41a, sha256 `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`.

## diag-before-300

mirage_vmaf sha256 `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`; the committed runs' (their build.txt): `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear2_vs_gear5 | FAIL / FAIL | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear1_vs_gear5 | FAIL / FAIL | 300 / 300 | yes | 300 / 300 | yes |
| relabel | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| relabel | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| relabel | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| relabel | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

## color-cases-before

mirage_vmaf sha256 `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`; the committed runs' (their build.txt): `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | m01_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | m02_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m03_bt709_vs_untagged | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | m04_untagged_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | m05_untagged_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m06_untagged_vs_untagged | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m07_smpte170m_vs_bt470bg | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m08_bt2020nc_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | r01_tv_vs_pc | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | r02_pc_vs_tv | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | r03_norange_vs_pc | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | r04_pc_vs_norange | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | r05_norange_vs_tv | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r06_tv_vs_norange | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x01_bt709tv_vs_smpte170mpc | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | x02_primaries_transfer | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x03_chroma_topleft | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x04_interlaced_tff | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| refused | u01_bt709_vs_ycgco | FAIL / FAIL | 10 / 10 | yes | None / None | yes |
| refused | u02_ycgco_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | None / None | yes |

## diag-after-300

mirage_vmaf sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`; the committed runs' (their build.txt): `420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

## color-cases-after

mirage_vmaf sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`; the committed runs' (their build.txt): `420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | m01_bt709_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m02_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m03_bt709_vs_untagged | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m04_untagged_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m05_untagged_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m06_untagged_vs_untagged | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m07_smpte170m_vs_bt470bg | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | m08_bt2020nc_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r01_tv_vs_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r02_pc_vs_tv | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r03_norange_vs_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r04_pc_vs_norange | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r05_norange_vs_tv | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | r06_tv_vs_norange | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x01_bt709tv_vs_smpte170mpc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x02_primaries_transfer | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x03_chroma_topleft | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | x04_interlaced_tff | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| refused | u01_bt709_vs_ycgco | FAIL / FAIL | None / None | yes | None / None | yes |
| refused | u02_ycgco_vs_bt709 | FAIL / FAIL | None / None | yes | None / None | yes |

## scalers-300

mirage_vmaf sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| bilinear | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bilinear | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bilinear | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bilinear | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bicubic | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bicubic | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bicubic | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| bicubic | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| lanczos | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| lanczos | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| lanczos | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| lanczos | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

scaler_effect: the same as committed

## models-300

mirage_vmaf sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| vmaf_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| vmaf_v0.6.1neg | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| vmaf_4k_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| file_vmaf_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

model_effect: the same as committed
