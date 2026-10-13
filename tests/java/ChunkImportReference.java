import java.io.*;
import java.nio.file.*;
import io.netty.buffer.Unpooled;
import net.minecraft.init.Bootstrap;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.server.S21PacketChunkData;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;

/** Compares captured S21 decoding using the original Chunk.fillChunk method. */
public final class ChunkImportReference {
    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        int dimension = Integer.parseInt(args[0]);
        S21PacketChunkData packet = new S21PacketChunkData();
        packet.readPacketData(new PacketBuffer(Unpooled.wrappedBuffer(Files.readAllBytes(Paths.get(args[1])))));
        PhysicsReference.FixtureWorld world = new PhysicsReference.FixtureWorld(dimension);
        Chunk chunk = new Chunk(world, packet.getChunkX(), packet.getChunkZ());
        chunk.fillChunk(packet.getExtractedDataBytes(), packet.getExtractedSize(), packet.func_149274_i());
        try (OutputStream out = new FileOutputStream(args[2])) {
            ExtendedBlockStorage[] sections = chunk.getBlockStorageArray();
            for (int i = 0; i < 16; ++i) if ((packet.getExtractedSize() & (1 << i)) != 0)
                for (char state : sections[i].getData()) { out.write(state & 255); out.write(state >> 8); }
            for (int i = 0; i < 16; ++i) if ((packet.getExtractedSize() & (1 << i)) != 0) out.write(sections[i].getBlocklightArray().getData());
            if (!world.provider.getHasNoSky())
                for (int i = 0; i < 16; ++i) if ((packet.getExtractedSize() & (1 << i)) != 0) out.write(sections[i].getSkylightArray().getData());
            if (packet.func_149274_i()) out.write(chunk.getBiomeArray());
        }
    }
}
