package de.harmssi.thorp5r;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The deck (bottom screen, drawn on a 1240x1080 reference): red / black / white, slanted panels and heavy italic
 * type, drawn procedurally – no game artwork. Tabs: Today, Party, Confidants, Shadows.
 */
final class P5View extends View {
    private static final float W = 1240, H = 1080;
    private static final int RED = 0xFFE5191C, BLACK = 0xFF0B0B0B, WHITE = 0xFFFFFFFF, GREY = 0xFF8A8A8A, DARK = 0xFF1C1C1C,
        CYAN = 0xFF34E0FF, PINK = 0xFFFF5FD2, YELLOW = 0xFFFFE600;
    private static final int TAB_TODAY = 0, TAB_PARTY = 1, TAB_CONF = 2, TAB_SHADOWS = 3;
    private static final int[] TAB_LABELS = { R.string.tab_today, R.string.tab_party, R.string.tab_confidants, R.string.tab_shadows };
    private static final int[] ELEMENTS = { R.string.el_phys, R.string.el_gun, R.string.el_fire, R.string.el_ice, R.string.el_elec,
        R.string.el_wind, R.string.el_psy, R.string.el_nuke, R.string.el_bless, R.string.el_curse };
    private static final int[] ELEMENT_COLORS = { 0xFFFF9A3C, 0xFFB8B8B8, 0xFFFF5A3C, 0xFF6FD0FF, 0xFFFFE600, 0xFF5BE38A, 0xFFFF7AD9,
        0xFF63F0E0, 0xFFFFF3B0, 0xFFB57BFF };
    private static final int[] STATS = { R.string.st_knowledge, R.string.st_charm, R.string.st_proficiency, R.string.st_guts, R.string.st_kindness };   // the game's own order
    private static final int[] DAYS = { R.string.d_sun, R.string.d_mon, R.string.d_tue, R.string.d_wed, R.string.d_thu, R.string.d_fri, R.string.d_sat };
    private static final int[] TIMES = { R.string.t_early, R.string.t_morning, R.string.t_daytime, R.string.t_lunch, R.string.t_after, R.string.t_evening, R.string.t_night };

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final Typeface heavy = Typeface.create(Typeface.create("sans-serif-black", Typeface.BOLD), Typeface.BOLD_ITALIC);
    private final Typeface plain = Typeface.create("sans-serif-condensed", Typeface.BOLD);

    private P5State state; private boolean connected;
    private List<String[]> shadows = new ArrayList<>();   // {name, affinities}, sorted by name, duplicates removed
    private int tab = TAB_TODAY; private float scroll, maxScroll, s = 1, ox, oy;
    private float downX, downY, lastY; private boolean dragging;
    private Runnable onTouchFinished, onHeaderHold;

    P5View(Context c) { super(c); text.setTypeface(heavy); }

    void setOnTouchFinished(Runnable r) { onTouchFinished = r; }
    void setOnHeaderHold(Runnable r) { onHeaderHold = r; }
    void setState(P5State st) { state = st; invalidate(); }
    void setConnected(boolean c) { connected = c; if (!c) state = null; invalidate(); }

    void setShadows(List<String[]> all) {
        List<String[]> out = new ArrayList<>();
        java.util.Set<String> seen = new java.util.HashSet<>();
        for (String[] e : all) {
            String n = e[0];
            // the game's own placeholders for unused slots
            if (n.isEmpty() || n.equals("RESERVE") || n.equals("LEER") || n.startsWith("BLANK") || n.equals("Nicht verwendet") || n.equals("Unused") || n.equals("?")) continue;
            if (n.indexOf('?') >= 0 || e[1].equals("wwwwwwwwww")) continue;   // test entries
            if (seen.add(n + "|" + e[1])) out.add(e);
        }
        out.sort((a, b) -> a[0].compareToIgnoreCase(b[0]));
        shadows = out; invalidate();
    }

    private String str(int id) { return getResources().getString(id); }

    // ------------------------------------------------------------------ drawing helpers

    /** Parallelogram: the right edge leans by `lean` pixels. */
    private void slab(Canvas c, float l, float t, float r, float b, float lean, int color) {
        path.reset(); path.moveTo(l + lean, t); path.lineTo(r + lean, t); path.lineTo(r, b); path.lineTo(l, b); path.close();
        fill.setColor(color); c.drawPath(path, fill);
    }

    private void label(Canvas c, String t, float x, float y, float size, int color, Paint.Align align) {
        text.setTypeface(heavy); text.setTextSize(size); text.setColor(color); text.setTextAlign(align); c.drawText(t, x, y, text);
    }

    private void body(Canvas c, String t, float x, float y, float size, int color, Paint.Align align) {
        text.setTypeface(plain); text.setTextSize(size); text.setColor(color); text.setTextAlign(align); c.drawText(t, x, y, text);
    }

    private String fit(String t, float size, float width, Typeface tf) {
        text.setTypeface(tf); text.setTextSize(size);
        if (text.measureText(t) <= width) return t;
        int n = text.breakText(t, true, width - text.measureText("…"), null);
        return t.substring(0, Math.max(1, n)) + "…";
    }

    private void bar(Canvas c, float l, float t, float r, float h, float frac, int color) {
        slab(c, l, t, r, t + h, 8, DARK);
        if (frac > 0) slab(c, l, t, l + (r - l) * Math.min(1, frac), t + h, 8, color);
    }

    /** Ten affinity cells. Letters: w weak, r resist, n null, p repel, d drain. */
    private void affinities(Canvas c, String aff, float l, float t, float cell, boolean withLabels) {
        for (int i = 0; i < 10 && i < aff.length(); i++) {
            char a = aff.charAt(i); float x = l + i * (cell + 6);
            int bg = a == 'w' ? RED : a == '-' ? DARK : a == 'r' ? 0xFF3A3A3A : WHITE;
            slab(c, x, t, x + cell, t + cell, 6, bg);
            if (withLabels) body(c, str(ELEMENTS[i]), x + cell / 2 + 3, t - 8, 17, ELEMENT_COLORS[i], Paint.Align.CENTER);
            String mark = a == 'w' ? str(R.string.a_weak) : a == 'r' ? str(R.string.a_resist) : a == 'n' ? str(R.string.a_null)
                : a == 'p' ? str(R.string.a_repel) : a == 'd' ? str(R.string.a_drain) : "";
            if (!mark.isEmpty()) label(c, mark, x + cell / 2 + 3, t + cell * 0.68f, cell * 0.5f, a == 'w' ? WHITE : a == 'r' ? GREY : BLACK, Paint.Align.CENTER);
        }
    }

    // ------------------------------------------------------------------ frame

    @Override protected void onDraw(Canvas c) {
        s = Math.min(getWidth() / W, getHeight() / H); ox = (getWidth() - W * s) / 2f; oy = (getHeight() - H * s) / 2f;
        c.drawColor(BLACK);
        c.save(); c.translate(ox, oy); c.scale(s, s);
        // backdrop: red wedge and halftone dots
        path.reset(); path.moveTo(0, 0); path.lineTo(W, 0); path.lineTo(W, 150); path.lineTo(0, 250); path.close();
        fill.setColor(RED); c.drawPath(path, fill);
        fill.setColor(0x22FFFFFF);
        for (int y = 0; y < 9; y++) for (int x = 0; x < 16; x++) c.drawCircle(820 + x * 28 + (y % 2) * 14, 16 + y * 22, 2 + (x + y) * 0.22f, fill);
        P5State st = state;
        drawHeader(c, st);
        drawTabs(c);
        c.save(); c.clipRect(0, 262, W, H);
        maxScroll = 0;
        if (st == null || !st.ready) message(c, str(connected ? R.string.starting : R.string.waiting), str(R.string.waiting_hint));
        else if (tab == TAB_SHADOWS) drawShadows(c);
        else if (!st.inGame) message(c, str(R.string.no_save), str(R.string.no_save_hint));
        else if (tab == TAB_TODAY) drawToday(c, st);
        else if (tab == TAB_PARTY) drawParty(c, st);
        else drawConfidants(c, st);
        c.restore();
        scroll = Math.max(0, Math.min(scroll, maxScroll));
        c.restore();
    }

    private void message(Canvas c, String title, String hint) {
        slab(c, 150, 500, 1070, 620, 30, WHITE);
        label(c, title, 620, 582, 62, BLACK, Paint.Align.CENTER);
        body(c, hint, 620, 690, 28, GREY, Paint.Align.CENTER);
    }

    private void drawHeader(Canvas c, P5State st) {
        if (st == null || !st.inGame) { label(c, "THOR P5R", 40, 110, 96, WHITE, Paint.Align.LEFT); return; }
        // date block like a torn calendar page
        slab(c, 30, 24, 330, 150, 24, BLACK);
        label(c, st.month + "/" + st.day, 62, 126, 104, WHITE, Paint.Align.LEFT);
        slab(c, 340, 30, 610, 86, 14, WHITE);
        label(c, st.dow >= 0 && st.dow < 7 ? str(DAYS[st.dow]) : "", 362, 74, 44, BLACK, Paint.Align.LEFT);
        body(c, st.time >= 0 && st.time < TIMES.length ? str(TIMES[st.time]) : "", 350, 128, 34, WHITE, Paint.Align.LEFT);
        if (st.money >= 0) {
            label(c, "¥" + String.format(Locale.US, "%,d", st.money), W - 40, 84, 60, WHITE, Paint.Align.RIGHT);
        }
        body(c, st.fps + " FPS", W - 40, 124, 24, 0xCCFFFFFF, Paint.Align.RIGHT);
    }

    private void drawTabs(Canvas c) {
        float w = 290, l = 24;
        for (int i = 0; i < 4; i++) {
            float x = l + i * (w + 10);
            boolean on = tab == i;
            slab(c, x, 186, x + w, 250, 18, on ? WHITE : DARK);
            label(c, str(TAB_LABELS[i]), x + w / 2 + 9, 234, 38, on ? BLACK : GREY, Paint.Align.CENTER);
        }
    }

    // ------------------------------------------------------------------ Today

    private void drawToday(Canvas c, P5State st) {
        float y = 290;
        label(c, str(R.string.social), 40, y + 40, 40, RED, Paint.Align.LEFT);
        y += 62;
        for (int i = 0; i < 5; i++) {
            slab(c, 30, y, 1200, y + 70, 16, DARK);
            label(c, str(STATS[i]), 60, y + 50, 38, WHITE, Paint.Align.LEFT);
            for (int k = 0; k < 5; k++) slab(c, 520 + k * 74, y + 16, 520 + k * 74 + 60, y + 54, 10, k < st.statLv[i] ? RED : 0xFF333333);
            label(c, str(R.string.rank) + " " + st.statLv[i], 930, y + 50, 34, WHITE, Paint.Align.LEFT);
            body(c, st.statPts[i] + " " + str(R.string.points), 1180, y + 48, 26, GREY, Paint.Align.RIGHT);
            y += 80;
        }
        y += 20;
        label(c, str(R.string.tab_party), 40, y + 40, 40, RED, Paint.Align.LEFT);
        y += 62;
        for (int i = 0; i < st.party.size() && i < 4; i++) {
            P5State.Member m = st.party.get(i); float x = 30 + i * 296;
            slab(c, x, y, x + 280, y + 96, 12, DARK);
            label(c, fit(m.name, 30, 250, heavy), x + 24, y + 38, 30, WHITE, Paint.Align.LEFT);
            bar(c, x + 14, y + 52, x + 262, 14, m.maxHp > 0 ? m.hp / (float) m.maxHp : 0, CYAN);
            bar(c, x + 14, y + 72, x + 262, 14, m.maxSp > 0 ? m.sp / (float) m.maxSp : 0, PINK);
        }
        y += 126;
        if (st.alert >= 0 && st.fieldMajor >= 100) {   // palaces and Mementos use the high field numbers
            label(c, str(R.string.security), 40, y + 40, 40, RED, Paint.Align.LEFT);
            bar(c, 330, y + 8, 1050, 40, st.alert / 100f, st.alert >= 70 ? RED : WHITE);
            label(c, st.alert + "%", 1190, y + 42, 40, WHITE, Paint.Align.RIGHT);
        }
    }

    // ------------------------------------------------------------------ Party

    private void drawParty(Canvas c, P5State st) {
        float y = 286 - scroll;
        float cw = 290;
        for (int i = 0; i < st.party.size() && i < 4; i++) {
            P5State.Member m = st.party.get(i); float x = 24 + i * (cw + 10);
            slab(c, x, y, x + cw, y + 190, 14, DARK);
            slab(c, x, y, x + cw, y + 52, 14, i == 0 ? RED : WHITE);
            label(c, fit(m.name, 34, cw - 100, heavy), x + 26, y + 40, 34, i == 0 ? WHITE : BLACK, Paint.Align.LEFT);
            body(c, str(R.string.lv) + " " + m.lv, x + cw - 6, y + 38, 26, i == 0 ? WHITE : BLACK, Paint.Align.RIGHT);
            body(c, "HP", x + 14, y + 92, 22, CYAN, Paint.Align.LEFT);
            bar(c, x + 56, y + 74, x + cw - 16, 22, m.maxHp > 0 ? m.hp / (float) m.maxHp : 0, CYAN);
            body(c, m.hp + " / " + m.maxHp, x + cw - 20, y + 122, 22, WHITE, Paint.Align.RIGHT);
            body(c, "SP", x + 14, y + 152, 22, PINK, Paint.Align.LEFT);
            bar(c, x + 56, y + 134, x + cw - 16, 22, m.maxSp > 0 ? m.sp / (float) m.maxSp : 0, PINK);
            body(c, m.sp + " / " + m.maxSp, x + cw - 20, y + 182, 22, WHITE, Paint.Align.RIGHT);
            body(c, fit(m.persona, 22, cw - 150, plain), x + 14, y + 182, 22, GREY, Paint.Align.LEFT);
        }
        y += 214;
        label(c, str(R.string.personas), 40, y + 40, 40, RED, Paint.Align.LEFT);
        y += 58;
        for (P5State.Persona p : st.personas) {
            slab(c, 24, y, 1204, y + 150, 16, DARK);
            slab(c, 24, y, 424, y + 56, 16, WHITE);
            label(c, fit(p.name, 36, 290, heavy), 56, y + 43, 36, BLACK, Paint.Align.LEFT);
            body(c, str(R.string.lv) + " " + p.lv, 412, y + 40, 26, BLACK, Paint.Align.RIGHT);
            if (!p.aff.isEmpty()) affinities(c, p.aff, 470, y + 30, 64, true);
            StringBuilder sk = new StringBuilder();
            for (String n : p.skills) sk.append(sk.length() > 0 ? "  ·  " : "").append(n);
            body(c, fit(sk.toString(), 26, 1130, plain), 46, y + 132, 26, WHITE, Paint.Align.LEFT);
            y += 162;
        }
        maxScroll = Math.max(0, y + scroll - H + 20);
    }

    // ------------------------------------------------------------------ Confidants

    private void drawConfidants(Canvas c, P5State st) {
        float y = 286 - scroll;
        if (st.confidants.isEmpty()) { message(c, str(R.string.no_confidants), ""); return; }
        for (P5State.Confidant q : st.confidants) {
            boolean ready = q.next > 0 && q.pts >= q.next && q.lv < 10;
            slab(c, 24, y, 1204, y + 84, 16, ready ? RED : DARK);
            label(c, fit(q.name, 40, 380, heavy), 60, y + 58, 40, WHITE, Paint.Align.LEFT);
            for (int k = 0; k < 10; k++) slab(c, 470 + k * 44, y + 24, 470 + k * 44 + 34, y + 60, 8, k < q.lv ? WHITE : 0xFF3A3A3A);
            label(c, q.lv >= 10 ? "MAX" : String.valueOf(q.lv), 940, y + 60, 44, WHITE, Paint.Align.CENTER);
            if (ready) label(c, str(R.string.rank_up), 1180, y + 54, 24, YELLOW, Paint.Align.RIGHT);
            else if (q.next > 0) body(c, q.pts + " / " + q.next, 1180, y + 54, 28, GREY, Paint.Align.RIGHT);
            y += 94;
        }
        maxScroll = Math.max(0, y + scroll - H + 20);
    }

    // ------------------------------------------------------------------ Shadows (lookup)

    private static final float ROW = 74, INDEX_W = 70;

    private void drawShadows(Canvas c) {
        if (shadows.isEmpty()) { message(c, str(R.string.no_shadows), ""); return; }
        float top = 300;
        for (int i = 0; i < 10; i++) body(c, str(ELEMENTS[i]), 618 + i * 52 + 26, top - 8, 17, ELEMENT_COLORS[i], Paint.Align.CENTER);
        c.save(); c.clipRect(0, top, W - INDEX_W, H);
        int first = Math.max(0, (int) (scroll / ROW)), last = Math.min(shadows.size(), first + 12);
        for (int i = first; i < last; i++) {
            float y = top + i * ROW - scroll; String[] e = shadows.get(i);
            slab(c, 20, y + 4, W - INDEX_W - 14, y + ROW - 4, 12, i % 2 == 0 ? DARK : 0xFF141414);
            label(c, fit(e[0], 32, 540, heavy), 48, y + 49, 32, WHITE, Paint.Align.LEFT);
            if (!e[1].isEmpty()) affinities(c, e[1], 618, y + 14, 46, false);
        }
        c.restore();
        // A–Z index on the right edge
        float ih = (H - top - 10) / 26f;
        for (int k = 0; k < 26; k++) body(c, String.valueOf((char) ('A' + k)), W - INDEX_W / 2, top + (k + 0.8f) * ih, 24, WHITE, Paint.Align.CENTER);
        maxScroll = Math.max(0, shadows.size() * ROW - (H - top) + 10);
    }

    private void jumpToLetter(float y) {
        int k = Math.max(0, Math.min(25, (int) ((y - 300) / ((H - 310) / 26f))));
        char want = (char) ('A' + k);
        for (int i = 0; i < shadows.size(); i++) {
            if (Character.toUpperCase(java.text.Normalizer.normalize(shadows.get(i)[0], java.text.Normalizer.Form.NFD).charAt(0)) >= want) { scroll = i * ROW; break; }
        }
        invalidate();
    }

    // ------------------------------------------------------------------ touch

    @Override public boolean onTouchEvent(MotionEvent e) {
        float x = (e.getX() - ox) / s, y = (e.getY() - oy) / s;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX = x; downY = y; lastY = y; dragging = false;
                if (tab == TAB_SHADOWS && x > W - INDEX_W && y > 290) jumpToLetter(y);
                return true;
            case MotionEvent.ACTION_MOVE:
                if (tab == TAB_SHADOWS && downX > W - INDEX_W && downY > 290) { jumpToLetter(y); return true; }
                if (Math.abs(y - downY) > 12) dragging = true;
                if (dragging) { scroll -= (y - lastY); lastY = y; invalidate(); }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (onTouchFinished != null) postDelayed(onTouchFinished, 120);
                if (!dragging && e.getActionMasked() == MotionEvent.ACTION_UP) {
                    if (downY < 170 && e.getEventTime() - e.getDownTime() > 700 && onHeaderHold != null) { onHeaderHold.run(); return true; }
                    if (y >= 180 && y <= 256) {
                        int i = (int) ((x - 24) / 300);
                        if (i >= 0 && i < 4 && i != tab) { tab = i; scroll = 0; invalidate(); performClick(); }
                    }
                }
                return true;
        }
        return super.onTouchEvent(e);
    }

    @Override public boolean performClick() { return super.performClick(); }
}
