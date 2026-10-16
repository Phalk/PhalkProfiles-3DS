# Using Phalk Profiles on Nintendo 3DS

1. Copy `PhalkProfiles-3DS.3dsx` to `/3ds/PhalkProfiles/` on your SD card.
2. Launch it from the Homebrew Launcher.
3. Choose **Setup 3DS timezone** and set the same UTC offset as the 3DS system clock.
4. Choose **Set username** and **Set password** for your Phalk Profiles account.
5. Connect to Wi-Fi and choose **Upload play data**.

The timezone setup is required before either upload option can export data. It is saved in `/3ds/PhalkProfiles/config.txt` and applied to exported Unix timestamps so the website receives the intended date and time.

## Upload play data

On the first upload, the app creates `/3ds/PhalkProfiles/play_data_db.json` by copying the current Activity Log (`pld.dat`) and saving a PTM history checkpoint. PTM events from before this checkpoint are ignored to avoid importing sessions already represented in the PLD. For a complete initial copy, open the Activity Log once before the first upload so it writes its recent sessions to `pld.dat`.

On later uploads, the app adds new sessions from PTM to the local database, creates `export.json`, and uploads it. You do not need to open the Activity Log before each upload. If the saved PTM history cursor has expired because its event buffer wrapped, open Activity Log and reset the local database so the next upload can initialize it again from PLD.

After the initial PLD copy, Phalk Profiles can send the latest session time from PTM without reopening Activity Log for every upload. The Activity Log keeps the play record; Phalk Profiles gets that timestamp to the website, converted to UTC using the configured 3DS offset. 😎 (PTM timestamps have minute precision.)

The app can migrate an existing prototype database at `/3ds/PhalkProfiles/ptm_test_db.json` to the new local database path. It keeps the old file as a backup.

## Upload using Activity Log Save (PLD)

This option uses the previous PLD-based export flow and does not update the local PTM database. Open the Activity Log manually first, let it load, then return to Phalk Profiles and choose **Upload using Activity Log Save (PLD)**. The 3DS writes recent sessions to `pld.dat` when Activity Log opens.

## Other details

Game names are looked up locally by Title ID using `romfs:/3ds_db.json`. Unknown titles are exported with their Title ID in the name.

If future dates are found, the app displays the title and its first- and last-played dates. Use **left/right** to choose the year, month, or day, and **up/down** to change the selected value. Press **A** to save or **B** to cancel. Upload proceeds only after all future dates are corrected.

Credentials, timezone offset, and date corrections are stored in `/3ds/PhalkProfiles/config.txt`. Existing settings in the former `database.txt` file are migrated automatically. The generated upload file is `/3ds/PhalkProfiles/export.json`. **Reset local play data database** removes the PTM database; the next default upload seeds a new one from the current PLD. Open Activity Log before resetting if you want that initial copy to include recent sessions.

Use **up/down** to navigate, **A** to select, and **START** to exit.

## Technical notes for developers

### Data flow

The default **Upload play data** action uses a local database as the source for every export:

1. At startup, `source/main.cpp` loads `/3ds/PhalkProfiles/play_data_db.json`. If it is missing, the database stays uninitialized; date corrections and other settings do not determine this state.
2. On the first upload, the app reads PLD summaries and session records through `readActivityLog()` in `source/activity_log.cpp`. It then snapshots the PTM event history and saves the PTM cursor. This copies the PLD totals and intentionally skips PTM events already present before that checkpoint.
3. On later uploads, `syncPlayDataFromPtm()` in `source/play_data.cpp` reads PTM and processes events after the saved cursor. It does not read PLD again.
4. The app converts the in-memory database to the upload schema, applies saved date corrections to a temporary export copy, writes `export.json`, and uploads it. The corrections do not modify `play_data_db.json`.

PTM changes are saved to the local database before JSON generation and upload. If a later export or upload step fails, those PTM events remain recorded locally and are not added again on the next run.

Initialization reads PLD before it snapshots PTM. A session recorded in PTM between those two reads falls before the saved checkpoint and may not yet be included in PLD. Avoid launching or switching titles during first-time initialization; this small timing window is a known limitation of the current ordering.

The upload JSON keeps the existing service schema: top-level export metadata and `users`, then each user's `titles`. Each title exports its hexadecimal `id`, display `name`, `platform`, and `summary` containing Unix timestamps for `firstPlayed` and `lastPlayed`, `playtime` in seconds, and `launches`. The user must configure the 3DS UTC offset before exporting. The app interprets Activity Log and PTM date/time values as the 3DS's configured local time and subtracts the saved offset to produce UTC timestamps; this also applies to `exportTimestamp`. In the default database flow, `lastPlayed` includes the stored session time when its day matches the exported last-played day; otherwise it falls back to midnight on that date. PTM timestamps have minute precision, so their seconds are `00`. The legacy PLD export continues to use date-only `lastPlayed`. The PTM cursor is not sent.

### PTM event processing

`readSnapshot()` opens the PTM Plays service and reads the circular play-history buffer in chunks of 256 events. The saved cursor consists of the next ring index, the last consumed index, and a signature derived from the last consumed event's title ID, minute timestamp, and event type. Before syncing, `cursorIsRetained()` checks that the prior event is still present and matches its signature. If the cursor has fallen out of the buffer or the ring position no longer matches, sync stops with a recovery message instead of silently skipping unknown history.

`makeSessions()` pairs application launch/jump events with exit/leave events by Title ID. A global shutdown event (PTM event type 10 with an invalid Title ID) closes any sessions still active at shutdown. Sessions without a matching end are counted as incomplete and are not added to play time. PTM timestamps have minute precision, so reconstructed durations and dates use that precision. The current implementation does not persist an open session across syncs; a session whose start and end land on opposite sides of a saved cursor cannot be reconstructed from the new-event slice alone.

When PTM history has wrapped past the cursor, the app asks the user to open Activity Log and reset the local database. Reset removes the PTM database; the next default upload seeds it again from the current PLD and establishes a fresh cursor. This recovery can only preserve history available in that PLD snapshot.

### Local database format

`PlayDataTitle` stores Title ID, cumulative play time in seconds, launch count, first- and last-played day numbers, and the latest session timestamp. The database also stores its format version, initialization flag, and PTM cursor. It is written as JSON to `/3ds/PhalkProfiles/play_data_db.json` through a temporary file.

The loader accepts format 1 and format 2 databases and can migrate the older prototype file `ptm_test_db.json`. Migration writes the current format to `play_data_db.json` and leaves the old prototype file as a backup. A reset removes both files.

### PLD path and code map

The separate **Upload using Activity Log Save (PLD)** action reads and exports PLD directly. It retains the older flow, including PLD-derived recent-session date handling, and exports `lastPlayed` at date-only precision. It does not initialize or update the PTM database. `source/activity_log.cpp` reads the system save read-only; the app does not write to `pld.dat` or launch Activity Log.

- `source/main.cpp`: menu, orchestration, date-correction and timezone setup screens, JSON timestamp conversion/schema, and upload.
- `source/play_data.cpp` / `include/play_data.hpp`: PTM snapshot, session reconstruction, local database, migration, and cursor validation.
- `source/activity_log.cpp` / `include/activity_log.hpp`: read-only PLD parser and local Title ID-to-name lookup.
- `config.txt`: credentials, timezone setting, and persisted date corrections; independent of the play-data database.
