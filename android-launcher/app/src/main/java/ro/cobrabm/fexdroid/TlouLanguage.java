package ro.cobrabm.fexdroid;

import android.content.Context;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import android.os.LocaleList;
import java.util.Locale;

/** App-only translations. No game/guest language override is ever sent. */
final class TlouLanguage {
    static final String[] CODES={"pt","en","es","fr","de","it","tr","pl","ru","hi"};
    static final String[] NAMES={"Português (Brasil)","English","Español","Français",
        "Deutsch","Italiano","Türkçe","Polski","Русский","हिन्दी"};
    private static Map<String,String[]> dictionary;
    private TlouLanguage() {}

    /** The app follows the user's first system language unless explicitly overridden.
     * Unsupported system languages fall back to EN (index 1), not Brazilian PT.
     * Game/guest language remains controlled only by the game's own settings. */
    static int index(Context context) {
        String selected=PortSettings.prefs(context).getString("ui_language","auto");
        if(!"auto".equals(selected)) {
            for(int i=0;i<CODES.length;i++)if(CODES[i].equals(selected))return i;
        }
        LocaleList locales=LocaleList.getDefault();
        Locale first=locales.isEmpty()?Locale.getDefault():locales.get(0);
        String system=first.getLanguage().toLowerCase(Locale.ROOT);
        for(int i=0;i<CODES.length;i++)if(CODES[i].equals(system))return i;
        return 1; // English fallback when system locale is not supported.
    }

    /** Language spinner: automatic first, then the ten supported manual overrides. */
    static String[] options(Context context) {
        String[] names=new String[NAMES.length+1];
        names[0]=t(context,"Automático (idioma do sistema)");
        System.arraycopy(NAMES,0,names,1,NAMES.length);
        return names;
    }

    static int selectedOption(Context context) {
        String saved=PortSettings.prefs(context).getString("ui_language","auto");
        if("auto".equals(saved))return 0;
        for(int i=0;i<CODES.length;i++)if(CODES[i].equals(saved))return i+1;
        return 0;
    }

    static String selectedCode(int spinnerIndex) {
        return spinnerIndex<=0?"auto":CODES[spinnerIndex-1];
    }

    private static synchronized void ensureLoaded(Context context) {
        if(dictionary!=null)return;
        Map<String,String[]> loaded=new HashMap<>();
        try(BufferedReader r=new BufferedReader(new InputStreamReader(
            context.getAssets().open("ui-translations.tsv"),StandardCharsets.UTF_8))) {
            String line;
            while((line=r.readLine())!=null) {
                if(line.isEmpty() || line.charAt(0)=='#')continue;
                String[] fields=line.split("\\t",-1);
                if(fields.length!=CODES.length || fields[0].isEmpty())continue;
                loaded.put(fields[0],fields);
            }
        } catch(Exception ignored) {
            // Never break gameplay or settings merely because translation
            // assets are missing in a developer checkout.
        }
        dictionary=loaded;
    }

    static String t(Context context,String original) {
        if(original==null)return "";
        final int lang=index(context);
        if(lang==0)return original;
        ensureLoaded(context);
        String[] values=dictionary.get(original);
        if(values==null)return original;
        if(!values[lang].isEmpty())return values[lang];
        // Incomplete optional translations fall back to English, never to
        // a string in an arbitrary third language.
        return !values[1].isEmpty()?values[1]:original;
    }
}
