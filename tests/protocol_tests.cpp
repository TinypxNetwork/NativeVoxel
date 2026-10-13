#include "mc/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace mc::net;

namespace {
std::size_t checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const std::string& message) {
    bool rejected = false;
    try { function(); } catch (const ProtocolError&) { rejected = true; }
    require(rejected, message);
}

void primitiveTests() {
    Writer w;
    w.u8(255).i8(-128).u16(0xabcd).i16(-30000).u32(0x89abcdef).i32(-2147483647)
        .u64(0xfedcba9876543210ull).i64(std::numeric_limits<std::int64_t>::min())
        .f32(-0.25f).f64(123456789.125).boolean(true).boolean(false);
    Reader r(w.data);
    require(r.u8() == 255, "unsigned byte");
    require(r.i8() == -128, "signed byte");
    require(r.u16() == 0xabcd, "unsigned short");
    require(r.i16() == -30000, "signed short");
    require(r.u32() == 0x89abcdef, "unsigned int");
    require(r.i32() == -2147483647, "signed int");
    require(r.u64() == 0xfedcba9876543210ull, "unsigned long");
    require(r.i64() == std::numeric_limits<std::int64_t>::min(), "signed long");
    require(r.f32() == -0.25f, "float");
    require(r.f64() == 123456789.125, "double");
    require(r.boolean() && !r.boolean(), "boolean");
    r.requireEnd();
    rejects([&] { r.u8(); }, "reader must reject missing byte");
    rejects([&] { r.skip(std::numeric_limits<std::size_t>::max()); }, "reader must reject overflow skip");
    for (std::size_t cut = 0; cut < 8; ++cut) {
        Reader truncated(w.data.data(), cut);
        rejects([&] { truncated.u64(); }, "truncated long");
    }
    Writer known;
    known.i32(0x12345678).i64(0x0123456789abcdefll).f32(1.0f).f64(1.0);
    require(known.data == std::vector<std::uint8_t>({0x12,0x34,0x56,0x78,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
        0x3f,0x80,0,0,0x3f,0xf0,0,0,0,0,0,0}), "known big-endian protocol vector");
}

void varintTests() {
    const std::vector<std::pair<std::int32_t, std::vector<std::uint8_t>>> cases = {
        {0,{0}}, {1,{1}}, {127,{127}}, {128,{0x80,1}}, {255,{0xff,1}}, {2097151,{0xff,0xff,0x7f}},
        {2147483647,{0xff,0xff,0xff,0xff,7}}, {-1,{0xff,0xff,0xff,0xff,15}},
        {std::numeric_limits<std::int32_t>::min(),{0x80,0x80,0x80,0x80,8}}
    };
    for (const auto& value : cases) {
        Writer w; w.varInt(value.first);
        require(w.data == value.second, "known VarInt encoding");
        Reader r(value.second);
        require(r.varInt() == value.first && r.remaining() == 0, "known VarInt decoding");
        for (std::size_t cut = 0; cut < value.second.size(); ++cut) {
            Reader truncated(value.second.data(), cut);
            rejects([&] { truncated.varInt(); }, "truncated VarInt");
        }
    }
    std::mt19937 random(47);
    for (int i = 0; i < 10000; ++i) {
        const auto integer = static_cast<std::int32_t>(random());
        const auto integer64 = static_cast<std::int64_t>((static_cast<std::uint64_t>(random()) << 32) | random());
        Writer w; w.varInt(integer).varLong(integer64);
        Reader r(w.data);
        require(r.varInt() == integer && r.varLong() == integer64 && r.remaining() == 0, "random variable integer roundtrip");
    }
    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{{0x80,0x80,0x80,0x80,0x80,0}, {0xff,0xff,0xff,0xff,0x7f}}) {
        Reader r(bytes); rejects([&] { r.varInt(); }, "oversize VarInt");
    }
    const std::vector<std::uint8_t> longOverflow(10, 0xff);
    Reader r(longOverflow); rejects([&] { r.varLong(); }, "oversize VarLong");
}

void stringTests() {
    const std::string unicode = "ASCII " "\xe4\xb8\xad\xe6\x96\x87" "\xf0\x9f\x98\x80";
    Writer w; w.string(unicode, 10).string("").string(std::string("a\0b",3));
    Reader r(w.data);
    require(r.string(10) == unicode, "UTF-8 string roundtrip");
    require(r.string().empty(), "empty string");
    require(r.string() == std::string("a\0b",3), "embedded null string");
    r.requireEnd();
    rejects([&] { Writer().string(unicode, 9); }, "UTF-16 surrogate pair counts as two units");
    Reader small(w.data); rejects([&] { small.string(9); }, "incoming UTF-16 length");
    const auto negative = Writer().varInt(-1).data;
    Reader negativeReader(negative); rejects([&] { negativeReader.string(); }, "negative string length");
    for (const std::string invalid : {std::string("\xc0\x80",2), std::string("\xed\xa0\x80",3), std::string("\xf4\x90\x80\x80",4), std::string("\xe4",1)}) {
        rejects([&] { Writer().string(invalid); }, "malformed outgoing UTF-8");
        Writer bad; bad.varInt(static_cast<int>(invalid.size())).bytes(reinterpret_cast<const std::uint8_t*>(invalid.data()), invalid.size());
        Reader incoming(bad.data); rejects([&] { incoming.string(); }, "malformed incoming UTF-8");
    }
    rejects([] { Writer().string(std::string(32768, 'a')); }, "Java writeString byte limit");
    const auto array = Writer().byteArray({1,2,3,4}).data;
    Reader bytes(array); require(bytes.byteArray(4) == std::vector<std::uint8_t>({1,2,3,4}), "bounded byte array");
    Reader limited(array); rejects([&] { limited.byteArray(3); }, "array maximum");
}

void frameTests() {
    std::mt19937 random(18);
    for (const int threshold : {-1, 0, 1, 16, 256, 1000000}) {
        for (const int size : {0, 1, 15, 16, 127, 255, 256, 1024, 65535}) {
            std::vector<std::uint8_t> body(static_cast<std::size_t>(size));
            for (auto& byte : body) byte = static_cast<std::uint8_t>(random());
            const auto wire = encodePacket(0x3f, body, threshold);
            Framer framer; framer.setCompression(threshold);
            Packet packet;
            const std::size_t fragment = size > 4096 ? 113 : 1;
            for (std::size_t offset = 0; offset < wire.size(); offset += fragment) {
                const auto count = std::min(fragment, wire.size() - offset);
                framer.append(wire.data() + offset, count);
                const bool ready = framer.next(packet);
                require(ready == (offset + count == wire.size()), "fragmentation readiness");
            }
            require(packet.id == 0x3f && packet.data == body, "framed packet identity");
            require(!framer.next(packet) && framer.buffered() == 0, "no phantom packet");
        }
    }
    Framer coalesced;
    auto enable = encodePacket(3, Writer().varInt(16).data);
    const auto success = encodePacket(2, Writer().string("login").data, 16);
    const auto large = encodePacket(0x21, std::vector<std::uint8_t>(100000, 42), 16);
    enable.insert(enable.end(), success.begin(), success.end());
    enable.insert(enable.end(), large.begin(), large.end());
    coalesced.append(enable);
    Packet packet;
    require(coalesced.next(packet) && packet.id == 3, "compression control first");
    Reader compression(packet.data); coalesced.setCompression(compression.varInt());
    require(coalesced.next(packet) && packet.id == 2, "coalesced uncompressed data envelope");
    require(coalesced.next(packet) && packet.id == 0x21 && packet.data == std::vector<std::uint8_t>(100000,42), "coalesced compressed packet");
    require(!coalesced.next(packet), "coalesced exhausted");

    const auto maxFrame = encodePacket(0x21, std::vector<std::uint8_t>(MaxPacketSize - 1, 0), 256);
    Framer maximum; maximum.setCompression(256); maximum.append(maxFrame);
    require(maximum.next(packet) && packet.data.size() == MaxPacketSize - 1, "exact 2 MiB decompressed maximum");
    rejects([] { encodePacket(0, std::vector<std::uint8_t>(MaxPacketSize, 0)); }, "outgoing payload cap");
    rejects([] { encodePacket(0, std::vector<std::uint8_t>(MaxPacketSize - 1, 0)); }, "uncompressed wire length cap");
    for (const auto& bad : std::vector<std::vector<std::uint8_t>>{{0}, {0x80,0x80,0x80}, {1,0x80}, {5,0xff,0xff,0xff,0xff,0x0f}}) {
        Framer f; f.append(bad); rejects([&] { f.next(packet); }, "malformed frame rejection");
    }
    Framer overfull;
    rejects([&] { overfull.append(std::vector<std::uint8_t>(MaxPacketSize * 3, 0)); }, "receive buffer cap");
    Framer belowThreshold; belowThreshold.setCompression(256); belowThreshold.append(encodePacket(1, {1}, 0));
    rejects([&] { belowThreshold.next(packet); }, "compressed packet below threshold");
    Framer corrupt; corrupt.setCompression(0);
    auto corrupted = encodePacket(1, std::vector<std::uint8_t>(200, 7), 0); corrupted.back() ^= 1;
    corrupt.append(corrupted); rejects([&] { corrupt.next(packet); }, "zlib checksum rejection");
    Framer wrongLength; wrongLength.setCompression(0);
    auto wrong = encodePacket(1, {2,3,4}, 0); wrong[1] = 5;
    wrongLength.append(wrong); rejects([&] { wrongLength.next(packet); }, "zlib exact inflated size");
    Writer hugeBody; hugeBody.varInt(static_cast<int>(MaxPacketSize + 1)).u8(0);
    Writer hugeFrame; hugeFrame.varInt(static_cast<int>(hugeBody.data.size())).bytes(hugeBody.data);
    Framer bomb; bomb.setCompression(0); bomb.append(hugeFrame.data);
    rejects([&] { bomb.next(packet); }, "inflate allocation cap");
}

int statusProbe(const std::string& host, std::uint16_t port) {
    const auto result = queryStatus(host, port, 5000);
    if (!result.ok) { std::cerr << "STATUS_ERROR=" << result.error << '\n'; return 1; }
    std::cout << "STATUS_JSON=" << result.json << "\nLATENCY_MS=" << result.latencyMs << '\n';
    return 0;
}

int mockClient(const std::string& host, std::uint16_t port, const std::string& mode) {
    Client client;
    std::optional<Auth> auth;
#ifdef MC_PROTOCOL_TESTING
    if (mode == "encrypted" || mode == "auth-reject") {
        auth = Auth{"unit-test-token-not-a-credential", "123456781234123412341234567890ab"};
        client.setSessionJoinForTests([mode](const Auth& profile, const std::string& hash, std::string& error) {
            if (profile.uuid != "123456781234123412341234567890ab" || profile.accessToken != "unit-test-token-not-a-credential") {
                error = "bad test profile"; return false;
            }
            std::cout << "TEST_JOIN_HASH=" << hash << std::endl;
            if (mode == "auth-reject") { error = "intentional mock session rejection"; return false; }
            return true;
        });
    }
#endif
    if (!client.connect(host, port, "NativeTest", std::move(auth), 3000)) {
        std::cerr << "CONNECT_ERROR=" << client.error() << '\n'; return 2;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool sentRoundtrip = false;
    bool receivedRoundtrip = false;
    bool reportedPlay = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto packets = client.poll();
        if (client.state() == State::Play && !reportedPlay) {
            std::cout << "PLAY encrypted=" << client.encrypted() << " compression=" << client.compressionThreshold() << std::endl;
            reportedPlay = true;
        }
        if (client.state() == State::Play && !sentRoundtrip) {
            Writer body; body.string("TEST|ROUNDTRIP");
            for (int i = 0; i < 4096; ++i) body.u8(static_cast<std::uint8_t>(i * 37 + 11));
            if (!client.send(0x17, body.data)) throw std::runtime_error("roundtrip send failed");
            sentRoundtrip = true;
        }
        for (const auto& packet : packets) {
            if (packet.id == 0) throw std::runtime_error("keepalive leaked to application");
            if (packet.id == 0x3f) {
                Reader body(packet.data);
                require(body.string(20) == "TEST|ROUNDTRIP", "roundtrip channel");
                for (int i = 0; i < 4096; ++i) require(body.u8() == static_cast<std::uint8_t>(i * 37 + 11), "roundtrip payload");
                body.requireEnd(); receivedRoundtrip = true;
                std::cout << "ROUNDTRIP_OK" << std::endl;
            }
        }
        if (client.state() == State::Failed) {
            std::cout << "CLIENT_ERROR=" << client.error() << std::endl;
            if (mode == "auth-reject") return client.error() == "intentional mock session rejection" ? 0 : 3;
            if (mode == "no-auth") {
                require(client.encryptionRequest().received && client.encryptionRequest().serverId == "native-test" &&
                    client.encryptionRequest().publicKeyBytes > 0 && client.encryptionRequest().verifyTokenBytes == 4,
                    "nonsecret encryption request diagnostics survive disconnect");
                return client.error().find("login encryption") != std::string::npos ? 0 : 3;
            }
            if (mode == "malformed") return 0;
            return 3;
        }
        if (client.state() == State::Disconnected) {
            std::cout << "DISCONNECTED=" << client.error() << std::endl;
            if (receivedRoundtrip && client.error() == "{\"text\":\"test complete\"}") return 0;
            return 4;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cerr << "MOCK_TIMEOUT\n"; return 5;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "--status")
            return statusProbe(argv[2], argc >= 4 ? static_cast<std::uint16_t>(std::stoi(argv[3])) : 25565);
        if (argc == 5 && std::string(argv[1]) == "--client")
            return mockClient(argv[2], static_cast<std::uint16_t>(std::stoi(argv[3])), argv[4]);
        primitiveTests(); varintTests(); stringTests(); frameTests();
        std::cout << "protocol tests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
