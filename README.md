# Using Phalk Profiles on Nintendo 3DS

1. Copy `PhalkProfiles-3DS.3dsx` to the `/3ds/PhalkProfiles/` folder on your SD card.
2. Launch the app from the Homebrew Launcher.
3. Select **Set username** and **Set password** to enter your Phalk Profiles account credentials.
4. Select **Create and upload** to save your activity history to the SD card and upload it to the website. Connect your 3DS to Wi-Fi first.

The generated file is saved to `/3ds/PhalkProfiles/export.json`.

Before uploading, choose the first menu option, **Update Activity Log**. The app records the request time and tries to open Activity Log; the 3DS saves recent sessions there when it opens. Return to PhalkProfiles-3DS and choose **Create and upload**. If Activity Log does not open directly, open it manually and relaunch the app.

Game names are looked up locally by Title ID in the database bundled at `romfs:/3ds_db.json`. The matched name is also written to the `name` field in `export.json`.

If future dates are found, the app displays the game name and its first- and last-played dates. Use **left/right** to select the year, month, or day for each date, and **up/down** to change the selected value. Press **A** to save or **B** to cancel. Upload proceeds only after all future dates have been corrected. Credentials, date corrections, and the last Activity Log update request are stored in `/3ds/PhalkProfiles/database.txt`; the NAND Activity Log is not modified. Corrections made in the previous screen must be saved again.

Use **up/down** to navigate, **A** to select, and **START** to exit.
