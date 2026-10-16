# Using Phalk Profiles on Nintendo 3DS

1. Copy `PhalkProfiles-3DS.3dsx` to `/3ds/PhalkProfiles/` on your SD card.
2. Launch it from the Homebrew Launcher.
3. Choose **Set username** and **Set password** for your Phalk Profiles account.
4. Connect to Wi-Fi and choose **Upload play data**.

## Upload play data

On the first upload, the app creates `/3ds/PhalkProfiles/play_data_db.json` by copying the current Activity Log (`pld.dat`) and saving a PTM history checkpoint. PTM events from before this checkpoint are ignored to avoid importing sessions already represented in the PLD. For a complete initial copy, open the Activity Log once before the first upload so it writes its recent sessions to `pld.dat`.

On later uploads, the app adds new sessions from PTM to the local database, creates `export.json`, and uploads it. You do not need to open the Activity Log before each upload. If the saved PTM history cursor has expired because its event buffer wrapped, open Activity Log and reset the local database so the next upload can initialize it again from PLD.

The app can migrate an existing prototype database at `/3ds/PhalkProfiles/ptm_test_db.json` to the new local database path. It keeps the old file as a backup.

## Upload play data using Activity Log (PLD)

This option uses the previous PLD-based export flow and does not update the local PTM database. Open the Activity Log manually first, let it load, then return to Phalk Profiles and choose **Upload play data using Activity Log (PLD)**. The 3DS writes recent sessions to `pld.dat` when Activity Log opens.

## Other details

Game names are looked up locally by Title ID using `romfs:/3ds_db.json`. Unknown titles are exported with their Title ID in the name.

If future dates are found, the app displays the title and its first- and last-played dates. Use **left/right** to choose the year, month, or day, and **up/down** to change the selected value. Press **A** to save or **B** to cancel. Upload proceeds only after all future dates are corrected.

Credentials and date corrections are stored in `/3ds/PhalkProfiles/database.txt`. The generated upload file is `/3ds/PhalkProfiles/export.json`. **Reset local play data database** removes the PTM database; the next default upload seeds a new one from the current PLD. Open Activity Log before resetting if you want that initial copy to include recent sessions.

Use **up/down** to navigate, **A** to select, and **START** to exit.
