# Hardware Datasheets

**These files are deliberately NOT committed.** They are proprietary vendor
documentation from Seeed Studio and Good Display, and redistributing them in an
MIT-licensed public repository would be a licensing violation.

This folder is gitignored except for this README. Keep your local copies here — the
rest of the documentation refers to them by these filenames.

## What to download

| Expected local filename | What it is | Where to get it |
| --- | --- | --- |
| `GDEY029T94-2.9in-epaper-panel.pdf` | Good Display GDEY029T94 panel datasheet (296x128 mono, SSD1680) | [Good Display product page](https://www.good-display.com/product/210.html) |
| `XIAO_ePaper_Display_Board_EN05_V1.0.pdf` | Seeed XIAO ePaper Display Board (EN05) documentation | [Seeed EN05 wiki](https://wiki.seeedstudio.com/epaper_en05/) |
| `XIAO_ePaper_Display_Board_EN05_V1.11_SCH_PCB.zip` | EN05 schematic and PCB archive | Linked from the [Seeed EN05 wiki](https://wiki.seeedstudio.com/epaper_en05/) resources section |

Related official references:

- Seeed XIAO nRF52840 wiki: https://wiki.seeedstudio.com/XIAO_BLE/
- 2.9" panel product page:
  https://www.seeedstudio.com/2-9-Monochrome-ePaper-Display-with-296x128-Pixels-p-5782.html
- SSD1680 controller datasheet: published by Solomon Systech

## Why this matters

Anyone rebuilding this clock needs these documents, but they must fetch them from the
vendor themselves. Everything this project actually *learned* from them — pin
mappings, the D6 power gate, controller identification, refresh behaviour — is
written up in plain terms in `docs/hardware/PINOUT.md` and
`docs/lessons/LESSONS_LEARNT.md`, so the repo remains useful standalone.
