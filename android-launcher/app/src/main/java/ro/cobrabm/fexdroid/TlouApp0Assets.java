package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;

/** Mount /app0/build for PS4 dumps that keep game assets under dvdroot_ps4.
 *
 * The prepared tlou-view already overlays the 1.11 update via symlinks. Make
 * one RELATIVE alias inside that same view so update precedence is preserved,
 * without copying PSARCs or modifying the user's original game directory.
 */
final class TlouApp0Assets {
    private TlouApp0Assets() {}

    static String ensure(File view) throws IOException {
        if (!view.isDirectory()) throw new IOException("TLOU view ausente: " + view);
        Path root=view.toPath();
        Path direct=root.resolve("build");
        Path nested=root.resolve("dvdroot_ps4").resolve("build");
        String mapping="root";

        if (!Files.isDirectory(direct)) {
            if (Files.exists(direct,LinkOption.NOFOLLOW_LINKS)) {
                throw new IOException("TLOU /app0/build inválido ou link quebrado: "+direct);
            }
            if (Files.isDirectory(nested)) {
                // Relative to tlou-view, not to a temporary preparation path.
                // If the update replaces any dvdroot file, the existing view's
                // merged dvdroot_ps4 tree is still used for that file.
                Files.createSymbolicLink(direct,Paths.get("dvdroot_ps4","build"));
                mapping="dvdroot_ps4/build";
            } else {
                mapping="nenhum build encontrado";
            }
        }
        Path main=direct.resolve("ps4").resolve("main");
        Path common=main.resolve("common-textures.psarc");
        Path core=main.resolve("core-textures.psarc");
        Path lut=main.resolve("lut0").resolve("default-color-gray.dds");
        return "TLOU /app0 assets: mapping="+mapping
             +" main="+Files.isDirectory(main)
             +" common-textures.psarc="+Files.isRegularFile(common)
             +" core-textures.psarc="+Files.isRegularFile(core)
             +" default-color-gray.dds="+Files.isRegularFile(lut);
    }
}
