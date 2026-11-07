package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=30, application=android.app.Application.class, qualifiers="mdpi")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
public class PresentationTest {
    @Test public void fullHudUsesOneLineAndOriginalReferenceColors() {
        SpannableString text=HudPresentation.text("full",60,42,23,true,true);
        assertEquals("FPS 60 · GPU 42% · CPU 23%",text.toString());
        ForegroundColorSpan[] colors=text.getSpans(0,text.length(),ForegroundColorSpan.class);
        assertEquals(3,colors.length);
        assertEquals(0xff76ff03,colors[0].getForegroundColor());
        assertEquals(0xffe040fb,colors[1].getForegroundColor());
        assertEquals(0xff00e5ff,colors[2].getForegroundColor());
        assertEquals("FPS 0 · GPU N/A · CPU N/A",HudPresentation.text("full",0,Double.NaN,Double.NaN,true,true).toString());
        assertEquals("FPS 60",HudPresentation.text("minimal",60,42,23,true,true).toString());
    }
    private String labels(View view) {
        StringBuilder text=new StringBuilder();
        if(view instanceof TextView)text.append(((TextView)view).getText()).append('\n');
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++)text.append(labels(((ViewGroup)view).getChildAt(i)));
        return text.toString();
    }
    private void render(View view,int width,String name) throws Exception {
        view.measure(View.MeasureSpec.makeMeasureSpec(width,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(0,View.MeasureSpec.UNSPECIFIED));
        int height=view.getMeasuredHeight();assertTrue(height>0);view.layout(0,0,width,height);
        Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(bitmap);canvas.drawColor(0xff101613);view.draw(canvas);
        File out=new File("build/reports/ui",name);out.getParentFile().mkdirs();
        try(FileOutputStream stream=new FileOutputStream(out)){assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG,100,stream));}
    }
    @Test public void aboutMatchesContactProjectCardsWithoutResearchLabels() throws Exception {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()) {
            Activity a=controller.get();PortSettings.prefs(a).edit().putString("ui_language","pt").commit();
            LinearLayout about=AboutPanel.create(a,()->{},()->{});a.setContentView(about);
            String text=labels(about);
            assertTrue(text.contains("@daedalusemulator"));assertTrue(text.contains("ravencaio/tlou-remastered-pc"));
            assertTrue(text.contains("Bugs e sugestões"));assertFalse(text.contains("CUSA00552"));
            assertFalse(text.toLowerCase().contains("log 32"));assertFalse(text.toLowerCase().contains("readback"));
            render(about,411,"about-pt.png");
            PortSettings.prefs(a).edit().putString("ui_language","en").commit();
            render(AboutPanel.create(a,()->{},()->{}),411,"about-en.png");
        }
    }
    @Test public void renderActualHudWidgetAndCheckCompactSize() throws Exception {
        try(var controller=Robolectric.buildActivity(Activity.class).setup()) {
            Activity a=controller.get();TextView hud=new TextView(a);HudPresentation.style(hud,PortSettings.prefs(a));
            hud.setText(HudPresentation.text("full",60,42,23,true,true));
            hud.measure(View.MeasureSpec.makeMeasureSpec(411,View.MeasureSpec.AT_MOST),View.MeasureSpec.makeMeasureSpec(200,View.MeasureSpec.AT_MOST));
            assertTrue(hud.getMeasuredWidth()<300);assertTrue(hud.getMeasuredHeight()<45);
            assertEquals(1,hud.getMaxLines());
            render(hud,hud.getMeasuredWidth(),"hud-reference.png");
        }
    }
}
