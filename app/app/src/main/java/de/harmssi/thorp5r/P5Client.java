package de.harmssi.thorp5r;

import android.os.Handler;
import android.os.Looper;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

/** Polls the status mod on 127.0.0.1 (the game runs on the same device) and hands snapshots to the UI thread. */
final class P5Client {
    interface Listener { void onState(P5State s); void onConnection(boolean connected); void onEnemies(List<String[]> enemies); }

    private final String base; private final Listener listener;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean running; private Thread thread;

    P5Client(String host, int port, Listener l) { base = "http://" + host + ":" + port; listener = l; }

    void start() {
        if (running) return;
        running = true;
        thread = new Thread(this::loop, "p5-status"); thread.setDaemon(true); thread.start();
    }

    void stop() { running = false; if (thread != null) thread.interrupt(); }

    private void loop() {
        boolean connected = false, haveEnemies = false;
        int polls = 0;
        while (running) {
            try {
                P5State s = P5State.parse(get("/state"));
                if (!connected) { connected = true; ui.post(() -> listener.onConnection(true)); }
                ui.post(() -> listener.onState(s));
                // the list never changes once the game has loaded its tables; until then it is empty, so ask again now and then
                if (!haveEnemies && s.ready && polls++ % 8 == 0) {
                    JSONArray a = new JSONObject(get("/enemies")).getJSONArray("enemies");
                    List<String[]> list = new ArrayList<>();
                    for (int i = 0; i < a.length(); i++) list.add(new String[] { a.getJSONObject(i).optString("name"), a.getJSONObject(i).optString("aff") });
                    haveEnemies = !list.isEmpty();
                    if (haveEnemies) ui.post(() -> listener.onEnemies(list));
                }
                Thread.sleep(700);
            } catch (InterruptedException e) {
                return;
            } catch (Exception e) {
                if (connected) { connected = false; haveEnemies = false; ui.post(() -> listener.onConnection(false)); }
                try { Thread.sleep(1500); } catch (InterruptedException x) { return; }
            }
        }
    }

    private String get(String path) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(base + path).openConnection();
        c.setConnectTimeout(1500); c.setReadTimeout(8000);
        try (InputStream in = c.getInputStream()) {
            ByteArrayOutputStream out = new ByteArrayOutputStream(); byte[] b = new byte[8192]; int n;
            while ((n = in.read(b)) > 0) out.write(b, 0, n);
            return out.toString("UTF-8");
        } finally { c.disconnect(); }
    }
}
