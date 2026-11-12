package de.harmssi.thorp5r;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;

/**
 * Startet Final Fantasy IX (Steam-AppID 377840) in DroiDeck auf dem oberen Bildschirm (Display 0)
 * und öffnet den Begleiter auf dem unteren Bildschirm (beim AYN Thor Display 4).
 */
public class StarterActivity extends Activity {
    static final String STEAM_APP_ID = "1687950";
    private static final int TOP_DISPLAY = 0;
    private static final int BOTTOM_DISPLAY = 4;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Erst der Begleiter unten, dann das Spiel oben: der zuletzt gestartete Bildschirm erhält den Controller-Fokus.
        Intent companion = new Intent(this, CompanionActivity.class);
        companion.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        int bottom = bottomDisplay(this);
        DeckAlarm.arm(this, -9);   // the game needs a few minutes to start: allow some extra misses first
        // without this permission Android does not let the alarm reopen the deck after it was ended for memory
        if (!android.provider.Settings.canDrawOverlays(this)) Toast.makeText(this, R.string.overlay_hint, Toast.LENGTH_LONG).show();
        startActivity(companion, ActivityOptions.makeBasic().setLaunchDisplayId(bottom).toBundle());
        Intent dd = new Intent("com.droiddeck.launcher.LAUNCH_GAME");
        dd.setClassName("com.droiddeck.launcher", "com.droiddeck.launcher.FrontEndActivity");
        dd.putExtra("app_id", STEAM_APP_ID);
        dd.putExtra("game_source", "STEAM");
        dd.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            startActivity(dd, ActivityOptions.makeBasic().setLaunchDisplayId(TOP_DISPLAY).toBundle());
        } catch (Exception e) {
            Log.w("ThorP5R", "DroiDeck: " + e);
            Toast.makeText(this, R.string.droiddeck_missing, Toast.LENGTH_LONG).show();
        }

        finish();
    }

    /** Display 4 auf dem Thor; auf anderen Geräten das erste Nicht-Standard-Display, sonst 0. */
    static int bottomDisplay(android.content.Context c) {
        DisplayManager dm = c.getSystemService(DisplayManager.class);
        int fallback = TOP_DISPLAY;
        for (Display d : dm.getDisplays()) {
            if (d.getDisplayId() == BOTTOM_DISPLAY) return BOTTOM_DISPLAY;
            if (d.getDisplayId() != TOP_DISPLAY && fallback == TOP_DISPLAY) fallback = d.getDisplayId();
        }
        return fallback;
    }
}
