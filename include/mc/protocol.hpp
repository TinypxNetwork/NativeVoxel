#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef MC_PROTOCOL_TESTING
#include <functional>
#endif

namespace mc::net {

inline constexpr int ProtocolVersion = 47;
inline constexpr std::size_t MaxPacketSize = 2097152;

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Readers do not own their input. All lengths and accesses are checked.
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data);
    Reader(const std::uint8_t* data, std::size_t size);
    std::uint8_t u8();
    std::int8_t i8();
    std::uint16_t u16();
    std::int16_t i16();
    std::uint32_t u32();
    std::int32_t i32();
    std::uint64_t u64();
    std::int64_t i64();
    float f32();
    double f64();
    bool boolean();
    std::int32_t varInt();
    std::int64_t varLong();
    std::string string(std::size_t maxChars = 32767);
    std::vector<std::uint8_t> bytes(std::size_t count);
    std::vector<std::uint8_t> byteArray(std::size_t maxBytes = MaxPacketSize);
    void skip(std::size_t count);
    std::size_t remaining() const noexcept;
    std::size_t position() const noexcept;
    const std::uint8_t* current() const noexcept;
    void requireEnd() const;
private:
    void require(std::size_t count) const;
    const std::uint8_t* data_;
    std::size_t size_, pos_ = 0;
};

class Writer {
public:
    std::vector<std::uint8_t> data;
    Writer& u8(std::uint8_t value);
    Writer& i8(std::int8_t value);
    Writer& u16(std::uint16_t value);
    Writer& i16(std::int16_t value);
    Writer& u32(std::uint32_t value);
    Writer& i32(std::int32_t value);
    Writer& u64(std::uint64_t value);
    Writer& i64(std::int64_t value);
    Writer& f32(float value);
    Writer& f64(double value);
    Writer& boolean(bool value);
    Writer& varInt(std::int32_t value);
    Writer& varLong(std::int64_t value);
    Writer& string(const std::string& value, std::size_t maxChars = 32767);
    Writer& bytes(const std::uint8_t* value, std::size_t count);
    Writer& bytes(const std::vector<std::uint8_t>& value);
    Writer& byteArray(const std::vector<std::uint8_t>& value);
private:
    void requireSpace(std::size_t count) const;
};

struct Packet {
    std::int32_t id = 0;
    std::vector<std::uint8_t> data; // Does not include the packet ID.
};

// TCP frame decoder. Change compression immediately after taking its login
// packet, before decoding the next frame (both may arrive in the same recv).
class Framer {
public:
    void append(const std::uint8_t* data, std::size_t size);
    void append(const std::vector<std::uint8_t>& data);
    bool next(Packet& packet);
    void setCompression(int threshold);
    int compressionThreshold() const noexcept;
    std::size_t buffered() const noexcept;
    void clear();
private:
    std::vector<std::uint8_t> buffer_;
    std::size_t offset_ = 0;
    int compression_ = -1;
};

std::vector<std::uint8_t> encodePacket(std::int32_t id,
    const std::vector<std::uint8_t>& body, int compressionThreshold = -1);

struct Auth {
    std::string accessToken;
    std::string uuid;
    // Both MC_ACCESS_TOKEN and MC_UUID must be set. Incomplete/invalid values
    // are rejected on connect, never silently converted into an offline login.
    static std::optional<Auth> fromEnvironment();
};

enum class State { Disconnected, Connecting, Login, Play, Failed };

struct EncryptionRequestInfo {
    bool received = false;
    std::string serverId;
    std::size_t publicKeyBytes = 0;
    std::size_t verifyTokenBytes = 0;
};

// A single main-thread owner pumps this nonblocking connection. Online session
// authorization uses a bounded background HTTPS request, with certificate
// verification enabled. Neither credentials nor service response bodies are logged.
class Client {
public:
    Client();
    ~Client();
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect(const std::string& host, std::uint16_t port,
        const std::string& username, std::optional<Auth> auth = std::nullopt,
        int timeoutMs = 5000);
    std::vector<Packet> poll();
    bool send(std::int32_t id, const std::vector<std::uint8_t>& body);
    void disconnect();
    State state() const noexcept;
    const std::string& error() const noexcept;
    const std::string& username() const noexcept;
    const std::string& uuid() const noexcept;
    int compressionThreshold() const noexcept;
    bool encrypted() const noexcept;
    std::uint64_t bytesReceived() const noexcept;
    std::uint64_t bytesSent() const noexcept;
    const EncryptionRequestInfo& encryptionRequest() const noexcept;

#ifdef MC_PROTOCOL_TESTING
    // Only compiled into the isolated test executable. The production client
    // always performs Mojang's authenticated HTTPS session-join request.
    using SessionJoinTestHook = std::function<bool(const Auth&, const std::string&, std::string&)>;
    void setSessionJoinForTests(SessionJoinTestHook hook);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct StatusResult {
    bool ok = false;
    std::string json;
    std::string error;
    std::int64_t latencyMs = -1;
};

struct ServerAddress {
    std::string host;
    std::uint16_t port = 25565;
    bool usedSrv = false;
};

// Matches 1.8.9 ServerAddress: a default-port hostname may be redirected by its
// _minecraft._tcp SRV record. Lookup failure falls back to the supplied address.
// Client::connect and queryStatus already call this automatically.
ServerAddress resolveServerAddress(const std::string& host, std::uint16_t port = 25565,
    int timeoutMs = 1500);

// This bounded synchronous operation belongs on a launcher/loading thread.
StatusResult queryStatus(const std::string& host, std::uint16_t port = 25565,
    int timeoutMs = 5000);

} // namespace mc::net
