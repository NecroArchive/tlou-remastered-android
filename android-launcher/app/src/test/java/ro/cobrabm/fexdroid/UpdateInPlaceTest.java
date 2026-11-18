package ro.cobrabm.fexdroid;

import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.stream.*;

public final class UpdateInPlaceTest {
    private static void ck(boolean condition,String msg){if(!condition)throw new AssertionError(msg);}
    private static void file(Path path,String text)throws IOException{
        Files.createDirectories(path.getParent());
        Files.write(path,text.getBytes(StandardCharsets.UTF_8));
    }
    private static String read(Path p)throws IOException{return new String(Files.readAllBytes(p),StandardCharsets.UTF_8);}
    private static void erase(Path p)throws IOException{
        try(Stream<Path> walk=Files.walk(p)){
            for(Path x:walk.sorted(Comparator.reverseOrder()).collect(Collectors.toList()))Files.deleteIfExists(x);
        }
    }
    public static void main(String[] argv)throws Exception{
        Path root=Files.createTempDirectory("tlou-inplace-test-");
        Path game=root.resolve("base"),patch=root.resolve("update");
        try{
            file(game.resolve("sce_sys/param.sfo"),"base-sfo");
            file(game.resolve("eboot.bin"),"base-eboot");
            file(game.resolve("build/ps4/main/bin.psarc"),"base-bin");
            file(patch.resolve("sce_sys/param.sfo"),"update-sfo");
            file(patch.resolve("eboot.bin"),"update-eboot");
            file(patch.resolve("build/ps4/main/bin.psarc"),"update-bin");
            file(patch.resolve("build/ps4/main/core.psarc"),"update-core");
            try(UpdateInPlace.Transaction transaction=UpdateInPlace.apply(game.toFile(),patch.toFile(),null)){
                ck(read(game.resolve("eboot.bin")).equals("update-eboot"),"eboot not updated");
                ck(read(game.resolve("build/ps4/main/core.psarc")).equals("update-core"),"core not added");
            }
            ck(read(game.resolve("eboot.bin")).equals("base-eboot"),"close failed rollback");
            ck(read(game.resolve("build/ps4/main/bin.psarc")).equals("base-bin"),"rollback replaced file");
            ck(!Files.exists(game.resolve("build/ps4/main/core.psarc")),"rollback new file");
            ck(!Files.exists(game.resolve(".tlou-update-transaction")),"rollback journal leftover");
            // A process can die after replacing the files but before commit.
            UpdateInPlace.apply(game.toFile(),patch.toFile(),null);
            UpdateInPlace.recover(game.toFile());
            ck(read(game.resolve("eboot.bin")).equals("base-eboot"),"interrupted recovery");
            ck(!Files.exists(game.resolve("build/ps4/main/core.psarc")),"new file survived recovery");
            try(UpdateInPlace.Transaction transaction=UpdateInPlace.apply(game.toFile(),patch.toFile(),null)){
                transaction.commit();
            }
            ck(read(game.resolve("eboot.bin")).equals("update-eboot"),"commit not persisted");
            ck(read(game.resolve("build/ps4/main/bin.psarc")).equals("update-bin"),"updated archive missing");
            ck(read(game.resolve("build/ps4/main/core.psarc")).equals("update-core"),"new archive missing");
            ck(read(patch.resolve("eboot.bin")).equals("update-eboot"),"update source mutated");
            UpdateInPlace.recover(game.toFile());
            ck(read(game.resolve("eboot.bin")).equals("update-eboot"),"committed update rolled back");
            Files.delete(game.resolve("eboot.bin"));
            Files.createSymbolicLink(game.resolve("eboot.bin"),patch.resolve("eboot.bin"));
            boolean blocked=false;
            try{UpdateInPlace.apply(game.toFile(),patch.toFile(),null);}catch(IOException expected){blocked=true;}
            ck(blocked,"destination symlink not rejected");
            ck(read(patch.resolve("eboot.bin")).equals("update-eboot"),"source damaged");
            System.out.println("TLOU physical update transaction tests passed");
        }finally{erase(root);}
    }
}
