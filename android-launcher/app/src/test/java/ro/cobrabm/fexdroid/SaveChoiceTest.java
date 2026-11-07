package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=30,application=android.app.Application.class,qualifiers="w960dp-h480dp-mdpi")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
public class SaveChoiceTest {
    private GameChoiceProtocol.Request nativeRequest(){
        GameChoiceProtocol.Request r=GameChoiceProtocol.read(new File("build/native-save-choice.bin"));
        assertNotNull("Actual native serializer fixture must be generated before UI tests",r);return r;
    }
    @Test public void nativeMetadataKeepsSlotChapterDescriptionAndTimestamp(){
        var r=nativeRequest();assertEquals("Load game",r.title);assertEquals(2,r.entries.length);
        assertEquals("SAVE0001",r.entries[0].directory);assertEquals("The Outskirts",r.entries[0].subtitle);
        assertEquals("The Last of Us",r.entries[0].title);assertEquals("Actual checkpoint",r.entries[0].detail);
        assertEquals(1700000000L,r.entries[0].timestamp);assertEquals(42,r.entries[0].userParam);
        assertEquals("SAVE0002",r.entries[1].directory);assertEquals("Bill's Town",r.entries[1].heading());
    }
    @Test public void replyUsesExactSlotIndexAndRejectsStaleOrInvalidReplies() throws Exception {
        File dir=Files.createTempDirectory("save-choice-test-").toFile(),req=new File(dir,"choice.request"),reply=new File(dir,"choice.reply");
        byte[] nativeBytes=Files.readAllBytes(new File("build/native-save-choice.bin").toPath());Files.write(req.toPath(),nativeBytes);
        var request=GameChoiceProtocol.read(req);assertNotNull(request);
        assertTrue(GameChoiceProtocol.reply(req,reply,request,1));
        ByteBuffer response=ByteBuffer.wrap(Files.readAllBytes(reply.toPath())).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(request.id,response.getLong(8));assertEquals(1,response.getInt(16));
        assertFalse(GameChoiceProtocol.reply(req,reply,request,2));
        ByteBuffer.wrap(nativeBytes).order(ByteOrder.LITTLE_ENDIAN).putLong(8,request.id+1);Files.write(req.toPath(),nativeBytes);
        assertFalse(GameChoiceProtocol.reply(req,reply,request,0));
        Files.write(req.toPath(),new byte[12]);assertNull(GameChoiceProtocol.read(req));
        req.delete();reply.delete();dir.delete();
    }
    private View find(View view,String text){
        if(view instanceof TextView && text.contentEquals(((TextView)view).getText()))return view;
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++){View found=find(((ViewGroup)view).getChildAt(i),text);if(found!=null)return found;}
        return null;
    }
    private String labels(View view){
        String value=view instanceof TextView?((TextView)view).getText().toString()+"\n":"";
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++)value+=labels(((ViewGroup)view).getChildAt(i));return value;
    }
    @Test public void saveCardsShowRealMetadataAndConfirmTheChosenCard() throws Exception {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()){
            Activity a=controller.get();PortSettings.prefs(a).edit().putString("ui_language","pt").commit();
            int[] chosen={-99};SaveChoicePanel panel=new SaveChoicePanel(a,nativeRequest(),null,index->chosen[0]=index);panel.show();
            View root=panel.getWindow().getDecorView();String text=labels(root);
            assertTrue(text.contains("The Outskirts"));assertTrue(text.contains("Actual checkpoint"));assertTrue(text.contains("SAVE0001"));
            assertTrue(text.contains("Bill's Town"));assertTrue(text.contains("Sem miniatura"));assertFalse(text.contains("Autossalvamento"));
            assertEquals(-99,chosen[0]);
            View card=find(root,"Bill's Town");assertNotNull(card);
            View cardContainer=(View)card.getParent().getParent();
            assertEquals("Landscape save cards must stay compact",84,cardContainer.getLayoutParams().height);
            assertTrue("Long press keeps full metadata available",cardContainer.performLongClick());
            assertTrue(panel.isShowing());
            cardContainer.performClick();
            assertEquals(-99,chosen[0]);View load=find(root,"Carregar");assertTrue(load instanceof Button);load.performClick();
            assertEquals(1,chosen[0]);assertFalse(panel.isShowing());
        }
    }
    @Test public void cancelReturnsToTheGameAndNativeOrderingIsPreserved(){
        try(var controller=Robolectric.buildActivity(Activity.class).setup()){
            int[] chosen={-99};SaveChoicePanel panel=new SaveChoicePanel(controller.get(),nativeRequest(),null,index->chosen[0]=index);panel.show();panel.cancel();org.robolectric.Shadows.shadowOf(android.os.Looper.getMainLooper()).idle();assertEquals(-2,chosen[0]);
            chosen[0]=-99;panel=new SaveChoicePanel(controller.get(),nativeRequest(),null,index->chosen[0]=index);panel.show();
            panel.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_DPAD_DOWN));
            panel.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_A));assertEquals(1,chosen[0]);
            chosen[0]=-99;panel=new SaveChoicePanel(controller.get(),nativeRequest(),null,index->chosen[0]=index);panel.show();
            panel.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_ESCAPE));assertEquals(-2,chosen[0]);
        }
    }
    @Test public void thumbnailsComeOnlyFromTheActualSaveTree() throws Exception {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()){
            Activity a=controller.get();File state=new File(a.getFilesDir(),"tlou");
            File icon=new File(state,"user/savedata/1/CUSA00556/SAVE0001.sce_sys/icon0.png");icon.getParentFile().mkdirs();
            Bitmap image=Bitmap.createBitmap(16,9,Bitmap.Config.ARGB_8888);image.eraseColor(0xff456729);
            try(FileOutputStream stream=new FileOutputStream(icon)){image.compress(Bitmap.CompressFormat.PNG,100,stream);}
            var original=nativeRequest();var old=original.entries[0];
            var entry=new GameChoiceProtocol.Entry(old.label,old.directory,old.title,old.subtitle,old.detail,icon.getPath(),old.timestamp,old.userParam,false);
            var r=new GameChoiceProtocol.Request(original.id,original.title,new GameChoiceProtocol.Entry[]{entry,original.entries[1]},0);
            GameChoiceBridge bridge=new GameChoiceBridge(a,state,()->{},null);
            try{Bitmap[] images=bridge.loadThumbnails(r);assertNotNull(images[0]);assertEquals(0xff456729,images[0].getPixel(0,0));assertNull(images[1]);}finally{bridge.close();}
        }
    }
    @Test public void renderSaveSelector() throws Exception {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()){
            Activity a=controller.get();PortSettings.prefs(a).edit().putString("ui_language","pt").commit();
            SaveChoicePanel panel=new SaveChoicePanel(a,nativeRequest(),null,index->{});panel.show();View root=panel.getWindow().getDecorView();
            root.measure(View.MeasureSpec.makeMeasureSpec(800,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(432,View.MeasureSpec.EXACTLY));root.layout(0,0,800,432);
            Bitmap image=Bitmap.createBitmap(800,432,Bitmap.Config.ARGB_8888);root.draw(new Canvas(image));File target=new File("build/reports/ui/save-selector.png");target.getParentFile().mkdirs();
            try(FileOutputStream out=new FileOutputStream(target)){assertTrue(image.compress(Bitmap.CompressFormat.PNG,100,out));}panel.dismiss();
        }
    }
}
