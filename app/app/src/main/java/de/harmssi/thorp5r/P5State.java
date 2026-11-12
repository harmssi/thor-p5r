package de.harmssi.thorp5r;

import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.List;

/** One snapshot of the status mod (GET /state). */
final class P5State {
    static final class Member { int id, lv, hp, maxHp, sp, maxSp; String name = "", persona = ""; }
    static final class Persona { String name = "", aff = ""; int lv; List<String> skills = new ArrayList<>(); int[] st = new int[5]; }
    static final class Confidant { int id, lv, pts, next; String name = ""; }

    boolean ready, inGame;
    int fps, month, day, dow, time, alert, mode;
    long money = -1;
    int fieldMajor, fieldMinor;
    final List<Member> party = new ArrayList<>();
    final List<Persona> personas = new ArrayList<>();
    final List<Confidant> confidants = new ArrayList<>();
    final int[] statPts = new int[5], statLv = new int[5];

    static P5State parse(String json) throws Exception {
        JSONObject o = new JSONObject(json);
        P5State s = new P5State();
        s.ready = o.optBoolean("ready"); s.inGame = o.optBoolean("inGame"); s.fps = o.optInt("fps");
        JSONArray seq = o.optJSONArray("seq");
        s.mode = seq != null ? seq.optInt(1, -1) : -1;
        if (!s.inGame) return s;
        JSONArray f = o.optJSONArray("field");
        if (f != null) { s.fieldMajor = f.optInt(0); s.fieldMinor = f.optInt(1); }
        JSONObject d = o.optJSONObject("date");
        if (d != null) {
            s.month = d.optInt("month"); s.day = d.optInt("day"); s.dow = d.optInt("dow"); s.time = d.optInt("time");
        }
        s.alert = o.optInt("alert", -1);
        s.money = o.isNull("money") ? -1 : o.optLong("money", -1);
        JSONArray p = o.optJSONArray("party");
        for (int i = 0; p != null && i < p.length(); i++) {
            JSONObject x = p.getJSONObject(i); Member m = new Member();
            m.id = x.optInt("id"); m.name = x.optString("name"); m.lv = x.optInt("lv"); m.hp = x.optInt("hp"); m.maxHp = x.optInt("maxHp");
            m.sp = x.optInt("sp"); m.maxSp = x.optInt("maxSp"); m.persona = x.optString("personaName");
            s.party.add(m);
        }
        JSONArray ps = o.optJSONArray("personas");
        for (int i = 0; ps != null && i < ps.length(); i++) {
            JSONObject x = ps.getJSONObject(i); Persona q = new Persona();
            q.name = x.optString("name"); q.lv = x.optInt("lv"); q.aff = x.optString("aff");
            JSONArray sk = x.optJSONArray("skills");
            for (int k = 0; sk != null && k < sk.length(); k++) q.skills.add(sk.optString(k));
            JSONArray st = x.optJSONArray("st");
            for (int k = 0; st != null && k < 5 && k < st.length(); k++) q.st[k] = st.optInt(k);
            s.personas.add(q);
        }
        JSONArray stats = o.optJSONArray("stats");
        for (int i = 0; stats != null && i < 5 && i < stats.length(); i++) {
            s.statPts[i] = stats.getJSONObject(i).optInt("pts"); s.statLv[i] = stats.getJSONObject(i).optInt("lv");
        }
        JSONArray c = o.optJSONArray("confidants");
        for (int i = 0; c != null && i < c.length(); i++) {
            JSONObject x = c.getJSONObject(i); Confidant q = new Confidant();
            q.id = x.optInt("id"); q.lv = x.optInt("lv"); q.pts = x.optInt("pts"); q.next = x.optInt("next"); q.name = x.optString("name");
            s.confidants.add(q);
        }
        return s;
    }
}
