package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.io.File;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Shows the game's native save-selection request as an Android modal list. */
final class GameChoiceBridge {
    private final Activity activity;
    private final File request,response;
    private final Runnable releaseInput;
    private final java.util.function.Function<KeyEvent,String> buttonName;
    private final ScheduledExecutorService worker=Executors.newSingleThreadScheduledExecutor(r->{Thread t=new Thread(r,"tlou-game-dialog");t.setDaemon(true);return t;});
    private volatile boolean closed;
    private volatile GameChoiceProtocol.Request current;
    private Dialog dialog;
    private final java.util.concurrent.ExecutorService imageWorker=Executors.newSingleThreadExecutor(r->{Thread t=new Thread(r,"tlou-save-thumbnails");t.setDaemon(true);return t;});
    private long preparedId;
    private long shownId;
    GameChoiceBridge(Activity activity,File state,Runnable releaseInput,java.util.function.Function<KeyEvent,String> buttonName){
        this.buttonName=buttonName;
        this.activity=activity;this.request=new File(state,"choice.request");this.response=new File(state,"choice.reply");this.releaseInput=releaseInput;
    }
    void start() throws java.io.IOException {
        if(closed)return;
        java.nio.file.Files.deleteIfExists(request.toPath());java.nio.file.Files.deleteIfExists(response.toPath());
        worker.scheduleWithFixedDelay(this::poll,0,200,TimeUnit.MILLISECONDS);
    }
    private void poll(){
        if(closed)return;
        GameChoiceProtocol.Request next=GameChoiceProtocol.read(request);current=next;

        activity.runOnUiThread(()->{
            if(closed || activity.isFinishing() || activity.isDestroyed())return;
            if(next==null){if(dialog!=null){dialog.dismiss();dialog=null;}return;}
            if(current==null || current.id!=next.id || (dialog!=null && shownId==next.id))return;
            if(dialog!=null)dialog.dismiss();shownId=next.id;show(next,null);
        });
        if(next!=null && preparedId!=next.id){
            preparedId=next.id;
            try{imageWorker.execute(()->{
                Bitmap[] pictures=loadThumbnails(next);
                activity.runOnUiThread(()->{
                    if(!closed && current!=null && current.id==next.id && shownId==next.id && dialog instanceof SaveChoicePanel)
                        ((SaveChoicePanel)dialog).updateThumbnails(pictures);
                });
            });}catch(java.util.concurrent.RejectedExecutionException ignored){}
        }
    }
    private void respond(GameChoiceProtocol.Request next,int index){
        if(closed)return;
        worker.execute(()->{try{GameChoiceProtocol.reply(request,response,next,index);}catch(Exception e){RunLog.failure(activity,"Resposta do seletor de saves",e);}});
    }
    private void show(GameChoiceProtocol.Request next,Bitmap[] pictures){
        releaseInput.run();
        dialog=new SaveChoicePanel(activity,next,pictures,index->respond(next,index),buttonName);dialog.show();
    }
    Bitmap[] loadThumbnails(GameChoiceProtocol.Request next){
        Bitmap[] images=new Bitmap[next.entries.length];
        try {
            String root=new File(activity.getFilesDir(),"tlou/user/savedata").getCanonicalPath()+File.separator;
            for(int i=0;i<images.length;i++)try {
                if(closed)break;
                File icon=new File(next.entries[i].icon);
                if(next.entries[i].icon.isEmpty() || !icon.getCanonicalPath().startsWith(root) || !icon.isFile() || icon.length()>4L*1024*1024)continue;
                BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeFile(icon.getPath(),options);
                if(options.outWidth<1 || options.outHeight<1 || options.outWidth>8192 || options.outHeight>8192)continue;
                options.inSampleSize=1;while(options.outWidth/options.inSampleSize>320 || options.outHeight/options.inSampleSize>180)options.inSampleSize*=2;
                options.inJustDecodeBounds=false;images[i]=BitmapFactory.decodeFile(icon.getPath(),options);
            }catch(Exception|OutOfMemoryError ignored){}
        }catch(Exception ignored){}
        return images;
    }
    void close(){
        if(closed)return;closed=true;worker.shutdownNow();imageWorker.shutdownNow();
        activity.runOnUiThread(()->{if(dialog!=null){dialog.dismiss();dialog=null;}});
    }
}
