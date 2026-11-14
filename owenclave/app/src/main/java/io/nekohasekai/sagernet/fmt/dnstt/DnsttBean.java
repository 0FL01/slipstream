package io.nekohasekai.sagernet.fmt.dnstt;

import androidx.annotation.NonNull;

import com.esotericsoftware.kryo.KryoException;
import com.esotericsoftware.kryo.io.ByteBufferInput;
import com.esotericsoftware.kryo.io.ByteBufferOutput;

import org.jetbrains.annotations.NotNull;

import io.nekohasekai.sagernet.fmt.AbstractBean;
import io.nekohasekai.sagernet.fmt.KryoConverters;

public class DnsttBean extends AbstractBean {

    public static final class UnsupportedVersionException extends KryoException {
        public UnsupportedVersionException(int version) {
            super("Unsupported DNS Tunnel profile version " + version);
        }
    }

    public static final String LEGACY_DOMAIN = "t.x.ass-peak.de";

    public String token;
    public String resolver;
    public String benchmarkSnapshot;

    @Override
    public void initializeDefaultValues() {
        if (serverAddress == null || serverAddress.isEmpty()) serverAddress = LEGACY_DOMAIN;
        serverPort = 53;
        super.initializeDefaultValues();
        if (token == null) token = "";
        if (resolver == null) resolver = "";
        if (benchmarkSnapshot == null) benchmarkSnapshot = "";
    }

    @Override
    public void serialize(ByteBufferOutput output) {
        output.writeInt(2);
        output.writeString(token);
        output.writeString(resolver);
        output.writeString(benchmarkSnapshot);
        output.writeString(serverAddress);
    }

    @Override
    public void deserialize(ByteBufferInput input) {
        int version = input.readInt();
        if (version < 0 || version > 2) throw new UnsupportedVersionException(version);
        token = input.readString();
        resolver = input.readString();
        benchmarkSnapshot = version >= 1 ? input.readString() : "";
        serverAddress = version >= 2 ? input.readString() : LEGACY_DOMAIN;
    }

    @Override
    public String displayName() {
        return name != null && !name.isEmpty() ? name : "DNS Tunnel";
    }

    @Override
    public String displayAddress() {
        return resolver == null || resolver.isEmpty() ? "Automatic DNS" : resolver;
    }

    @NotNull
    @Override
    public DnsttBean clone() {
        return KryoConverters.deserialize(new DnsttBean(), KryoConverters.serialize(this));
    }

    public static final Creator<DnsttBean> CREATOR = new CREATOR<>() {
        @NonNull
        @Override
        public DnsttBean newInstance() {
            return new DnsttBean();
        }

        @Override
        public DnsttBean[] newArray(int size) {
            return new DnsttBean[size];
        }
    };
}
