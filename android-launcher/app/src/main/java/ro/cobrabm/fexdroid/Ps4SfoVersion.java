package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Read APP_VER from the real PS4 param.sfo; never infer the installed
 * game version from stale launcher preferences or an old preparation marker. */
final class Ps4SfoVersion {
    private Ps4SfoVersion() {}

    static String fromFolder(File game) throws IOException {
        return fromBytes(Files.readAllBytes(new File(game,"sce_sys/param.sfo").toPath()));
    }

    // Only a verified executable may override stale installation metadata.
    // Keep fromFolder() as the raw SFO reader for update transactions.
    static String fromInstalledFolder(File game) throws Exception {
        String declared=fromFolder(game);
        if(!"1.00".equals(declared) || !"CUSA00557".equals(titleId(game)))
            return declared;
        File boot=new File(game,"eboot.bin");
        if(!boot.isFile())return declared;
        java.security.MessageDigest sha=java.security.MessageDigest.getInstance("SHA-256");
        try(java.io.InputStream stream=new java.io.BufferedInputStream(
                new java.io.FileInputStream(boot))) {
            byte[] buffer=new byte[1024*1024];
            int count;
            while((count=stream.read(buffer))!=-1)sha.update(buffer,0,count);
        }
        StringBuilder digest=new StringBuilder(64);
        for(byte value:sha.digest())
            digest.append(String.format(java.util.Locale.ROOT,"%02x",value&255));
        return installedVersion(declared,"CUSA00557",digest.toString());
    }

    static String installedVersion(String declared,String title,String digest) {
        if("1.00".equals(declared) && "CUSA00557".equals(title) &&
           ("31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7".equals(digest) ||
            "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e".equals(digest)))
            return "1.11";
        return declared;
    }

    static String titleId(File game) throws IOException {
        String value=fieldFromBytes(
            Files.readAllBytes(new File(game,"sce_sys/param.sfo").toPath()),"TITLE_ID");
        if(!value.matches("CUSA[0-9]{5}"))
            throw new IOException("TITLE_ID inválido em param.sfo: "+value);
        return value;
    }

    static String fromBytes(byte[] raw) throws IOException {
        String version=fieldFromBytes(raw,"APP_VER");
        if("01.00".equals(version) || "1.00".equals(version))return "1.00";
        if("01.11".equals(version) || "1.11".equals(version))return "1.11";
        throw new IOException("Versão do jogo não suportada: "+version);
    }

    private static String fieldFromBytes(byte[] raw,String wanted) throws IOException {
        if(raw.length<20 || raw.length>1024*1024 ||
           raw[0]!=0 || raw[1]!='P' || raw[2]!='S' || raw[3]!='F')
            throw new IOException("param.sfo inválido ou ausente");
        ByteBuffer data=ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
        long keys=Integer.toUnsignedLong(data.getInt(8));
        long values=Integer.toUnsignedLong(data.getInt(12));
        long count=Integer.toUnsignedLong(data.getInt(16));
        if(count>4096 || 20+count*16>raw.length ||
           keys>=raw.length || values>raw.length)
            throw new IOException("Índice inválido em param.sfo");
        for(int i=0;i<count;i++) {
            int entry=20+i*16;
            int keyOffset=Short.toUnsignedInt(data.getShort(entry));
            int length=data.getInt(entry+4);
            long valueOffset=Integer.toUnsignedLong(data.getInt(entry+12));
            long keyAt=keys+keyOffset, valueAt=values+valueOffset;
            if(keyAt<0 || keyAt>=values || valueAt>raw.length ||
               length<0 || valueAt+length>raw.length)
                throw new IOException("Entrada inválida em param.sfo");
            int pos=(int)keyAt;
            while(pos<values && raw[pos]!=0)pos++;
            if(pos>=values)throw new IOException("Chave não terminada em param.sfo");
            String name=new String(raw,(int)keyAt,pos-(int)keyAt,StandardCharsets.UTF_8);
            if(!wanted.equals(name))continue;
            int stop=(int)valueAt, limit=stop+length;
            while(stop<limit && raw[stop]!=0)stop++;
            return new String(raw,(int)valueAt,stop-(int)valueAt,StandardCharsets.UTF_8).trim();
        }
        throw new IOException(wanted+" ausente em param.sfo");
    }
}
