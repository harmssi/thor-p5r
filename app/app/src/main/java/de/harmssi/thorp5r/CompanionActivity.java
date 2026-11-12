package de.harmssi.thorp5r;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The deck on the bottom screen: live data from the status mod inside Persona 5 Royal (127.0.0.1).
 * It never keeps the controller: after a touch the focus goes back to the game screen.
 */
public class CompanionActivity extends Activity implements P5Client.Listener {
    static final String HOST = "127.0.0.1";
    static final int PORT = 8767;

    private P5View view;
    private P5Client client;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Focusable on purpose: when this screen holds the focus and the window cannot take keys, Android reports
        // "not responding" at the first controller key. The focus is handed back to the game screen instead.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new P5View(this);
        view.setOnTouchFinished(() -> { lowered = false; returnFocusToGame(); });   // a touch means the deck is visible again
        view.setOnHeaderHold(this::lower);
        setContentView(view);
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
        client = new P5Client(HOST, PORT, this);
        DeckAlarm.deckOpen = true;
    }

    @Override protected void onStart() {
        super.onStart();
        client.start();
    }

    private long focusBackAt;

    private void focusBack() {
        long now = android.os.SystemClock.uptimeMillis();
        if (now - focusBackAt < 700) return;
        focusBackAt = now;
        returnFocusToGame();
    }

    @Override public void onWindowFocusChanged(boolean has) {
        super.onWindowFocusChanged(has);
        if (has) view.postDelayed(this::focusBack, 400);
    }

    @Override public boolean dispatchKeyEvent(android.view.KeyEvent e) {
        focusBack();
        return true;
    }

    @Override public boolean dispatchGenericMotionEvent(android.view.MotionEvent e) {
        if ((e.getSource() & android.view.InputDevice.SOURCE_CLASS_JOYSTICK) != 0) focusBack();
        return true;
    }

    @Override protected void onStop() {
        client.stop();
        super.onStop();
    }

    private int lastMode = -99;
    private boolean lowered;

    @Override public void onState(P5State s) {
        view.setState(s);
        // DroidDeck re-creates its panel when the game window appears, which puts it in front again: come back to the
        // front whenever the game changes its scene, unless the player asked for DroidDeck's panel (header held).
        if (s.mode != lastMode) { lastMode = s.mode; if (!lowered) raise(); }
    }

    @Override public void onEnemies(java.util.List<String[]> enemies) { view.setShadows(enemies); }

    @Override public void onConnection(boolean connected) {
        view.setConnected(connected);
        if (connected) { lowered = false; raise(); }
    }

    private android.app.Presentation front;

    /**
     * DroidDeck draws its control panel as a Presentation, which lies above every activity on this screen.
     * A Presentation of our own that is opened later (the game is running by then) lies above that panel.
     */
    private void raise() {
        try {
            if (front != null) front.dismiss();
            if (view.getParent() != null) ((android.view.ViewGroup) view.getParent()).removeView(view);
            front = new android.app.Presentation(this, getDisplay());
            // not focusable: keys and the controller stay with the game
            front.getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            front.setContentView(view);
            front.show();
        } catch (Exception e) {
            android.util.Log.w("ThorP5R", "raise: " + e);
            if (view.getParent() == null) setContentView(view);
        }
    }

    /** Back under the launcher's panel (hold the header); closing that panel shows the deck again. */
    private void lower() {
        lowered = true;
        if (front == null) return;
        front.dismiss();
        front = null;
        if (view.getParent() != null) ((android.view.ViewGroup) view.getParent()).removeView(view);
        setContentView(view);
    }

    @Override protected void onDestroy() {
        DeckAlarm.deckOpen = false;
        if (front != null) front.dismiss();
        super.onDestroy();
    }

    /** Nach einer Berührung den Eingabefokus (Controller) wieder auf den oberen Bildschirm legen. */
    private void returnFocusToGame() {
        try {
            android.content.Intent i = new android.content.Intent(this, FocusTopActivity.class);
            i.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK | android.content.Intent.FLAG_ACTIVITY_NO_ANIMATION);
            startActivity(i, android.app.ActivityOptions.makeBasic().setLaunchDisplayId(0).toBundle());
        } catch (Exception e) {
            android.util.Log.w("ThorP5R", "focus: " + e);
        }
    }
}
