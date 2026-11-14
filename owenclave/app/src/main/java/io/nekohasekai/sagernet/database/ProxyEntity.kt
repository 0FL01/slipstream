package io.nekohasekai.sagernet.database

import android.content.Context
import android.content.Intent
import androidx.room.Dao as RoomDao
import androidx.room.Delete
import androidx.room.Entity
import androidx.room.Ignore
import androidx.room.Index
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Update
import com.esotericsoftware.kryo.io.ByteBufferInput
import com.esotericsoftware.kryo.io.ByteBufferOutput
import io.nekohasekai.sagernet.aidl.TrafficStats
import io.nekohasekai.sagernet.fmt.AbstractBean
import io.nekohasekai.sagernet.fmt.KryoConverters
import io.nekohasekai.sagernet.fmt.Serializable
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.olcrtc.OLCRTCBean
import io.nekohasekai.sagernet.fmt.olcrtc.toUri
import io.nekohasekai.sagernet.ui.compose.ComposeProfileSettingsActivity

@Entity(tableName = "proxy_entities", indices = [Index("groupId", name = "groupId")])
data class ProxyEntity(
    @PrimaryKey(autoGenerate = true) var id: Long = 0L,
    var groupId: Long = 0L,
    var type: Int = 0,
    var userOrder: Long = 0L,
    var tx: Long = 0L,
    var rx: Long = 0L,
    var status: Int = 0,
    var ping: Int = 0,
    @androidx.room.ColumnInfo(defaultValue = "0") var connectedTime: Long = 0L,
    var uuid: String = "",
    var error: String? = null,
    var dnsttBean: DnsttBean? = null,
    var olcrtcBean: OLCRTCBean? = null,
    @androidx.room.ColumnInfo(defaultValue = "-1") var iconIndex: Int = -1,
) : Serializable() {

    companion object {
        const val TYPE_DNSTT = 17
        const val TYPE_OLCRTC = 31

        @JvmField
        val CREATOR = object : CREATOR<ProxyEntity>() {
            override fun newInstance() = ProxyEntity()
            override fun newArray(size: Int): Array<ProxyEntity?> = arrayOfNulls(size)
        }
    }

    @Ignore @Transient var dirty: Boolean = false
    @Ignore @Transient var stats: TrafficStats? = null

    override fun initializeDefaultValues() = Unit

    override fun serializeToBuffer(output: ByteBufferOutput) {
        output.writeInt(0)
        output.writeLong(id)
        output.writeLong(groupId)
        output.writeInt(type)
        output.writeLong(userOrder)
        output.writeLong(tx)
        output.writeLong(rx)
        output.writeInt(status)
        output.writeInt(ping)
        output.writeString(uuid)
        output.writeString(error)
        val data = KryoConverters.serialize(requireBean())
        output.writeVarInt(data.size, true)
        output.writeBytes(data)
        output.writeBoolean(dirty)
    }

    override fun deserializeFromBuffer(input: ByteBufferInput) {
        input.readInt()
        id = input.readLong()
        groupId = input.readLong()
        type = input.readInt()
        userOrder = input.readLong()
        tx = input.readLong()
        rx = input.readLong()
        status = input.readInt()
        ping = input.readInt()
        uuid = input.readString()
        error = input.readString()
        putByteArray(input.readBytes(input.readVarInt(true)))
        dirty = input.readBoolean()
    }

    fun putByteArray(bytes: ByteArray) {
        when (type) {
            TYPE_DNSTT -> dnsttBean = KryoConverters.dnsttDeserialize(bytes)
            TYPE_OLCRTC -> olcrtcBean = KryoConverters.olcrtcDeserialize(bytes)
            else -> error("Unsupported profile type $type")
        }
    }

    fun displayType() = when (type) {
        TYPE_DNSTT -> "DNS Tunnel"
        TYPE_OLCRTC -> "olcRTC"
        else -> "Invalid"
    }

    fun displayName() = requireBean().displayName()
    fun displayAddress() = requireBean().displayAddress()

    fun requireBean(): AbstractBean = when (type) {
        TYPE_DNSTT -> dnsttBean
        TYPE_OLCRTC -> olcrtcBean
        else -> null
    } ?: error("Missing bean for profile type $type")

    fun canExportBackup() = true
    fun hasShareLink() = type == TYPE_OLCRTC
    fun toLink() = (requireBean() as? OLCRTCBean)?.toUri()
    fun needExternal() = type == TYPE_OLCRTC

    fun putBean(bean: AbstractBean): ProxyEntity {
        dnsttBean = null
        olcrtcBean = null
        when (bean) {
            is DnsttBean -> {
                type = TYPE_DNSTT
                dnsttBean = bean
            }
            is OLCRTCBean -> {
                type = TYPE_OLCRTC
                olcrtcBean = bean
            }
            else -> error("Unsupported profile ${bean.javaClass.simpleName}")
        }
        return this
    }

    fun settingIntent(ctx: Context, isSubscription: Boolean): Intent? {
        if (type != TYPE_DNSTT && type != TYPE_OLCRTC) return null
        return Intent(ctx, ComposeProfileSettingsActivity::class.java).apply {
            putExtra(ComposeProfileSettingsActivity.EXTRA_PROFILE_ID, id)
            putExtra(ComposeProfileSettingsActivity.EXTRA_PROFILE_TYPE, type)
        }
    }

    @RoomDao
    interface Dao {
        @Query("select * from proxy_entities") fun getAll(): List<ProxyEntity>
        @Query("SELECT id FROM proxy_entities WHERE groupId = :groupId ORDER BY userOrder") fun getIdsByGroup(groupId: Long): List<Long>
        @Query("SELECT * FROM proxy_entities WHERE groupId = :groupId ORDER BY userOrder") fun getByGroup(groupId: Long): List<ProxyEntity>
        @Query("SELECT * FROM proxy_entities WHERE groupId = :groupId ORDER BY userOrder") fun getByGroupFlow(groupId: Long): kotlinx.coroutines.flow.Flow<List<ProxyEntity>>
        @Query("SELECT * FROM proxy_entities WHERE id in (:proxyIds)") fun getEntities(proxyIds: List<Long>): List<ProxyEntity>
        @Query("SELECT COUNT(*) FROM proxy_entities WHERE groupId = :groupId") fun countByGroup(groupId: Long): Long
        @Query("SELECT MAX(userOrder) + 1 FROM proxy_entities WHERE groupId = :groupId") fun nextOrder(groupId: Long): Long?
        @Query("SELECT * FROM proxy_entities WHERE id = :proxyId") fun getById(proxyId: Long): ProxyEntity?
        @Query("SELECT COUNT(*) FROM proxy_entities WHERE groupId = :groupId AND id = :proxyId LIMIT 1") fun isIdInGroup(proxyId: Long, groupId: Long): Long
        @Query("DELETE FROM proxy_entities WHERE id IN (:proxyId)") fun deleteById(proxyId: Long): Int
        @Query("DELETE FROM proxy_entities WHERE groupId = :groupId") fun deleteByGroup(groupId: Long)
        @Query("DELETE FROM proxy_entities WHERE groupId in (:groupId)") fun deleteByGroup(groupId: LongArray)
        @Delete fun deleteProxy(proxy: ProxyEntity): Int
        @Delete fun deleteProxy(proxies: List<ProxyEntity>): Int
        @Update fun updateProxy(proxy: ProxyEntity): Int
        @Update fun updateProxy(proxies: List<ProxyEntity>): Int
        @Insert fun addProxy(proxy: ProxyEntity): Long
        @Insert fun insert(proxies: List<ProxyEntity>)
        @Query("DELETE FROM proxy_entities WHERE groupId = :groupId") fun deleteAll(groupId: Long): Int
        @Query("DELETE FROM proxy_entities") fun reset()
        @Query("""
            UPDATE proxy_entities
               SET tx = CASE WHEN :tx IS NULL THEN tx ELSE :tx END,
                   rx = CASE WHEN :rx IS NULL THEN rx ELSE :rx END
             WHERE id = :id
        """) fun updateTraffic(id: Long, tx: Long?, rx: Long?): Int
        @Query("UPDATE proxy_entities SET connectedTime = :time WHERE id = :id") fun updateConnectedTime(id: Long, time: Long): Int
    }

    override fun describeContents() = 0
}
