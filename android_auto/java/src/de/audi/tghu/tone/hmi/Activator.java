/*
 * Patched de.audi.tghu.tone.hmi.Activator: identical to stock except that the model bank and
 * screen factory come from com.luka.carplay.tone (adds the Sound effects "UpMix" row).
 * Any failure there falls back to the stock classes, so the worst case is "no UpMix row".
 */
package de.audi.tghu.tone.hmi;

import com.luka.carplay.tone.UpmixToneUi;
import com.luka.carplay.core.UpmixSetting;
import de.audi.atip.activator.AbstractHMIActivator;
import de.audi.atip.hmi.HMIConditionBank;
import de.audi.atip.hmi.view.AbstractScreenFactory;
import de.audi.atip.model.ToneModelBank;
import de.audi.tghu.tone.hmi.evohigh.ToneConditionBank;
import de.audi.tghu.tone.hmi.evohigh.ToneScreenFactory;

public class Activator extends AbstractHMIActivator {
    private ToneScreenFactory instance = null;
    private ToneConditionBank conditionBank;

    public Activator() {
        /* the stock constants as the stock bytecode inlines them (AppNameConst.APP_NAME_TONE,
         * RemoteHMIParameters.PARAMETER_VARIANT): no reference to either class */
        super(10, "Tone", System.getProperty("variant.skin", "EvoHigh"), newBank());
    }

    private static ToneModelBank newBank() {
        try {
            return UpmixToneUi.newBank();
        } catch (Throwable t) {
            return new ToneModelBank();
        }
    }

    protected synchronized AbstractScreenFactory getScreenFactory() {
        if (this.instance == null) {
            /* Start the shared level bus from Tone itself.  Keeping this here means
             * the combined deployment does not have to replace the already-tested
             * cockpit/HUD CarPlayApp class just to expose the UpMix row. */
            try {
                UpmixSetting.boot(this.getFramework());
            } catch (Throwable t) {
                /* The row still falls back safely below if the bus is unavailable. */
            }
            try {
                this.instance = UpmixToneUi.newFactory(this.getFramework());
            } catch (Throwable t) {
                this.instance = null;
            }
            if (this.instance == null) {
                this.instance = new ToneScreenFactory(this.getFramework());
            }
        }
        return this.instance;
    }

    public HMIConditionBank getConditionBank() {
        if (this.conditionBank == null) {
            this.conditionBank = new ToneConditionBank((ToneScreenFactory) this.getScreenFactory());
        }
        return this.conditionBank;
    }
}
