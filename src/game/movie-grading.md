# Movie color grades

`data/movie-grades.tsv` is the source of the reviewed scene grades. The build
validates it and embeds it in the playback library. Changing it requires a rebuild.
Rows use case-normalized `xv/12345.xmv` filenames. Audio and container differences
do not change the key. Only moving Cinepak video uses these grades.

| Column | Meaning | Neutral |
| --- | --- | --- |
| contrast | S-curve strength in percent, -25 to 25 | 0 |
| brightness | Additive normalized RGB offset in percent, -20 to 20 | 0 |
| gamma | Midtone exponent denominator in percent, 50 to 200 | 100 |
| black | Input black level, 0 to 127 | 0 |
| white | Input white level, 128 to 255 | 255 |
| red, green, blue | Per-channel gain in percent, 75 to 125 | 100 |

The white-minus-black interval must be at least 128. Processing order is input
levels, gamma, contrast, brightness and channel gain. Values clamp to the output
byte range. Alpha is preserved. Grades are fixed for a whole clip to avoid exposure
pumping. The neutral row produces the original pixels without modification.

Original mode bypasses grading. Reviewed scene grades uses this table. The two
manual contrast modes use fixed strengths of 15 and 25 with all other parameters
neutral. Menus, still images, JPEG and DVD MPEG video are excluded.

The F10 preview decodes independently and seeks silently. It compares the same
frame and grade function used by playback, without dispatching game callbacks.
Its side-by-side display uses fixed GDI scaling. The main scaling-filter selector
changes the live shared DirectDraw renderer instead.
