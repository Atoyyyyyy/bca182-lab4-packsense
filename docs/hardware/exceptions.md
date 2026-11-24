# Hardware exceptions

List here every component from [`inventory.md`](inventory.md) that is **absent,
broken, or deliberately not integrated**, with the reason. The lab requires every
physically present, working module to be integrated, so each exception needs the
instructor's verification.

| # | Component | Status (absent / broken / not integrated) | Reason and how it was checked | Evidence file | Logged by / date | Instructor verification (signature, date) |
|---|---|---|---|---|---|---|
| 1 | microSD **card** (the slot, SDIO and card-detect are fitted) | absent | No card in the kit (operator, 2026-11-24). `sd` reports `init NOT_PRESENT` (SDIO command-response timeout) and the system keeps running. The FAT logging (PRD `sdlog.c`) needs a card: FAT32 card to be added | `evidence/m2_storage.log` | R. Beroy, 2026-11-24 | |

### Notes

- *Connector-only* items (CAN, RS485, PMOD, USB OTG) with nothing attached are
  not exceptions. They are listed in `inventory.md` row 16 as "connector only".
- A module that fails intermittently stays in the product with fault handling
  (PRD §6.4). Record it here as well, with the observed failure rate.
