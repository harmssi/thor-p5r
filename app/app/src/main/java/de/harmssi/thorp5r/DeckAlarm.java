package de.harmssi.thorp5r;

import android.app.ActivityOptions;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.SystemClock;
import android.util.Log;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * Brings the deck back. While Persona 5 Royal loads, Android runs so short of memory that it may end the deck's
 * process. An alarm lives in the system, not in our process: every 20 s it checks whether the game's status mod
 * answers and the deck is gone, and reopens the deck on the bottom screen. It stops by itself two minutes after the
 * game has ended. Opening a screen from the background needs the "display over other apps" permission.
 */
public class DeckAlarm extends BroadcastReceiver {
    static volatile boolean deckOpen;          // set by the deck while its screen exists (same process)
    private static final int PERIOD_MS = 20000, GIVE_UP = 6;   // 6 checks without the game = 2 minutes
    private static final String MISSES = "misses";

    static void arm(Context c, int misses) {
        Intent i = new Intent(c, DeckAlarm.class).putExtra(MISSES, misses);
        PendingIntent pi = PendingIntent.getBroadcast(c, 0, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        c.getSystemService(AlarmManager.class).set(AlarmManager.ELAPSED_REALTIME, SystemClock.elapsedRealtime() + PERIOD_MS, pi);
    }

    @Override public void onReceive(Context c, Intent intent) {
        int misses = intent.getIntExtra(MISSES, 0);
        PendingResult result = goAsync();
        new Thread(() -> {
            boolean game = false;
            try (Socket s = new Socket()) { s.connect(new InetSocketAddress(CompanionActivity.HOST, CompanionActivity.PORT), 400); game = true; }
            catch (Exception e) { /* the game is not running (yet) */ }
            try {
                if (game && !deckOpen) {
                    Intent i = new Intent(c, CompanionActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    c.startActivity(i, ActivityOptions.makeBasic().setLaunchDisplayId(StarterActivity.bottomDisplay(c)).toBundle());
                }
            } catch (Exception e) {
                Log.w("ThorP5R", "reopen: " + e);
            }
            int next = game ? 0 : misses + 1;
            if (next < GIVE_UP) arm(c, next);
            result.finish();
        }).start();
    }
}
