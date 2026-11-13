package ro.cobrabm.fexdroid;

import android.content.SharedPreferences;
import org.json.JSONObject;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Resolve the original game and its installed update on EVERY APK launch.
 * Installed Android APK upgrades preserve app-private files and preferences,
 * but an old launcher could select a prepared folder and skip --update.
 * Fail closed if an installed update was removed: never silently boot 1.00.
 */
final class GameInstallState {
    private GameInstallState() {}

    static JSONObject marker(File folder) throws Exception {
        File record=new File(folder.getCanonicalFile(),"device-ready.json");
        if(!record.isFile())return null;
        try {
            return new JSONObject(new String(Files.readAllBytes(record.toPath()),
                                              StandardCharsets.UTF_8));
        } catch(Exception error) {
            throw new IOException("Registro do jogo preparado inválido: "+record,error);
        }
    }

    static File base(File selected) throws Exception {
        File root=selected.getCanonicalFile();
        if(InstallTools.prepared(root)) {
            JSONObject info=marker(root);
            if(info==null || info.optString("game","").isEmpty())
                throw new IOException("A pasta preparada não informa a origem do jogo.");
            root=new File(info.getString("game")).getCanonicalFile();
        }
        // An interrupted physical update must be rolled back BEFORE reading
        // the eboot, SFO or preparing the merged view on a later APK launch.
        UpdateInPlace.recover(root);
        if(!new File(root,"eboot.bin").isFile() ||
           !new File(root,"sce_sys/param.sfo").isFile())
            throw new IOException("A pasta original do jogo não está acessível. Selecione-a novamente.");
        return root;
    }

    static File update(File game,File selected,File managed,SharedPreferences prefs) throws Exception {
        File original=game.getCanonicalFile();
        String updateGame=prefs.getString("update_game","");
        String updatePath=prefs.getString("update_path","");
        // A user-selected update takes priority over any old cached preparation.
        if(!updateGame.isEmpty() && !updatePath.isEmpty() &&
           new File(updateGame).getCanonicalFile().equals(original))
            return requireUpdate(new File(updatePath));

        // Migrate older APKs that stored only the prepared folder or lost the
        // explicit update_path; the marker contains the original --update.
        File[] candidates={selected,managed};
        for(File candidate:candidates) {
            JSONObject info=marker(candidate);
            if(info==null || info.optString("game","").isEmpty() ||
               !new File(info.optString("game")).getCanonicalFile().equals(original))
                continue;
            String path=info.optString("update","");
            if(!path.isEmpty() && !"null".equals(path))
                return requireUpdate(new File(path));
            // An explicitly installed 1.11 update MUST NOT silently fall back.
            if("01.11".equals(info.optString("update_version","")))
                throw new IOException("A atualização 01.11 não foi localizada. Reselecione a pasta do update.");
        }
        return null;
    }

    private static File requireUpdate(File path) throws Exception {
        File found=path.getCanonicalFile();
        if(!new File(found,"sce_sys/param.sfo").isFile())
            throw new IOException("A atualização anteriormente instalada foi movida ou removida: "+found+
                                  ". Selecione novamente o PKG ou a pasta do update.");
        return found;
    }
}
