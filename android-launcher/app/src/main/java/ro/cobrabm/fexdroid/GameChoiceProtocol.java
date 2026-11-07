package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;

/** Bounded save metadata shared with android_choice_bridge.h; selection never writes saves. */
final class GameChoiceProtocol {
    static final class Entry {
        final String label,directory,title,subtitle,detail,icon;
        final long timestamp;final int userParam;final boolean isNew;
        Entry(String label,String directory,String title,String subtitle,String detail,String icon,long timestamp,int userParam,boolean isNew){
            this.label=label;this.directory=directory;this.title=title;this.subtitle=subtitle;this.detail=detail;this.icon=icon;this.timestamp=timestamp;this.userParam=userParam;this.isNew=isNew;
        }
        String heading(){return !subtitle.isEmpty()?subtitle:!title.isEmpty()?title:!directory.isEmpty()?directory:label;}
    }
    static final class Request {
        final long id;final String title;final Entry[] entries;final int focus;
        Request(long id,String title,Entry[] entries,int focus){this.id=id;this.title=title;this.entries=entries;this.focus=focus;}
    }
    private static String string(ByteBuffer b,int limit) throws IOException {
        int size=b.getInt();if(size<0 || size>limit || size>b.remaining())throw new IOException("Invalid choice text size");
        byte[] data=new byte[size];b.get(data);return new String(data,StandardCharsets.UTF_8);
    }
    static Request read(File file) {
        if(!file.isFile() || file.length()>1048576 || file.length()<28)return null;
        try {
            ByteBuffer b=ByteBuffer.wrap(Files.readAllBytes(file.toPath())).order(ByteOrder.LITTLE_ENDIAN);
            byte[] magic=new byte[8];b.get(magic);
            if(!Arrays.equals(magic,"TLCHOIC2".getBytes(StandardCharsets.US_ASCII)))return null;
            long id=b.getLong();int count=b.getInt(),focus=b.getInt();
            if(count<1 || count>256 || focus<0 || focus>=count)return null;
            String title=string(b,1024);Entry[] entries=new Entry[count];
            for(int i=0;i<count;i++){
                String label=string(b,2048),directory=string(b,32),name=string(b,128),subtitle=string(b,128),detail=string(b,1024),icon=string(b,1024);
                long timestamp=b.getLong();int userParam=b.getInt(),flags=b.getInt();if((flags&~1)!=0)return null;
                entries[i]=new Entry(label,directory,name,subtitle,detail,icon,timestamp,userParam,flags==1);
            }
            if(b.hasRemaining())return null;
            return new Request(id,title,entries,focus);
        }catch(Exception e){return null;}
    }
    static boolean reply(File request,File response,Request original,int index) throws IOException {
        if(index!=-2 && (index<0 || index>=original.entries.length))return false;
        Request current=read(request);if(current==null || current.id!=original.id)return false;
        ByteBuffer b=ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN);
        b.put("TLCHRPL1".getBytes(StandardCharsets.US_ASCII)).putLong(original.id).putInt(index);
        File temp=new File(response.getPath()+".tmp");Files.write(temp.toPath(),b.array());
        Files.move(temp.toPath(),response.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
        return true;
    }
}
