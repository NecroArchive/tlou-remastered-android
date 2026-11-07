package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/** A fixed exec wrapper publishes the exact child PID without restricted Java reflection. */
final class RuntimeProcess {
    static List<String> command(List<String> arguments,File pidFile) throws IOException {
        return command("/system/bin/sh",arguments,pidFile);
    }
    static List<String> command(String shell,List<String> arguments,File pidFile) throws IOException {
        if(arguments.isEmpty())throw new IOException("Empty runtime command");
        Files.deleteIfExists(pidFile.toPath());
        List<String> result=new ArrayList<>();
        result.add(shell);result.add("-c");
        result.add("umask 077; printf '%s\\n' \"$$\" > \"$1\" || exit 125; shift; exec \"$@\"");
        result.add("tlou-runtime");result.add(pidFile.getAbsolutePath());result.addAll(arguments);
        return result;
    }
    static int readPid(File file) {
        if(file==null || !file.isFile() || file.length()>32)return -1;
        try {
            String value=new String(Files.readAllBytes(file.toPath()),StandardCharsets.US_ASCII).trim();
            if(!value.matches("[0-9]+"))return -1;
            int pid=Integer.parseInt(value);return pid>0?pid:-1;
        }catch(Exception e){return -1;}
    }
}
