# Bundled content

Every asset that ships inside Purrlude is listed here with its source, author and license.
Only content whose license permits redistribution inside the app may be added.

| Asset | Used for | Source | Author | License |
|---|---|---|---|---|
| GeneralUser GS v2.0.3 (`GeneralUser-GS.sf2`) | Built-in General MIDI instruments | [github.com/mrbumpy409/GeneralUser-GS](https://github.com/mrbumpy409/GeneralUser-GS) at `684543d`; [schristiancollins.com](https://www.schristiancollins.com) | S. Christian Collins | GeneralUser GS License v2.0 (copied next to the app as `LICENSE-GeneralUser-GS.txt`) |
| GO:KEYS 3 / GO:KEYS 5 tone names and numbers (`devices/roland-go-keys-3-5.json`) | Choosing the keyboard's tones by name | Roland's published "GO:KEYS 3 / GO:KEYS 5 Tone List" (2024) | Roland Corporation | Factual reference data (tone names and MIDI addresses), used for interoperability; the PDF itself is not included |
| Verovio fonts and glyph data (`data/`) | Score engraving | [github.com/rism-digital/verovio](https://github.com/rism-digital/verovio) 6.3.0 | RISM Digital Center and contributors; SMuFL fonts by their respective authors | LGPLv3 (Verovio); fonts under SIL OFL 1.1 |

## Notes

- **Device definitions** (`content/devices/*.json`) are compiled into the app. Each lists a device's tone names with their Bank Select (MSB/LSB) and Program Change numbers.
  - **GO:KEYS:** bank and program are derived from Roland's tone numbers, which also corrects three misprinted entries (PR018–PR020).

- **GeneralUser GS:**
  - The license allows use for music creation, private or commercial, and allows use in software projects, including modifying it.
  - The author notes he can't be 100% certain of the origin of every sample, though none came from commercial sample libraries. Review this before any closed-source commercial release.
  - The license asks not to link directly to the author's download files. Purrlude fetches the file from the GitHub repository at a pinned commit, verified by SHA-256.
