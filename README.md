# Using Phalk Profiles on Nintendo 3DS

1. Copy `PhalkProfiles-3DS.3dsx` to the `/3ds/PhalkProfiles/` folder on your SD card.
2. Launch the app from the Homebrew Launcher.
3. Select **Set username** and **Set password** to enter your Phalk Profiles account credentials.
4. Select **Create JSON** to save your activity history to the SD card, or **Create and upload** to save it and upload it to the website. Connect your 3DS to Wi-Fi to upload.

The generated file is saved to `/3ds/PhalkProfiles/export.json`.

The app shows when the built-in Activity Log was last opened. Choose **Update Activity Log (Last update: …)** to open it before exporting; if your Homebrew setup cannot launch it directly, open Activity Log manually and relaunch PhalkProfiles-3DS. The 3DS may keep recent sessions out of its saved `pld.dat` until Activity Log is opened.

Game names are looked up locally by Title ID in the database bundled at `romfs:/3ds_db.json`. The matched name is also written to the `name` field in `export.json`.

If future dates are found, the app displays the game name and its first- and last-played dates. Use **left/right** to select the year, month, or day for each date, and **up/down** to change the selected value. Press **A** to save or **B** to cancel. The JSON file is created only after all future dates have been corrected. Corrections are saved to `/3ds/PhalkProfiles/date_corrections.txt`; the NAND Activity Log is not modified. Corrections made in the previous screen must be saved again.

Use **up/down** to navigate, **A** to select, and **START** to exit.
