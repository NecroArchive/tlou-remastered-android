package ro.cobrabm.fexdroid;

import org.junit.Test;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

/** Regression: automatically migrate 1.00 only when APP_VER is actually 01.00. */
public final class Ps4SfoVersionTest {
    private static byte[] version(String version) {
        byte[] key="APP_VER\\0".replace("\\0","\u0000")
            .getBytes(StandardCharsets.UTF_8);
        byte[] value=(version+"\u0000").getBytes(StandardCharsets.UTF_8);
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
