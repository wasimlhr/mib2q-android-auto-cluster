/*
 * The car's day/night state for the cockpit map's "Map theme: Auto".
 *
 * Run 150 (owner): Map theme Auto made Android Auto crash - the hook sent UiConfig ui_theme AUTOMATIC (0) on
 * the cluster display, which the phone does not handle there. Auto is now done on our side: the flag Audi
 * itself passes to Android Auto (INightDayModeHandler.getRequestedNightMode, the same one that switches the
 * centre screen and the Virtual Cockpit with the headlights) is polled every 2 s and written to
 * /tmp/sq5_daynight as a fixed 8-byte record "SQ5D 0\n " (0 day, 1 night, overwritten in place, never
 * truncated). The hook (aa-cluster-live src/uiconfig.c) sends Light or Dark from it.
 */
package com.sq5.aa.luka;

import de.audi.app.terminalmode.IContext;
import de.audi.app.terminalmode.INightDayModeHandler;
import java.io.RandomAccessFile;

public final class DayNight {
    static final String FILE = "/tmp/sq5_daynight";
    private static Thread thread;
    private static volatile int last = -1;

    private DayNight() { }

    /** 1 night, 0 day, -1 unknown. */
    static int read(IContext context) {
        try {
            INightDayModeHandler h = context == null ? null : context.getNightDayModeHandler();
            return h == null ? -1 : (h.getRequestedNightMode() ? 1 : 0);
        } catch (Throwable t) {
            return -1;
        }
    }

    static void write(int night) {
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(FILE, "rw");
            f.seek(0);
            f.write(("SQ5D " + night + "\n ").getBytes());
        } catch (Throwable t) {
            /* /tmp not writable: the hook falls back to Dark */
        } finally {
            if (f != null) try { f.close(); } catch (Throwable t) { /* ignore */ }
        }
    }

    public static synchronized void start(final IContext context) {
        if (thread != null) return;
        thread = new Thread("sq5-daynight") {
            public void run() {
                long rewrite = 0;
                for (;;) {
                    int v = read(context);
                    long now = System.currentTimeMillis();
                    if (v >= 0 && (v != last || now >= rewrite)) {   /* on change, and every 30 s */
                        if (v != last) AaLog.log("daynight: " + (v == 1 ? "night" : "day"));
                        last = v;
                        write(v);
                        rewrite = now + 30000L;
                    }
                    try { Thread.sleep(2000L); } catch (InterruptedException e) { return; }
                }
            }
        };
        thread.setDaemon(true);
        thread.start();
    }
}
