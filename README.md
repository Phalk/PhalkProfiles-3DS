# Using Phalk Profiles on Nintendo 3DS

1. Copy `PhalkProfiles-3DS.3dsx` to the `/3ds/PhalkProfiles/` folder on your SD card.
2. Launch the app from the Homebrew Launcher.
3. Select **Set username** and **Set password** to enter your Phalk Profiles account credentials.
4. Select **Create JSON** to save your activity history to the SD card, or **Create and upload** to save it and upload it to the website. Connect your 3DS to Wi-Fi to upload.

The generated file is saved to `/3ds/PhalkProfiles/export.json`. The app reads the Activity Log directly from the 3DS; Activity Log++ is not required.

Game names are looked up locally by Title ID in the database bundled at `romfs:/3ds_db.json`. The matched name is also written to the `name` field in `export.json`.

If future dates are found, the app displays the game name and its first- and last-played dates. Use **left/right** to select the year, month, or day for each date, and **up/down** to change the selected value. Press **A** to save or **B** to cancel. The JSON file is created only after all future dates have been corrected. Corrections are saved to `/3ds/PhalkProfiles/date_corrections.txt`; the NAND Activity Log is not modified. Corrections made in the previous screen must be saved again.

Use **up/down** to navigate, **A** to select, and **START** to exit.

**Warning:** uploads use unencrypted HTTP. Your password and activity history are transmitted over the network without protection.
