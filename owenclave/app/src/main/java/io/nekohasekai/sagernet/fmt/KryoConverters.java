package io.nekohasekai.sagernet.fmt;

import androidx.room.TypeConverter;
import com.esotericsoftware.kryo.KryoException;
import com.esotericsoftware.kryo.io.ByteBufferInput;
import com.esotericsoftware.kryo.io.ByteBufferOutput;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean;
import io.nekohasekai.sagernet.fmt.olcrtc.OLCRTCBean;
import io.nekohasekai.sagernet.ktx.KryosKt;
import io.nekohasekai.sagernet.ktx.Logs;

public class KryoConverters {
    private static final byte[] NULL = new byte[0];

    @TypeConverter
    public static byte[] serialize(Serializable bean) {
        if (bean == null) return NULL;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ByteBufferOutput buffer = KryosKt.byteBuffer(out);
        bean.serializeToBuffer(buffer);
        buffer.flush();
        buffer.close();
        return out.toByteArray();
    }

    public static <T extends Serializable> T deserialize(T bean, byte[] bytes) {
        if (bytes == null) return bean;
        ByteBufferInput buffer = KryosKt.byteBuffer(new ByteArrayInputStream(bytes));
        try {
            bean.deserializeFromBuffer(buffer);
        } catch (KryoException e) {
            if (e instanceof DnsttBean.UnsupportedVersionException) throw e;
            Logs.INSTANCE.w(e);
        }
        bean.initializeDefaultValues();
        return bean;
    }

    @TypeConverter
    public static DnsttBean dnsttDeserialize(byte[] bytes) {
        if (bytes == null || bytes.length == 0) return null;
        return deserialize(new DnsttBean(), bytes);
    }

    @TypeConverter
    public static OLCRTCBean olcrtcDeserialize(byte[] bytes) {
        if (bytes == null || bytes.length == 0) return null;
        return deserialize(new OLCRTCBean(), bytes);
    }

}
