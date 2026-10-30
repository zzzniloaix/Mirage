# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-26T22:40:10Z to 2026-10-27T17:27:24Z; repository at bbc669dfeae9febd6f609f5f1e7b4f1489a57182, working tree clean at the start and end.

**NOT everything is as committed: see below.**

| step | ran | as committed | compared |
|---|---|---|---|
| streams | yes | yes | every playlist's SHA-256 and every probed stream's parameters |
| clips | yes | yes | clips.txt: each clip's command, tags and MD5 of its frames |
| format-clips | yes | yes | clips.txt: each clip's command, tags and MD5 of its frames |
| align-clips | yes | yes | clips.txt: each clip's command, tags and MD5 of its frames |
| negotiation-clips | yes | yes | clips.txt: each clip's command, tags and MD5 of its frames |
| diag-before-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| color-cases-before | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| format-cases-before | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| diag-after-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| color-cases-after | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| format-cases-after | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| format-cases-d022 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| negotiation-cases | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| scalers-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| models-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| defaults-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| ladder-300 | yes | yes | the check's verdict, the ladder report's master and plan, and every per-frame score of the ladder's pairs and of the pairs scored one by one |
| align-cases-before | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| align-cases-d028 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| acceptance-full | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| defaults-full | yes | NO | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| ffmpeg-scale-default | yes | yes | the output of each command of checks.txt after check 1 |
| ffmpeg-libvmaf-filter | yes | yes | the output of each command of checks.txt after check 1 |
| pooling | yes | yes | every series and skipped entry of pooling.json |
| pooling-defaults-300 | yes | yes | every series and skipped entry of pooling.json |
| pooling-full | yes | yes | every series and skipped entry of pooling.json |
| pooling-defaults-full | yes | NO | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (every run made after D-016 but format-cases-after and align-cases-before): built from commit bbc669dfeae9febd6f609f5f1e7b4f1489a57182, sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`.
- mirage_vmaf `before` (the runs made before D-016): built from commit 83c8ed6b497783cc6dd265637e7b2298399ba41a, sha256 `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`.
- mirage_vmaf `before-d022` (format-cases-after, made after D-016 and before D-022): built from commit d993686add8b2af709e1200437e9d643aed26238, sha256 `b225a60efd146d27fbb59335176324fa9529b1a86921f8814a1b64e34d162803`.
- mirage_vmaf `before-d028` (align-cases-before, made before D-028): built from commit e8b867fc371ccca2a099e941eeaf808a96b078dc, sha256 `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`.

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

## format-cases-before

mirage_vmaf sha256 `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`; the committed runs' (their build.txt): `4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | f01_10bit_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f02_10bit_smpte170m_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f03_10bit_nomatrix_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f04_10bit_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f05_422_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f06_422_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f07_444_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f08_444_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | j01_yuvj_pc_vs_tv | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | j02_yuvj_pc_vs_pc | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | d01_bt709_vs_10bit_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | d02_bt709tv_vs_yuvj_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| refused | u03_ycgco_vs_ycgco | FAIL / FAIL | 10 / 10 | yes | None / None | yes |

## diag-after-300

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

## color-cases-after

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd`, a different build; the table shows whether it scores alike.

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

## format-cases-after

mirage_vmaf sha256 `b225a60efd146d27fbb59335176324fa9529b1a86921f8814a1b64e34d162803`; the committed runs' (their build.txt): `b225a60efd146d27fbb59335176324fa9529b1a86921f8814a1b64e34d162803`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | f01_10bit_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f02_10bit_smpte170m_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f03_10bit_nomatrix_vs_bt709 | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f04_10bit_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f05_422_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f06_422_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f07_444_bt709_vs_smpte170m | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | f08_444_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | j01_yuvj_pc_vs_tv | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | j02_yuvj_pc_vs_pc | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | d01_bt709_vs_10bit_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | d02_bt709tv_vs_yuvj_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| refused | u03_ycgco_vs_ycgco | FAIL / FAIL | None / None | yes | None / None | yes |

## format-cases-d022

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `2220ce3a0997d53f750b34a770110f5c65f5f870cfb9a0e6347256733bcf09ea`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | f01_10bit_bt709_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f02_10bit_smpte170m_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f03_10bit_nomatrix_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f04_10bit_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f05_422_bt709_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f06_422_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f07_444_bt709_vs_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | f08_444_bt709_vs_bt709 | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | j01_yuvj_pc_vs_tv | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | j02_yuvj_pc_vs_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | d01_bt709_vs_10bit_smpte170m | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | d02_bt709tv_vs_yuvj_pc | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| refused | u03_ycgco_vs_ycgco | FAIL / FAIL | None / None | yes | None / None | yes |

## negotiation-cases

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | n01_yuvj_pc_vs_norange | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | n02_10bit_tv_vs_norange | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | n03_10bit_bt709_vs_gray | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | n04_10bit_bt709_vs_rgb | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |

## scalers-300

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, a different build; the table shows whether it scores alike.

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

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| vmaf_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| vmaf_v0.6.1neg | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| vmaf_4k_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| file_vmaf_v0.6.1 | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

model_effect: the same as committed

## defaults-300

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

## ladder-300

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `eab1651cc09237fce5451fd1de0c236c2d589b2d3b2d2848065b675e8e35a4c7`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| top-rung | ladder pair 0 (entries 0) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 1 (entries 1) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 2 (entries 2) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 3 (entries 3) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | gear4_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear3_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear2_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear1_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |

## align-cases-before

mirage_vmaf sha256 `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`; the committed runs' (their build.txt): `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | a00_aligned | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | a01_dropped_distorted | FAIL / FAIL | 9 / 9 | yes | 9 / 9 | yes |
| main | a02_dropped_reference | FAIL / FAIL | 9 / 9 | yes | 10 / 10 | yes |
| main | a03_duplicated_timestamp | FAIL / FAIL | 10 / 10 | yes | 11 / 11 | yes |
| main | a04_half_rate | FAIL / FAIL | 5 / 5 | yes | 6 / 6 | yes |
| main | a05_later_start | FAIL / FAIL | 7 / 7 | yes | 10 / 10 | yes |
| main | a06_later_start_audio | FAIL / FAIL | 7 / 7 | yes | 7 / 7 | yes |
| main | a07_restart | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |
| main | a08_ms_vs_90khz | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |

## align-cases-d028

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `7fb455e9e1349c85e25afe54d5fd47d714c463ecebea1b03b84ca1b594d27741`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| main | a00_aligned | PASS / PASS | 10 / 10 | yes | 10 / 10 | yes |
| main | a01_dropped_distorted | FAIL / FAIL | 9 / 9 | yes | 9 / 9 | yes |
| main | a02_dropped_reference | FAIL / FAIL | 9 / 9 | yes | 10 / 10 | yes |
| main | a03_duplicated_timestamp | FAIL / FAIL | 10 / 10 | yes | 11 / 11 | yes |
| main | a04_half_rate | FAIL / FAIL | 5 / 5 | yes | 6 / 6 | yes |
| main | a05_later_start | FAIL / FAIL | 7 / 7 | yes | 10 / 10 | yes |
| main | a06_later_start_audio | PASS / PASS | 7 / 7 | yes | 7 / 7 | yes |
| main | a07_restart | FAIL / FAIL | 5 / 5 | yes | 10 / 10 | yes |
| main | a08_ms_vs_90khz | FAIL / FAIL | 10 / 10 | yes | 10 / 10 | yes |

## acceptance-full

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |

## defaults-full

mirage_vmaf sha256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`; the committed runs' (their build.txt): `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear3_vs_gear5 | FAIL / PASS | 52442 / 53946 | NO (4 differ, largest 38.627685) | 53946 / 53946 | yes |
| plain | gear2_vs_gear5 | FAIL / PASS | 4425 / 53946 | NO (47 differ, largest 35.928724) | 4562 / 53946 | NO (14 differ) |
| plain | gear1_vs_gear5 | FAIL / PASS | 15639 / 53946 | NO (12747 differ, largest 7.860565) | 52184 / 53946 | NO (40180 differ) |

## Other differences

- pooling-defaults-full: {"what": "every series and skipped entry of pooling.json", "sources": {"rerun": 1, "committed": 1}, "differing_sources": [0], "identical": false}
