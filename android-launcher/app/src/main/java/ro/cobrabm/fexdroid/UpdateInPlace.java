package ro.cobrabm.fexdroid;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/** Explicit, transactional physical installation of a PS4 update.
 * The update source remains untouched. Never run automatically on launcher start.
 * A journal inside the game directory lets a later startup roll back an interrupted
 * operation before any files are prepared or executed.
 */
final class UpdateInPlace {
    private UpdateInPlace() {}
    private static final String TXN=".tlou-update-transaction";
    private static final String TMP=".tlou-update-staging";

    static final class Transaction implements AutoCloseable {
        private final Path game,transaction;
        private boolean finished;
        Transaction(Path game,Path transaction){this.game=game;this.transaction=transaction;}
        void commit() throws IOException {
            if(finished)return;
            Files.write(transaction.resolve("complete"),new byte[]{1},StandardOpenOption.CREATE_NEW);
            finished=true;
            deleteTree(transaction);
        }
        @Override public void close() throws IOException {
            if(!finished){rollback(game,transaction);finished=true;}
        }
    }
    private static void deleteTree(Path path) throws IOException {
        if(!Files.exists(path,LinkOption.NOFOLLOW_LINKS))return;
        try(Stream<Path> tree=Files.walk(path)){
            List<Path> entries=tree.sorted(Comparator.reverseOrder()).collect(Collectors.toList());
            for(Path part:entries)Files.deleteIfExists(part);
        }
    }
    private static void move(Path source,Path dest) throws IOException {
        try{Files.move(source,dest,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
        catch(AtomicMoveNotSupportedException ignored){Files.move(source,dest,StandardCopyOption.REPLACE_EXISTING);}
    }
    private static void safeParents(Path root,Path relative) throws IOException {
        Path cursor=root;
        for(int i=0;i<relative.getNameCount()-1;i++){
            cursor=cursor.resolve(relative.getName(i));
            if(Files.isSymbolicLink(cursor))throw new IOException("Link na pasta de destino: "+cursor);
        }
        if(Files.isSymbolicLink(root.resolve(relative)))
            throw new IOException("Link de arquivo não permitido: "+root.resolve(relative));
    }
    private static String manifestLine(Path relative,boolean wasPresent){
        String encoded=Base64.getUrlEncoder().withoutPadding().encodeToString(relative.toString().getBytes(StandardCharsets.UTF_8));
        return (wasPresent?"1 ":"0 ")+encoded+"\n";
    }
    private static List<Plan> readPlan(Path transaction) throws IOException {
        Path manifest=transaction.resolve("manifest.txt");
        if(!Files.isRegularFile(manifest))throw new IOException("Journal de atualização ausente: "+manifest);
        List<Plan> entries=new ArrayList<>();
        for(String line:Files.readAllLines(manifest,StandardCharsets.US_ASCII)){
            if(line.length()<3 || line.charAt(1)!=' ')throw new IOException("Journal inválido");
            Path relative=Paths.get(new String(Base64.getUrlDecoder().decode(line.substring(2)),StandardCharsets.UTF_8));
            if(relative.isAbsolute()||relative.normalize().startsWith("..")||!relative.normalize().equals(relative))
                throw new IOException("Caminho inseguro no journal: "+relative);
            entries.add(new Plan(relative,line.charAt(0)=='1'));
        }
        return entries;
    }
    private static final class Plan {
        final Path relative; final boolean wasPresent;
        Plan(Path relative,boolean wasPresent){this.relative=relative;this.wasPresent=wasPresent;}
    }
    private static void rollback(Path game,Path transaction) throws IOException {
        List<Plan> plan=readPlan(transaction);
        IOException failure=null;
        for(int i=plan.size()-1;i>=0;i--){
            Plan item=plan.get(i);
            Path target=game.resolve(item.relative),backup=transaction.resolve("backup").resolve(item.relative);
            try{
                Path temp=target.resolveSibling(target.getFileName()+TMP);
                Files.deleteIfExists(temp);
                if(Files.exists(backup,LinkOption.NOFOLLOW_LINKS)){
                    Files.deleteIfExists(target);
                    Files.createDirectories(target.getParent());
                    move(backup,target);
                }else if(!item.wasPresent)Files.deleteIfExists(target);
            }catch(IOException problem){if(failure==null)failure=problem;else failure.addSuppressed(problem);}
        }
        if(failure!=null)throw failure;
        deleteTree(transaction);
    }
    static void recover(File folder) throws IOException {
        Path game=folder.getCanonicalFile().toPath();
        Path transaction=game.resolve(TXN);
        if(!Files.exists(transaction,LinkOption.NOFOLLOW_LINKS))return;
        if(Files.isRegularFile(transaction.resolve("complete")))deleteTree(transaction);
        else rollback(game,transaction);
    }
    static Transaction apply(File base,File extractedUpdate,Consumer<String> progress) throws IOException {
        Path game=base.getCanonicalFile().toPath(),update=extractedUpdate.getCanonicalFile().toPath();
        if(game.startsWith(update)||update.startsWith(game))
            throw new IOException("Jogo e update não podem ficar um dentro do outro");
        if(!Files.isRegularFile(game.resolve("sce_sys/param.sfo")) ||
           !Files.isRegularFile(update.resolve("sce_sys/param.sfo")))
            throw new IOException("Jogo ou atualização sem sce_sys/param.sfo");
        if(!Files.isRegularFile(update.resolve("eboot.bin")))
            throw new IOException("Update não contém o eboot.bin; instalação física recusada");
        recover(game.toFile());
        Path transaction=game.resolve(TXN);
        List<Plan> plan=new ArrayList<>();
        try(Stream<Path> walk=Files.walk(update)){
            List<Path> input=walk.filter(p->!p.equals(update)).sorted().collect(Collectors.toList());
            for(Path path:input){
                if(Files.isSymbolicLink(path))throw new IOException("Update contém link: "+path);
                if(Files.isDirectory(path))continue;
                if(!Files.isRegularFile(path))throw new IOException("Update contém arquivo inválido: "+path);
                Path relative=update.relativize(path);
                if(relative.getName(0).toString().startsWith(".tlou-update"))
                    throw new IOException("Arquivo reservado no update: "+relative);
                safeParents(game,relative);
                Path dest=game.resolve(relative);
                if(Files.exists(dest,LinkOption.NOFOLLOW_LINKS)&&!Files.isRegularFile(dest))
                    throw new IOException("Update substituiria diretório ou dispositivo: "+dest);
                plan.add(new Plan(relative,Files.isRegularFile(dest)));
            }
        }
        if(plan.isEmpty())throw new IOException("Atualização sem arquivos");
        if(Files.exists(transaction,LinkOption.NOFOLLOW_LINKS))
            throw new IOException("Transação de update anterior ainda existe: "+transaction);
        Files.createDirectory(transaction);
        try{
            StringBuilder manifest=new StringBuilder();
            for(Plan item:plan)manifest.append(manifestLine(item.relative,item.wasPresent));
            Path journal=transaction.resolve("manifest.txt");
            try(FileOutputStream output=new FileOutputStream(journal.toFile())){
                output.write(manifest.toString().getBytes(StandardCharsets.US_ASCII));output.getFD().sync();
            }
            int completed=0;
            for(Plan item:plan){
                Path source=update.resolve(item.relative),target=game.resolve(item.relative),backup=transaction.resolve("backup").resolve(item.relative);
                safeParents(game,item.relative);
                Files.createDirectories(target.getParent());
                Path temporary=target.resolveSibling(target.getFileName()+TMP);
                if(Files.exists(temporary,LinkOption.NOFOLLOW_LINKS))
                    throw new IOException("Arquivo temporário pré-existente: "+temporary);
                try(InputStream in=new BufferedInputStream(Files.newInputStream(source));
                    FileOutputStream output=new FileOutputStream(temporary.toFile())){
                    byte[] bytes=new byte[256*1024];int n;
                    while((n=in.read(bytes))!=-1)output.write(bytes,0,n);
                    output.getFD().sync();
                }
                if(item.wasPresent){Files.createDirectories(backup.getParent());move(target,backup);}
                move(temporary,target);
                completed++;
                if(progress!=null&&(completed==plan.size()||completed%50==0))
                    progress.accept("Atualização física: "+completed+"/"+plan.size()+" arquivos copiados");
            }
            return new Transaction(game,transaction);
        }catch(IOException|RuntimeException e){
            try{rollback(game,transaction);}catch(Exception rollback){e.addSuppressed(rollback);}
            throw e;
        }
    }
}
