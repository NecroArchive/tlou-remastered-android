package ro.cobrabm.fexdroid;

import org.junit.Test;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

/** Regression: automatically migrate 1.00 only when APP_VER is actually 01.00. */
public final class Ps4SfoVersionTest {
    private static byte[] version(String version) {
        byte[] key=new byte[]{'A','P','P','_','V','E','R',0};
        byte[] value=new byte[version.length()+1];
        byte[] versionBytes=version.getBytes(StandardCharsets.UTF_8);
        System.arraycopy(versionBytes,0,value,0,versionBytes.length);
        int keyTable=36, dataTable=keyTable+key.length;
        ByteBuffer data=ByteBuffer.allocate(dataTable+value.length)
            .order(ByteOrder.LITTLE_ENDIAN);
        data.put(new byte[]{0,'P','S','F'});
        data.putInt(0x00000101);
        data.putInt(keyTable);
        data.putInt(dataTable);
        data.putInt(1);
        data.putShort((short)0);
        data.putShort((short)0x0204);
        data.putInt(value.length);
        data.putInt(value.length);
        data.putInt(0);
        data.position(keyTable);
        data.put(key);
        data.put(value);
        return data.array();
    }

    @Test public void verifiedRegional111OverridesStale100Metadata() {
        String hash="31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7";
        assertEquals("1.11",Ps4SfoVersion.installedVersion("1.00","CUSA00557",hash));
        assertEquals("1.00",Ps4SfoVersion.installedVersion("1.00","CUSA00557","unknown"));
        assertEquals("1.00",Ps4SfoVersion.installedVersion("1.00","CUSA99999",hash));
    }

    @Test public void readsReal100Base() throws Exception {
        assertEquals("1.00",Ps4SfoVersion.fromBytes(version("01.00")));
    }

    @Test public void readsAlreadyUpdated111Folder() throws Exception {
        assertEquals("1.11",Ps4SfoVersion.fromBytes(version("01.11")));
    }

    @Test public void refusesUnsupportedVersionInsteadOfMigrating() throws Exception {
        try {
            Ps4SfoVersion.fromBytes(version("01.02"));
            fail("Unknown PS4 version accepted");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("não suportada"));
        }
    }

    @Test public void rejectsInvalidAndTruncatedSfo() throws Exception {
        byte[] valid=version("01.11");
        valid[0]=1;
        try {
            Ps4SfoVersion.fromBytes(valid);
            fail("Corrupt SFO accepted");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("inválido"));
        }
        try {
            Ps4SfoVersion.fromBytes(new byte[]{0,'P','S','F'});
            fail("Truncated SFO accepted");
        } catch(java.io.IOException expected) {
            assertTrue(expected.getMessage().contains("inválido"));
        }
    }
}
