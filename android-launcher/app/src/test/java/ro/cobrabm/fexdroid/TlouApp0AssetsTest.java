package ro.cobrabm.fexdroid;

import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.stream.Stream;
import java.util.stream.Collectors;
import java.nio.file.Path;
import java.util.Comparator;

public final class TlouApp0AssetsTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static void write(Path p, String v) throws Exception {
        Files.write(p,v.getBytes(StandardCharsets.UTF_8));
    }
    private static String read(Path p) throws Exception {
        return new String(Files.readAllBytes(p),StandardCharsets.UTF_8);
    }
    private static void erase(Path p) throws Exception {
        try (Stream<Path> files = Files.walk(p)) {
            for (Path item: files.sorted(Comparator.reverseOrder()).collect(Collectors.toList())) Files.delete(item);
        }
    }
    public static void main(String[] args) throws Exception {
        Path tmp=Files.createTempDirectory("tlou-app0-");
        try {
            Path view=tmp.resolve("prepared/tlou-view");
            Path nested=view.resolve("dvdroot_ps4/build/ps4/main");
            Files.createDirectories(nested);
            write(nested.resolve("common-textures.psarc"),"dummy-common");
            write(nested.resolve("core-textures.psarc"),"dummy-core");
            Files.createDirectories(nested.resolve("lut0"));
            write(nested.resolve("lut0/default-color-gray.dds"),"dummy-lut");

            String result=TlouApp0Assets.ensure(view.toFile());
            Path alias=view.resolve("build");
            check(Files.isSymbolicLink(alias), "missing relative asset alias");
            check(Files.readSymbolicLink(alias).toString().replace('\\','/').equals("dvdroot_ps4/build"),"non-relative alias");
            check(result.contains("mapping=dvdroot_ps4/build"),"no mapping status");
            check(result.contains("common-textures.psarc=true"),"common archive invisible");
            check(result.contains("core-textures.psarc=true"),"core archive invisible");
            check(read(alias.resolve("ps4/main/core-textures.psarc")).equals("dummy-core"), "alias not readable");
            TlouApp0Assets.ensure(view.toFile()); // must not overwrite on relaunch

            Path directView=tmp.resolve("direct");
            Files.createDirectories(directView.resolve("build/ps4/main"));
            Files.createDirectories(directView.resolve("dvdroot_ps4/build/ps4/main"));
            write(directView.resolve("build/ps4/main/core-textures.psarc"),"root-priority");
            String direct=TlouApp0Assets.ensure(directView.toFile());
            check(direct.contains("mapping=root"),"wrong source precedence");
            check(!Files.isSymbolicLink(directView.resolve("build")),"overwrote existing root");
            check(read(directView.resolve("build/ps4/main/core-textures.psarc")).equals("root-priority"),"overwrote direct content");

            // A common real-world layout: game/build is a symlink to the
            // original game folder, while 1.11 archives are under dvdroot_ps4.
            // The previous fix skipped this because /app0/build already existed.
            Path source=tmp.resolve("original-game/build/ps4/main");
            Files.createDirectories(source);
            write(source.resolve("common-textures.psarc"),"original-priority");
            Path mixed=tmp.resolve("mixed/tlou-view");
            Files.createDirectories(mixed);
            Files.createSymbolicLink(mixed.resolve("build"),
                tmp.resolve("original-game/build").toRealPath());
            Path secondary=mixed.resolve("dvdroot_ps4/build/ps4/main");
            Files.createDirectories(secondary);
            write(secondary.resolve("common-textures.psarc"),"nested-loses");
            write(secondary.resolve("core-textures.psarc"),"nested-core");
            Files.createDirectories(secondary.resolve("lut0"));
            write(secondary.resolve("lut0/default-color-gray.dds"),"nested-lut");
            String merged=TlouApp0Assets.ensure(mixed.toFile());
            check(merged.contains("mapping=root+dvdroot_ps4"),"existing root was not merged");
            check(merged.contains("common-textures.psarc=true"),"existing archive missing");
            check(merged.contains("core-textures.psarc=true"),"missing core archive not added");
            check(merged.contains("default-color-gray.dds=true"),"missing LUT not added");
            check(!Files.isSymbolicLink(mixed.resolve("build")),"build is still original symlink");
            check(read(mixed.resolve("build/ps4/main/common-textures.psarc"))
                  .equals("original-priority"),"base archive precedence broken");
            check(read(mixed.resolve("build/ps4/main/core-textures.psarc"))
                  .equals("nested-core"),"fallback archive not visible");
            check(!Files.exists(source.resolve("core-textures.psarc")),
                  "modified original game directory through symlink");
            check(!Files.exists(source.resolve("lut0")),
                  "modified original game assets through symlink");
            TlouApp0Assets.ensure(mixed.toFile()); // idempotent after detaching links
            check(read(mixed.resolve("build/ps4/main/core-textures.psarc"))
                  .equals("nested-core"),"relaunch changed merged data");

            Path missing=tmp.resolve("missing");
            Files.createDirectories(missing);
            String absent=TlouApp0Assets.ensure(missing.toFile());
            check(absent.contains("nenhum build encontrado"),"missing assets not disclosed");
            check(!Files.exists(missing.resolve("build")),"invented assets");
            System.out.println("TLOU /app0 asset-view tests passed");
        } finally {
            erase(tmp);
        }
    }
}
