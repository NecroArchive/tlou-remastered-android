package ro.cobrabm.fexdroid;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;

public final class TlouApp0AssetsTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static void erase(Path p) throws Exception {
        try (var files = Files.walk(p)) {
            for (Path item: files.sorted(Comparator.reverseOrder()).toList()) Files.delete(item);
        }
    }
    public static void main(String[] args) throws Exception {
        Path tmp=Files.createTempDirectory("tlou-app0-");
        try {
            Path view=tmp.resolve("prepared/tlou-view");
            Path nested=view.resolve("dvdroot_ps4/build/ps4/main");
            Files.createDirectories(nested);
            Files.writeString(nested.resolve("common-textures.psarc"),"dummy-common");
            Files.writeString(nested.resolve("core-textures.psarc"),"dummy-core");
            Files.createDirectories(nested.resolve("lut0"));
            Files.writeString(nested.resolve("lut0/default-color-gray.dds"),"dummy-lut");

            String result=TlouApp0Assets.ensure(view.toFile());
            Path alias=view.resolve("build");
            check(Files.isSymbolicLink(alias), "missing relative asset alias");
            check(Files.readSymbolicLink(alias).toString().replace('\\','/').equals("dvdroot_ps4/build"),"non-relative alias");
            check(result.contains("mapping=dvdroot_ps4/build"),"no mapping status");
            check(result.contains("common-textures.psarc=true"),"common archive invisible");
            check(result.contains("core-textures.psarc=true"),"core archive invisible");
            check(Files.readString(alias.resolve("ps4/main/core-textures.psarc")).equals("dummy-core"), "alias not readable");
            TlouApp0Assets.ensure(view.toFile()); // must not overwrite on relaunch

            Path directView=tmp.resolve("direct");
            Files.createDirectories(directView.resolve("build/ps4/main"));
            Files.createDirectories(directView.resolve("dvdroot_ps4/build/ps4/main"));
            Files.writeString(directView.resolve("build/ps4/main/core-textures.psarc"),"root-priority");
            String direct=TlouApp0Assets.ensure(directView.toFile());
            check(direct.contains("mapping=root"),"wrong source precedence");
            check(!Files.isSymbolicLink(directView.resolve("build")),"overwrote existing root");
            check(Files.readString(directView.resolve("build/ps4/main/core-textures.psarc")).equals("root-priority"),"overwrote direct content");

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
