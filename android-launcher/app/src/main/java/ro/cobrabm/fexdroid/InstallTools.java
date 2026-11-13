package ro.cobrabm.fexdroid;

import android.content.Context;
import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.function.Consumer;
import java.util.zip.*;

/** Bionic utilities run off the UI thread. No game data is included in tools.zip. */
final class InstallTools {
    private final Context context;
    private final File root;
    InstallTools(Context context) { this.context=context; root=new File(context.getFilesDir(),"install-tools-2"); }

    synchronized void install() throws Exception {
        String version=String.valueOf(context.getPackageManager().getPackageInfo(context.getPackageName(),0).lastUpdateTime);
        File ready=new File(root,".ready");
        if (ready.isFile() && new String(Files.readAllBytes(ready.toPath()),java.nio.charset.StandardCharsets.UTF_8).equals(version)) return;
        delete(root);
        if (!root.mkdirs()) throw new IOException("Não foi possível instalar as ferramentas");
        try (ZipInputStream zip=new ZipInputStream(context.getAssets().open("tools.zip"))) {
            ZipEntry entry;
            while ((entry=zip.getNextEntry())!=null) {
                File file=new File(root,entry.getName()).getCanonicalFile();
                if (!file.toPath().startsWith(root.getCanonicalFile().toPath())) throw new IOException("Caminho inválido no pacote");
                if (entry.isDirectory()) { file.mkdirs(); continue; }
                File parent=file.getParentFile();
                if (parent==null || (!parent.isDirectory()&&!parent.mkdirs())) throw new IOException("Falha ao criar pasta");
                try (OutputStream out=new FileOutputStream(file)) { byte[] buffer=new byte[65536]; int count; while((count=zip.read(buffer))!=-1) out.write(buffer,0,count); }
            }
        }
        for (String name:new String[]{"python/bin/python3.13","bin/orbis-pkg-util"})
            if (!new File(root,name).setExecutable(true,true)) throw new IOException("Falha ao ativar "+name);
        // Optional AOT tool shipped only in experimental package. It is a
        // standalone Android ARM64 executable; avoid running it as a JNI .so.
        File aotCompiler=new File(root,"bin/libbbrecomp.so");
        if(aotCompiler.isFile() && !aotCompiler.setExecutable(true,true))
            throw new IOException("Falha ao ativar compilador ARM64");
        Files.write(ready.toPath(),version.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    private String run(List<String> args, Consumer<String> progress) throws Exception {
        ProcessBuilder builder=new ProcessBuilder(args).directory(root).redirectErrorStream(true);
        Map<String,String> env=builder.environment();
        env.put("LD_LIBRARY_PATH",context.getApplicationInfo().nativeLibraryDir+":"+new File(root,"python/lib"));
        env.put("PYTHONHOME",new File(root,"python").getAbsolutePath());
        env.put("PYTHONUTF8","1"); env.put("PYTHONUNBUFFERED","1");
        env.put("TMPDIR",context.getCacheDir().getAbsolutePath());
        Process process=builder.start();
        StringBuilder tail=new StringBuilder();
        File log=new File(context.getFilesDir(),"installation.log");
        try (BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream()));
             PrintWriter output=new PrintWriter(new FileOutputStream(log,true))) {
            String line;
            while ((line=reader.readLine())!=null) {
                output.println(line);
                if (tail.length()>24000) tail.delete(0,tail.length()-12000);
                tail.append(line).append('\n');
                if (progress!=null) progress.accept(line);
            }
        }
        int code=process.waitFor();
        if (code!=0) throw new IOException("A operação falhou ("+code+").\n"+tail);
        return tail.toString();
    }

    File prepare(File game, Consumer<String> progress) throws Exception {
        return prepare(game,null,progress);
    }

    File prepare(File game,File update,Consumer<String> progress) throws Exception {
        install();
        File out=new File(context.getFilesDir(),"tlou-prepared");
        List<String> command=new ArrayList<>(Arrays.asList(
            new File(root,"python/bin/python3.13").getAbsolutePath(),
            new File(root,"source/android/device_prepare.py").getAbsolutePath(),
            "--game",game.getCanonicalPath(),"--out",out.getAbsolutePath(),
            "--options",PortSettings.patchOptions(context).getAbsolutePath()));
        if(update!=null){
            File changed=update.getCanonicalFile();
            if(!new File(changed,"sce_sys/param.sfo").isFile())
                throw new IOException("A pasta da atualização não contém sce_sys/param.sfo.");
            command.add("--update");
            command.add(changed.getAbsolutePath());
        }
        run(command,progress);
        if (!prepared(out)) throw new IOException("Preparação incompleta; consulte o log de instalação");
        syncEffectivePatches(out,progress);
        return out;
    }

    void applyPatches(File prepared,Consumer<String> progress) throws Exception {
        install();
        run(Arrays.asList(new File(root,"python/bin/python3.13").getAbsolutePath(),
            new File(root,"source/android/device_prepare.py").getAbsolutePath(),
            "--game",new File(prepared,"tlou-view").getAbsolutePath(),"--out",prepared.getAbsolutePath(),
            "--options",PortSettings.patchOptions(context).getAbsolutePath(),"--patch-only"),progress);
        syncEffectivePatches(prepared,progress);
    }

    private void syncEffectivePatches(File prepared,Consumer<String> progress) throws Exception {
        File report=new File(prepared,"patch-validation.json");
        if(!report.isFile())return;
        org.json.JSONObject profile=new org.json.JSONObject(new String(Files.readAllBytes(report.toPath()),
            java.nio.charset.StandardCharsets.UTF_8));
        if(!"01.11".equals(profile.optString("game_version")))return;
        if(!"original".equals(profile.optString("resolution")))return;
        android.content.SharedPreferences prefs=PortSettings.prefs(context);
        if(!"original".equals(prefs.getString("resolution","original"))) {
            prefs.edit().putString("resolution","original").apply();
            if(progress!=null)progress.accept(
                "Atualização 01.11: resolução Original aplicada; patches 720/540/360p ainda não validados.");
        }
    }


    /** Compiles a local, version-specific ARM64 library. It is not yet used to run TLOU. */
    File buildExperimentalAot(File prepared, Consumer<String> progress) throws Exception {
        if(!prepared(prepared))throw new IOException("Prepare o jogo antes de compilar ARM64");
        install();
        File compiler=new File(root,"bin/libbbrecomp.so");
        File bundle=new File(root,"aot/bundle");
        File frontend=new File(root,"source/android/aot/aot_local.py");
        if(!compiler.isFile() || !bundle.isDirectory() || !frontend.isFile())
            throw new IOException("Compilador AOT não incluído nesta APK experimental");
        File game=new File(prepared,"tlou-view");
        for(String name:new String[]{"eboot.bin","sce_module/libc.prx",
                                       "sce_module/libSceFios2.prx","sce_sys/param.sfo"})
            if(!new File(game,name).isFile())
                throw new IOException("Preparação incompleta: "+name);
        File boot=new File(prepared,"boot-linked.bin");
        if(!boot.isFile())throw new IOException("Imagem vinculada não encontrada");
        // Every output is private to this installed app, never in a shared game
        // folder or on GitHub. --resume allows completed shards to be reused.
        File output=new File(context.getFilesDir(),"tlou-aot-work");
        if(!output.isDirectory() && !output.mkdirs())
            throw new IOException("Sem espaço para criar pasta de compilação AOT");
        List<String> command=Arrays.asList(
            new File(root,"python/bin/python3.13").getAbsolutePath(),
            frontend.getAbsolutePath(),
            "--game",game.getCanonicalPath(),
            "--boot",boot.getCanonicalPath(),
            "--out",output.getCanonicalPath(),
            "--compiler",compiler.getCanonicalPath(),
            "--bundle-dir",bundle.getCanonicalPath(),
            "--jobs","3","--shards","64",
            "--misses",new File(context.getFilesDir(),"tlou/aot-misses.log").getAbsolutePath());
        run(command,progress);
        File so=new File(output,"aot/libtlouaot.so");
        File metadata=new File(output,"aot/libtlouaot.json");
        if(!so.isFile() || !metadata.isFile())
            throw new IOException("Compilador não gerou biblioteca e metadados AOT");
        return so;
    }

    File extract(File pkg, File parent, Consumer<String> progress) throws Exception {
        return extractPackage(pkg,parent,progress,false);
    }

    File extractUpdate(File pkg, File parent, Consumer<String> progress) throws Exception {
        return extractPackage(pkg,parent,progress,true);
    }

    private File extractPackage(File pkg, File parent, Consumer<String> progress,boolean update) throws Exception {
        install();
        try (DataInputStream input=new DataInputStream(new FileInputStream(pkg))) {
            if (input.readInt()!=0x7f434e54) throw new IOException("Selecione um PKG de PS4 válido");
        }
        String helper=new File(root,"bin/orbis-pkg-util").getAbsolutePath();
        progress.accept("Lendo informações do PKG…");
        String info=run(Arrays.asList(helper,"info",pkg.getCanonicalPath()),null);
        java.util.regex.Matcher title=java.util.regex.Pattern.compile("(?im)^\\s*Title ID:\\s*(CUSA[0-9]{5})\\s*$").matcher(info);
        if (!title.find())
            throw new IOException("Não foi possível identificar o CUSA do PKG PS4");
        final String cusa=title.group(1);
        if (!Arrays.asList("CUSA00552","CUSA00556","CUSA00557","CUSA00559").contains(cusa))
            throw new IOException("Título PS4 não suportado: "+cusa);
        File destination=new File(parent,cusa).getCanonicalFile();
        if (destination.exists()) throw new IOException("A pasta "+destination+" já existe. Se o jogo já está extraído, use Escolher pasta do jogo.");
        if (pkg.getCanonicalFile().toPath().startsWith(destination.toPath())) throw new IOException("PKG dentro do destino");
        if (!parent.canWrite() || !destination.mkdir()) throw new IOException("Sem acesso de escrita ao destino");
        try {
            progress.accept("Extraindo PKG para "+destination+"…");
            run(Arrays.asList(helper,"extract",pkg.getCanonicalPath(),"--output",destination.getAbsolutePath(),"--quiet"),progress);
            if (!new File(destination,"sce_sys/param.sfo").isFile() ||
                (!update && !new File(destination,"eboot.bin").isFile()))
                throw new IOException(update ? "PKG de atualização sem sce_sys/param.sfo"
                                             : "PKG extraído sem eboot.bin ou param.sfo");
            return destination;
        } catch (Exception failure) { delete(destination); throw failure; }
    }

    static boolean prepared(File dir) {
        return new File(dir,"boot-linked.bin").isFile() && new File(dir,"content.bin").isFile() && new File(dir,"tlou-view").isDirectory();
    }
    static void delete(File file) throws IOException {
        if (Files.isSymbolicLink(file.toPath())) { Files.deleteIfExists(file.toPath()); return; }
        File[] children=file.listFiles();
        if (children!=null) for (File child:children) delete(child);
        Files.deleteIfExists(file.toPath());
    }
}
