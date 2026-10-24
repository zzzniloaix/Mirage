# gear1_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 28525 frame pairs, FFmpeg 28704; only the 28440 pairs both scored are compared
- PAIRING MISMATCH: 85 of Mirage's 28525 pairs are not among FFmpeg's, and 264 of FFmpeg's 28704 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 2 of 28440 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 28525 | 28704 | **MISMATCH** |
| pooled mean (libvmaf) | 6.043396 | 5.988805 | 0.054591 |
| pooled min (libvmaf) | 0 | 0.000000 | 0.000000 |
| pooled harmonic mean (libvmaf) | 4.919687 | 4.668739 | 0.250948 |
| mean of the compared frames | 6.044193 | 6.044087 | 0.000106 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| libavutil version (run time) | 60.26.101 | 60.26.101 | |
| libavcodec version (run time) | 62.28.101 | 62.28.101 | |
| libavformat version (run time) | 62.12.101 | 62.12.101 | |
| libswscale version (run time) | 9.5.101 | 9.5.101 | |
| libavfilter version (run time) | 11.14.101 | 11.14.101 | |
| libavdevice version (run time) | - | 62.3.101 | |
| libswresample version (run time) | - | 6.3.101 | |
| exit status | 0 | 0 | |
| wall time, s | 756.0 | 726.1 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 10.000000, distorted 10.000000; the report's alignment, start_from container | reference 10.000000, distorted 10.000000; its log's input starts |
| scored pairs | 28525 | 28704 |
| pairs both scored | 28440 | 28440 |
| pairs only this side scored | 85 | 264 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 66 | 0 |
| reference frames in more than one pair | 0 | 1 |
| distorted frames in no pair | 329 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 264 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 1 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 1 | 1 |
| frames counted as failed | 395 (unmatched_reference 1, extra_distorted 46, after_discontinuity 348); failure rate 0.013658, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 28440: reference 28440 (948.948944 s) with distorted 28440 (948.948944 s)
- 28441: reference 28441 (948.982311 s) with distorted 28441 (948.982311 s)
- 28442: reference 28442 (949.015678 s) with distorted 28442 (949.015678 s)
- 28443: reference 28443 (949.049044 s) with distorted 28443 (949.049044 s)
- 28444: reference 28444 (949.082411 s) with distorted 28444 (949.082411 s)
- 28445: reference 28445 (949.115778 s) with distorted 28445 (949.115778 s)
- 28446: reference 28446 (949.149144 s) with distorted 28446 (949.149144 s)
- 28447: reference 28447 (949.182511 s) with distorted 28447 (949.182511 s)
- 28448: reference 28448 (949.215878 s) with distorted 28448 (949.215878 s)
- 28449: reference 28449 (949.249244 s) with distorted 28449 (949.249244 s)
- 28450: reference 28450 (949.282611 s) with distorted 28450 (949.282611 s)
- 28451: reference 28451 (949.315978 s) with distorted 28451 (949.315978 s)
- 28452: reference 28452 (949.349344 s) with distorted 28452 (949.349344 s)
- 28453: reference 28453 (949.382711 s) with distorted 28453 (949.382711 s)
- 28454: reference 28454 (949.416078 s) with distorted 28454 (949.416078 s)
- 28455: reference 28455 (949.449444 s) with distorted 28455 (949.449444 s)
- 28456: reference 28456 (949.482811 s) with distorted 28456 (949.482811 s)
- 28457: reference 28457 (949.516178 s) with distorted 28457 (949.516178 s)
- 28458: reference 28458 (949.549544 s) with distorted 28458 (949.549544 s)
- 28459: reference 28459 (949.582911 s) with distorted 28459 (949.582911 s)
- ... (65 more; all of them in delta.json)

Pairs only FFmpeg scored (its frame):
- 28440: reference 28439 (948.915578 s) with distorted 28440 (948.948944 s)
- 28441: reference 28439 (948.915578 s) with distorted 28441 (948.982311 s)
- 28442: reference 28439 (948.915578 s) with distorted 28442 (949.015678 s)
- 28443: reference 28439 (948.915578 s) with distorted 28443 (949.049044 s)
- 28444: reference 28439 (948.915578 s) with distorted 28444 (949.082411 s)
- 28445: reference 28439 (948.915578 s) with distorted 28445 (949.115778 s)
- 28446: reference 28439 (948.915578 s) with distorted 28446 (949.149144 s)
- 28447: reference 28439 (948.915578 s) with distorted 28447 (949.182511 s)
- 28448: reference 28439 (948.915578 s) with distorted 28448 (949.215878 s)
- 28449: reference 28439 (948.915578 s) with distorted 28449 (949.249244 s)
- 28450: reference 28439 (948.915578 s) with distorted 28450 (949.282611 s)
- 28451: reference 28439 (948.915578 s) with distorted 28451 (949.315978 s)
- 28452: reference 28439 (948.915578 s) with distorted 28452 (949.349344 s)
- 28453: reference 28439 (948.915578 s) with distorted 28453 (949.382711 s)
- 28454: reference 28439 (948.915578 s) with distorted 28454 (949.416078 s)
- 28455: reference 28439 (948.915578 s) with distorted 28455 (949.449444 s)
- 28456: reference 28439 (948.915578 s) with distorted 28456 (949.482811 s)
- 28457: reference 28439 (948.915578 s) with distorted 28457 (949.516178 s)
- 28458: reference 28439 (948.915578 s) with distorted 28458 (949.549544 s)
- 28459: reference 28439 (948.915578 s) with distorted 28459 (949.582911 s)
- ... (244 more; all of them in delta.json)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 28439 | 948.915578 | paired with distorted 28439 | paired with distorted 28439, 28440, 28441, 28442, 28443, 28444, 28445, 28446, 28447, 28448, 28449, 28450, 28451, 28452, 28453, 28454, 28455, 28456, 28457, 28458, 28459, 28460, 28461, 28462, 28463, 28464, 28465, 28466, 28467, 28468, 28469, 28470, 28471, 28472, 28473, 28474, 28475, 28476, 28477, 28478, 28479, 28480, 28481, 28482, 28483, 28484, 28485, 28486, 28487, 28488, 28489, 28490, 28491, 28492, 28493, 28494, 28495, 28496, 28497, 28498, 28499, 28500, 28501, 28502, 28503, 28504, 28505, 28506, 28507, 28508, 28509, 28510, 28511, 28512, 28513, 28514, 28515, 28516, 28517, 28518, 28519, 28520, 28521, 28522, 28523, 28524, 28525, 28526, 28527, 28528, 28529, 28530, 28531, 28532, 28533, 28534, 28535, 28536, 28537, 28538, 28539, 28540, 28541, 28542, 28543, 28544, 28545, 28546, 28547, 28548, 28549, 28550, 28551, 28552, 28553, 28554, 28555, 28556, 28557, 28558, 28559, 28560, 28561, 28562, 28563, 28564, 28565, 28566, 28567, 28568, 28569, 28570, 28571, 28572, 28573, 28574, 28575, 28576, 28577, 28578, 28579, 28580, 28581, 28582, 28583, 28584, 28585, 28586, 28587, 28588, 28589, 28590, 28591, 28592, 28593, 28594, 28595, 28596, 28597, 28598, 28599, 28600, 28601, 28602, 28603, 28604, 28605, 28606, 28607, 28608, 28609, 28610, 28611, 28612, 28613, 28614, 28615, 28616, 28617, 28618, 28619, 28620, 28621, 28622, 28623, 28624, 28625, 28626, 28627, 28628, 28629, 28630, 28631, 28632, 28633, 28634, 28635, 28636, 28637, 28638, 28639, 28640, 28641, 28642, 28643, 28644, 28645, 28646, 28647, 28648, 28649, 28650, 28651, 28652, 28653, 28654, 28655, 28656, 28657, 28658, 28659, 28660, 28661, 28662, 28663, 28664, 28665, 28666, 28667, 28668, 28669, 28670, 28671, 28672, 28673, 28674, 28675, 28676, 28677, 28678, 28679, 28680, 28681, 28682, 28683, 28684, 28685, 28686, 28687, 28688, 28689, 28690, 28691, 28692, 28693, 28694, 28695, 28696, 28697, 28698, 28699, 28700, 28701, 28702, 28703 (in 265 pairs) |
| reference | 28440 | - | paired with distorted 28440 | not among its frames |
| reference | 28441 | - | paired with distorted 28441 | not among its frames |
| reference | 28442 | - | paired with distorted 28442 | not among its frames |
| reference | 28443 | - | paired with distorted 28443 | not among its frames |
| reference | 28444 | - | paired with distorted 28444 | not among its frames |
| reference | 28445 | - | paired with distorted 28445 | not among its frames |
| reference | 28446 | - | paired with distorted 28446 | not among its frames |
| reference | 28447 | - | paired with distorted 28447 | not among its frames |
| reference | 28448 | - | paired with distorted 28448 | not among its frames |
| reference | 28449 | - | paired with distorted 28449 | not among its frames |
| reference | 28450 | - | paired with distorted 28450 | not among its frames |
| reference | 28451 | - | paired with distorted 28451 | not among its frames |
| reference | 28452 | - | paired with distorted 28452 | not among its frames |
| reference | 28453 | - | paired with distorted 28453 | not among its frames |
| reference | 28454 | - | paired with distorted 28454 | not among its frames |
| reference | 28455 | - | paired with distorted 28455 | not among its frames |
| reference | 28456 | - | paired with distorted 28456 | not among its frames |
| reference | 28457 | - | paired with distorted 28457 | not among its frames |
| reference | 28458 | - | paired with distorted 28458 | not among its frames |
- ... (546 more; all of them in delta.json)

Per frame, over the 28440 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 2.468787 (index 28439)
- mean \|delta\|: 0.000106
- mean delta: 0.000106
- frames with \|delta\| > 0.01: 2 (index 28438-28439)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 28439 | 28439 | 5.558348 | 3.089561 | 2.468787 |
| 28438 | 28438 | 5.536520 | 4.993359 | 0.543161 |
| 16635 | 16635 | 6.441876 | 6.441876 | 0.000000 |
| 1519 | 1519 | 5.891100 | 5.891100 | 0.000000 |
| 13482 | 13482 | 6.523038 | 6.523038 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 416x234 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: Parsed_scale_0: color_space smpte170m -> bt709

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 28525 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 416 |
| height | 1080 | 234 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | bt709 | smpte170m |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 961.751756 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: 1.37% of the frames failed to pair by timestamp (395 of 28920: 1 unmatched reference, 46 extra distorted, 348 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 28525 frame pairs that paired
- the two sides' pairing treats 566 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
