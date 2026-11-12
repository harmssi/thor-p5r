package de.harmssi.thorp5r;

import android.app.Activity;
import android.os.Bundle;

/**
 * Unsichtbare Hilfsaktivität für den oberen Bildschirm: Auf dem AYN Thor wandert der Eingabefokus
 * (und damit der Controller) mit jeder Berührung auf den unteren Bildschirm. Der Begleiter startet
 * nach einer Berührung kurz diese Aktivität auf Display 0, damit der Fokus zum Spiel zurückkehrt.
 */
public class FocusTopActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        finish();
        overridePendingTransition(0, 0);
    }
}
