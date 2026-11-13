package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Merge game and update assets into the private tlou-view WITHOUT copying
 * game data or writing through links to the original game/update directories.
 * Existing /app0/build entries always win. Missing children are sourced from
 * dvdroot_ps4/build, which may itself contain the prepared update overlay.
 */
final class TlouApp0Assets {
    private TlouApp0Assets() {}

    private static final LinkOption NO_LINKS=LinkOption.NOFOLLOW_LINKS;

    private static final class Counts {
        int linked;
        int detached;
    }

    private static boolean present(Path path) {
        return Files.exists(path,NO_LINKS);
    }

    private static void link(Path source,Path destination,Counts counts) throws IOException {
        Files.createSymbolicLink(destination,source.toRealPath());
        counts.linked++;
    }

    /** Detach ONLY this symlink, replicating its directory entries as links in
     * a private directory. No child is written to a source game directory. */
    private static void materializePrivateDirectory(Path target,Counts counts) throws IOException {
        if (!Files.isSymbolicLink(target)) return;
        Path real=target.toRealPath();
        if (!Files.isDirectory(real))
            throw new IOException("TLOU asset-directory link is not a directory: "+target);
        Path temporary=Files.createTempDirectory(target.getParent(),".tlou-asset-view-");
        boolean moved=false,removed=false;
        try {
            try (DirectoryStream<Path> children=Files.newDirectoryStream(real)) {
                for (Path entry:children)
                    Files.createSymbolicLink(temporary.resolve(entry.getFileName()),entry.toRealPath());
            }
            Files.delete(target); removed=true;
            Files.move(temporary,target); moved=true;
            counts.detached++;
        } catch(IOException failure) {
            if(removed && !present(target)) {
                try { Files.createSymbolicLink(target,real); }
                catch(IOException restoreError) { failure.addSuppressed(restoreError); }
            }
            throw failure;
        } finally {
            if(!moved && Files.isDirectory(temporary,NO_LINKS)) {
                try(DirectoryStream<Path> entries=Files.newDirectoryStream(temporary)){
                    for(Path child:entries)Files.deleteIfExists(child);
                }
                Files.deleteIfExists(temporary);
            }
        }
    }

    /** Merge without changing the original files and without giving the
     * nested base files priority over an existing direct update file. */
    private static void addMissing(Path direct,Path nested,Counts counts) throws IOException {
        if(!Files.isDirectory(nested))return;
        if(!present(direct)){
            link(nested,direct,counts);
            return;
        }
        if(!Files.isDirectory(direct))return; // existing file wins
        materializePrivateDirectory(direct,counts);
        try(DirectoryStream<Path> children=Files.newDirectoryStream(nested)){
            for(Path child:children){
                Path dest=direct.resolve(child.getFileName());
                if(!present(dest))link(child,dest,counts);
                else if(Files.isDirectory(child)&&Files.isDirectory(dest))
                    addMissing(dest,child,counts);
            }
        }
    }

    static String ensure(File view) throws IOException {
        if(!view.isDirectory())throw new IOException("TLOU view ausente: "+view);
        Path root=view.toPath();
        if(Files.isSymbolicLink(root))throw new IOException("TLOU view não pode ser um link");
        Path direct=root.resolve("build");
        Path nested=root.resolve("dvdroot_ps4").resolve("build");
        Counts counts=new Counts();
        String mapping="root";
        boolean nestedPresent=Files.isDirectory(nested);

        if(!present(direct)){
            if(nestedPresent){
                Files.createSymbolicLink(direct,Paths.get("dvdroot_ps4","build"));
                mapping="dvdroot_ps4/build";
                counts.linked++;
            }else mapping="nenhum build encontrado";
        }else if(!Files.isDirectory(direct)){
            throw new IOException("TLOU /app0/build inválido ou link quebrado: "+direct);
        }else if(nestedPresent){
            addMissing(direct,nested,counts);
            mapping="root+dvdroot_ps4";
        }

        Path main=direct.resolve("ps4").resolve("main");
        Path nestedMain=nested.resolve("ps4").resolve("main");
        Path common=main.resolve("common-textures.psarc");
        Path core=main.resolve("core-textures.psarc");
        Path lut=main.resolve("lut0").resolve("default-color-gray.dds");
        return "TLOU /app0 assets: mapping="+mapping
             +" nestedMain="+Files.isDirectory(nestedMain)
             +" links="+counts.linked+" privateDirs="+counts.detached
             +" main="+Files.isDirectory(main)
             +" common-textures.psarc="+Files.isRegularFile(common)
             +" core-textures.psarc="+Files.isRegularFile(core)
             +" default-color-gray.dds="+Files.isRegularFile(lut);
    }
}
