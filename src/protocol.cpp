#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "mc/protocol.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <windns.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace mc::net {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t MaxWirePacket = (1u << 21) - 1;
constexpr std::size_t MaxBuffered = MaxPacketSize * 2 + 8192;
constexpr std::size_t MaxQueued = MaxPacketSize * 4;
constexpr int LoginTimeoutMs = 45000;
constexpr int ReadTimeoutMs = 30000;

template<class To, class From> To bitCopy(From from) {
    static_assert(sizeof(To) == sizeof(From), "bitCopy size mismatch");
    To to;
    std::memcpy(&to, &from, sizeof to);
    return to;
}

std::uint32_t utf8Next(const std::string& s, std::size_t& offset) {
    if (offset >= s.size()) throw ProtocolError("Truncated UTF-8 sequence");
    const auto first = static_cast<std::uint8_t>(s[offset++]);
    if (first < 0x80) return first;
    int count;
    std::uint32_t cp;
    std::uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; cp = first & 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; cp = first & 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; cp = first & 7; minimum = 0x10000; }
    else throw ProtocolError("Invalid UTF-8 leading byte");
    for (int i = 0; i < count; ++i) {
        if (offset >= s.size()) throw ProtocolError("Truncated UTF-8 sequence");
        const auto next = static_cast<std::uint8_t>(s[offset++]);
        if ((next & 0xc0) != 0x80) throw ProtocolError("Invalid UTF-8 continuation byte");
        cp = (cp << 6) | (next & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        throw ProtocolError("Invalid UTF-8 code point");
    return cp;
}

std::size_t utf16Length(const std::string& value) {
    std::size_t count = 0;
    for (std::size_t pos = 0; pos < value.size();) count += utf8Next(value, pos) > 0xffff ? 2 : 1;
    return count;
}

void cleanString(std::string& value) {
    if (!value.empty()) OPENSSL_cleanse(&value[0], value.size());
    value.clear();
}

std::string normalizedUuid(const std::string& uuid) {
    if (uuid.size() != 32 && uuid.size() != 36)
        throw ProtocolError("Profile UUID must contain 32 hexadecimal digits");
    std::string out;
    out.reserve(32);
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        const char c = uuid[i];
        if (uuid.size() == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) {
            if (c != '-') throw ProtocolError("Invalid profile UUID separators");
        } else if (c >= '0' && c <= '9') out.push_back(c);
        else if (c >= 'a' && c <= 'f') out.push_back(c);
        else if (c >= 'A' && c <= 'F') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else throw ProtocolError("Profile UUID contains a non-hexadecimal digit");
    }
    return out;
}

bool validUsername(const std::string& value) {
    return !value.empty() && value.size() <= 16 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_';
    });
}

std::string socketError(const char* operation, int code = WSAGetLastError()) {
    return std::string(operation) + " failed (Winsock " + std::to_string(code) + ")";
}

class Socket {
public:
    SOCKET value = INVALID_SOCKET;
    Socket() = default;
    explicit Socket(SOCKET v) : value(v) {}
    ~Socket() { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value(other.value) { other.value = INVALID_SOCKET; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) { close(); value = other.value; other.value = INVALID_SOCKET; }
        return *this;
    }
    void close() { if (value != INVALID_SOCKET) { closesocket(value); value = INVALID_SOCKET; } }
    explicit operator bool() const noexcept { return value != INVALID_SOCKET; }
};

void initializeWinsock() {
    // Process lifetime initialization also covers outstanding bounded DNS jobs.
    static const int result = [] { WSADATA data{}; return WSAStartup(MAKEWORD(2, 2), &data); }();
    if (result != 0) throw ProtocolError(socketError("WSAStartup", result));
}

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<std::int64_t>(left, std::numeric_limits<int>::max()));
}

struct ResolvedAddress { sockaddr_storage address{}; int size = 0; int family = AF_UNSPEC; };
struct DnsJob {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    int error = 0;
    std::vector<ResolvedAddress> addresses;
};

std::vector<ResolvedAddress> resolve(const std::string& host, std::uint16_t port, Clock::time_point deadline) {
    auto job = std::make_shared<DnsJob>();
    // getaddrinfo has no portable timeout. Its isolated worker may outlive a
    // timeout but owns all of its state and never accesses Client/UI objects.
    std::thread([job, host, port] {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* result = nullptr;
        const int error = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
        std::vector<ResolvedAddress> addresses;
        if (error == 0) {
            for (auto* p = result; p; p = p->ai_next) {
                if ((p->ai_family != AF_INET && p->ai_family != AF_INET6) ||
                    p->ai_addrlen > sizeof(sockaddr_storage)) continue;
                ResolvedAddress address;
                address.size = static_cast<int>(p->ai_addrlen);
                address.family = p->ai_family;
                std::memcpy(&address.address, p->ai_addr, p->ai_addrlen);
                addresses.push_back(address);
            }
        }
        if (result) freeaddrinfo(result);
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->error = error;
            job->addresses = std::move(addresses);
            job->done = true;
        }
        job->cv.notify_one();
    }).detach();
    std::unique_lock<std::mutex> lock(job->mutex);
    if (!job->cv.wait_until(lock, deadline, [&] { return job->done; }))
        throw ProtocolError("DNS resolution timed out");
    if (job->error != 0) throw ProtocolError(socketError("DNS resolution", job->error));
    if (job->addresses.empty()) throw ProtocolError("DNS returned no TCP addresses");
    return std::move(job->addresses);
}

Socket connectSocket(const std::string& host, std::uint16_t port, Clock::time_point deadline) {
    initializeWinsock();
    if (host.empty() || host.size() > 255 || host.find('\0') != std::string::npos || port == 0)
        throw ProtocolError("Invalid server hostname or port");
    const auto addresses = resolve(host, port, deadline);
    std::string error = "Connection timed out";
    for (const auto& address : addresses) {
        if (remainingMs(deadline) == 0) break;
        Socket candidate(::socket(address.family, SOCK_STREAM, IPPROTO_TCP));
        if (!candidate) { error = socketError("socket"); continue; }
        u_long nonblocking = 1;
        if (ioctlsocket(candidate.value, FIONBIO, &nonblocking) != 0) {
            error = socketError("Setting nonblocking mode"); continue;
        }
        int noDelay = 1;
        setsockopt(candidate.value, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&noDelay), sizeof noDelay);
        const int result = ::connect(candidate.value,
            reinterpret_cast<const sockaddr*>(&address.address), address.size);
        if (result == 0) return candidate;
        const int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
            error = socketError("connect", code); continue;
        }
        fd_set writes, errors;
        FD_ZERO(&writes); FD_ZERO(&errors);
        FD_SET(candidate.value, &writes); FD_SET(candidate.value, &errors);
        // Do not let an unavailable first address consume the entire timeout.
        const int waitMs = std::min(remainingMs(deadline), addresses.size() > 1 ? 1500 : std::numeric_limits<int>::max());
        timeval timeout{waitMs / 1000, (waitMs % 1000) * 1000};
        const int ready = select(0, nullptr, &writes, &errors, &timeout);
        if (ready == SOCKET_ERROR) { error = socketError("select"); continue; }
        if (ready == 0) { error = "Connection timed out"; continue; }
        int failure = 0;
        int len = sizeof failure;
        if (getsockopt(candidate.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&failure), &len) != 0) {
            error = socketError("getsockopt"); continue;
        }
        if (failure == 0) return candidate;
        error = socketError("connect", failure);
    }
    throw ProtocolError(error);
}

Writer handshake(const std::string& host, std::uint16_t port, int nextState) {
    Writer body;
    body.varInt(ProtocolVersion).string(host, 255).u16(port).varInt(nextState);
    return body;
}

std::string signedHexDigest(const std::array<std::uint8_t, SHA_DIGEST_LENGTH>& digest) {
    auto magnitude = digest;
    const bool negative = (magnitude[0] & 0x80) != 0;
    if (negative) {
        unsigned carry = 1;
        for (std::size_t i = magnitude.size(); i-- > 0;) {
            const unsigned value = static_cast<unsigned>(magnitude[i] ^ 0xffu) + carry;
            magnitude[i] = static_cast<std::uint8_t>(value);
            carry = value >> 8;
        }
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const auto b : magnitude) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    const auto first = hex.find_first_not_of('0');
    if (first == std::string::npos) return "0";
    hex.erase(0, first);
    return negative ? "-" + hex : hex;
}

std::string serverHash(const std::string& serverId, const std::array<std::uint8_t, 16>& secret,
    const std::vector<std::uint8_t>& publicKey) {
    std::string latin1;
    for (std::size_t i = 0; i < serverId.size();) {
        const auto cp = utf8Next(serverId, i);
        latin1.push_back(cp <= 255 ? static_cast<char>(cp) : '?');
    }
    SHA_CTX hash{};
    std::array<std::uint8_t, SHA_DIGEST_LENGTH> digest{};
    if (SHA1_Init(&hash) != 1 || SHA1_Update(&hash, latin1.data(), latin1.size()) != 1 ||
        SHA1_Update(&hash, secret.data(), secret.size()) != 1 ||
        SHA1_Update(&hash, publicKey.data(), publicKey.size()) != 1 ||
        SHA1_Final(digest.data(), &hash) != 1) throw ProtocolError("SHA-1 calculation failed");
    return signedHexDigest(digest);
}

std::vector<std::uint8_t> rsaEncrypt(EVP_PKEY* key, const std::uint8_t* data, std::size_t size) {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(EVP_PKEY_CTX_new(key, nullptr), EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_PADDING) <= 0)
        throw ProtocolError("RSA encryption initialization failed");
    std::size_t encryptedSize = 0;
    if (EVP_PKEY_encrypt(context.get(), nullptr, &encryptedSize, data, size) <= 0 || encryptedSize > 1024)
        throw ProtocolError("Invalid RSA encryption size");
    std::vector<std::uint8_t> result(encryptedSize);
    if (EVP_PKEY_encrypt(context.get(), result.data(), &encryptedSize, data, size) <= 0)
        throw ProtocolError("RSA encryption failed");
    result.resize(encryptedSize);
    return result;
}

class HttpHandle {
public:
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET h) : value(h) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    explicit operator bool() const noexcept { return value != nullptr; }
};

std::string jsonEscape(const std::string& value) {
    std::string result;
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { result.push_back('\\'); result.push_back(c); }
        else if (c < 32) { result += "\\u00"; result.push_back(hex[c >> 4]); result.push_back(hex[c & 15]); }
        else result.push_back(c);
    }
    return result;
}

bool joinSession(const Auth& auth, const std::string& hash, std::string& error) {
    HttpHandle session(WinHttpOpen(L"NativeVoxel/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { error = "Could not initialize verified HTTPS session"; return false; }
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 10000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        error = "TLS 1.2 is unavailable"; return false;
    }
    HttpHandle connection(WinHttpConnect(session.value, L"sessionserver.mojang.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) { error = "Could not connect to Minecraft session service"; return false; }
    HttpHandle request(WinHttpOpenRequest(connection.value, L"POST", L"/session/minecraft/join",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) { error = "Could not create Minecraft session request"; return false; }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof redirectPolicy)) {
        error = "Could not disable authentication request redirects"; return false;
    }
    // Windows validates the remote certificate chain and hostname. Never set
    // SECURITY_FLAG_IGNORE_* and never accept a redirect carrying credentials.
    std::string body = "{\"accessToken\":\"" + jsonEscape(auth.accessToken) +
        "\",\"selectedProfile\":\"" + normalizedUuid(auth.uuid) + "\",\"serverId\":\"" + hash + "\"}";
    static constexpr wchar_t headers[] = L"Content-Type: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1),
        &body[0], static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    // WinHTTP can refer to this buffer until the response has arrived.
    const BOOL received = sent ? WinHttpReceiveResponse(request.value, nullptr) : FALSE;
    const DWORD receiveError = received ? ERROR_SUCCESS : GetLastError();
    cleanString(body);
    if (!sent || !received) {
        error = "Minecraft session HTTPS request failed (Windows " + std::to_string(sent ? receiveError : sendError) + ")";
        return false;
    }
    DWORD status = 0;
    DWORD size = sizeof status;
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        error = "Could not read Minecraft session response status"; return false;
    }
    if (status != 204) {
        if (status == 401 || status == 403) error = "Minecraft session rejected: token/profile invalid or expired (HTTP " + std::to_string(status) + ")";
        else error = "Minecraft session service rejected join (HTTP " + std::to_string(status) + ")";
        return false;
    }
    return true;
}

struct AuthJob {
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    bool success = false;
    std::string error;
    Auth auth;
    std::string hash;
    std::array<std::uint8_t, 16> secret{};
    std::vector<std::uint8_t> response;
    ~AuthJob() { cleanString(auth.accessToken); OPENSSL_cleanse(secret.data(), secret.size()); }
};

} // namespace

Reader::Reader(const std::vector<std::uint8_t>& data) : Reader(data.data(), data.size()) {}
Reader::Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
    if (!data && size) throw ProtocolError("Null packet buffer");
}
void Reader::require(std::size_t count) const {
    if (count > size_ - pos_) throw ProtocolError("Truncated packet");
}
std::uint8_t Reader::u8() { require(1); return data_[pos_++]; }
std::int8_t Reader::i8() { return bitCopy<std::int8_t>(u8()); }
std::uint16_t Reader::u16() { const auto hi = u8(); return static_cast<std::uint16_t>((hi << 8) | u8()); }
std::int16_t Reader::i16() { return bitCopy<std::int16_t>(u16()); }
std::uint32_t Reader::u32() { const auto hi = u16(); return (static_cast<std::uint32_t>(hi) << 16) | u16(); }
std::int32_t Reader::i32() { return bitCopy<std::int32_t>(u32()); }
std::uint64_t Reader::u64() { const auto hi = u32(); return (static_cast<std::uint64_t>(hi) << 32) | u32(); }
std::int64_t Reader::i64() { return bitCopy<std::int64_t>(u64()); }
float Reader::f32() { return bitCopy<float>(u32()); }
double Reader::f64() { return bitCopy<double>(u64()); }
bool Reader::boolean() { return u8() != 0; }
std::int32_t Reader::varInt() {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto b = u8();
        if (i == 4 && (b & 0xf0)) throw ProtocolError("VarInt exceeds 32 bits");
        value |= static_cast<std::uint32_t>(b & 127) << (i * 7);
        if ((b & 128) == 0) return bitCopy<std::int32_t>(value);
    }
    throw ProtocolError("VarInt exceeds five bytes");
}
std::int64_t Reader::varLong() {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 10; ++i) {
        const auto b = u8();
        if (i == 9 && (b & 0xfe)) throw ProtocolError("VarLong exceeds 64 bits");
        value |= static_cast<std::uint64_t>(b & 127) << (i * 7);
        if ((b & 128) == 0) return bitCopy<std::int64_t>(value);
    }
    throw ProtocolError("VarLong exceeds ten bytes");
}
std::string Reader::string(std::size_t maxChars) {
    const auto size = varInt();
    if (size < 0 || static_cast<std::size_t>(size) > MaxPacketSize ||
        (maxChars <= MaxPacketSize && static_cast<std::size_t>(size) > maxChars * 4))
        throw ProtocolError("String byte length exceeds protocol limit");
    require(static_cast<std::size_t>(size));
    std::string value;
    if (size) value.assign(reinterpret_cast<const char*>(data_ + pos_), static_cast<std::size_t>(size));
    if (utf16Length(value) > maxChars) throw ProtocolError("String character length exceeds protocol limit");
    pos_ += static_cast<std::size_t>(size);
    return value;
}
std::vector<std::uint8_t> Reader::bytes(std::size_t count) {
    require(count);
    if (count == 0) return {};
    std::vector<std::uint8_t> result(data_ + pos_, data_ + pos_ + count);
    pos_ += count;
    return result;
}
std::vector<std::uint8_t> Reader::byteArray(std::size_t maxBytes) {
    const auto size = varInt();
    if (size < 0 || static_cast<std::size_t>(size) > std::min(maxBytes, MaxPacketSize))
        throw ProtocolError("Byte array length exceeds protocol limit");
    return bytes(static_cast<std::size_t>(size));
}
void Reader::skip(std::size_t count) { require(count); pos_ += count; }
std::size_t Reader::remaining() const noexcept { return size_ - pos_; }
std::size_t Reader::position() const noexcept { return pos_; }
const std::uint8_t* Reader::current() const noexcept { return data_ ? data_ + pos_ : nullptr; }
void Reader::requireEnd() const { if (remaining()) throw ProtocolError("Unexpected bytes after packet fields"); }

void Writer::requireSpace(std::size_t count) const {
    if (data.size() > MaxPacketSize || count > MaxPacketSize - data.size())
        throw ProtocolError("Packet exceeds protocol size limit");
}
Writer& Writer::u8(std::uint8_t value) { requireSpace(1); data.push_back(value); return *this; }
Writer& Writer::i8(std::int8_t value) { return u8(static_cast<std::uint8_t>(value)); }
Writer& Writer::u16(std::uint16_t value) { return u8(static_cast<std::uint8_t>(value >> 8)).u8(static_cast<std::uint8_t>(value)); }
Writer& Writer::i16(std::int16_t value) { return u16(static_cast<std::uint16_t>(value)); }
Writer& Writer::u32(std::uint32_t value) { return u16(static_cast<std::uint16_t>(value >> 16)).u16(static_cast<std::uint16_t>(value)); }
Writer& Writer::i32(std::int32_t value) { return u32(static_cast<std::uint32_t>(value)); }
Writer& Writer::u64(std::uint64_t value) { return u32(static_cast<std::uint32_t>(value >> 32)).u32(static_cast<std::uint32_t>(value)); }
Writer& Writer::i64(std::int64_t value) { return u64(static_cast<std::uint64_t>(value)); }
Writer& Writer::f32(float value) { return u32(bitCopy<std::uint32_t>(value)); }
Writer& Writer::f64(double value) { return u64(bitCopy<std::uint64_t>(value)); }
Writer& Writer::boolean(bool value) { return u8(value ? 1 : 0); }
Writer& Writer::varInt(std::int32_t value) {
    auto remaining = static_cast<std::uint32_t>(value);
    do { const auto b = static_cast<std::uint8_t>(remaining & 127); remaining >>= 7; u8(b | (remaining ? 128 : 0)); } while (remaining);
    return *this;
}
Writer& Writer::varLong(std::int64_t value) {
    auto remaining = static_cast<std::uint64_t>(value);
    do { const auto b = static_cast<std::uint8_t>(remaining & 127); remaining >>= 7; u8(b | (remaining ? 128 : 0)); } while (remaining);
    return *this;
}
Writer& Writer::string(const std::string& value, std::size_t maxChars) {
    if (value.size() > 32767 || utf16Length(value) > maxChars)
        throw ProtocolError("Outgoing string exceeds protocol limit");
    varInt(static_cast<std::int32_t>(value.size()));
    return bytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}
Writer& Writer::bytes(const std::uint8_t* value, std::size_t count) {
    requireSpace(count);
    if (!value && count) throw ProtocolError("Null outgoing byte array");
    if (count) data.insert(data.end(), value, value + count);
    return *this;
}
Writer& Writer::bytes(const std::vector<std::uint8_t>& value) { return bytes(value.data(), value.size()); }
Writer& Writer::byteArray(const std::vector<std::uint8_t>& value) {
    if (value.size() > MaxPacketSize) throw ProtocolError("Outgoing byte array exceeds protocol limit");
    varInt(static_cast<std::int32_t>(value.size()));
    return bytes(value);
}

void Framer::append(const std::uint8_t* data, std::size_t size) {
    if (!data && size) throw ProtocolError("Null received frame buffer");
    if (size > MaxBuffered - buffered()) throw ProtocolError("Incoming frame buffer limit exceeded");
    if (offset_ && (offset_ >= 65536 || offset_ == buffer_.size() || buffer_.size() + size > MaxBuffered)) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_)); offset_ = 0;
    }
    if (size) buffer_.insert(buffer_.end(), data, data + size);
}
void Framer::append(const std::vector<std::uint8_t>& data) { append(data.data(), data.size()); }
bool Framer::next(Packet& packet) {
    std::uint32_t size = 0;
    std::size_t prefix = 0;
    for (; prefix < 3; ++prefix) {
        if (prefix >= buffered()) return false;
        const auto byte = buffer_[offset_ + prefix];
        size |= static_cast<std::uint32_t>(byte & 127) << (prefix * 7);
        if ((byte & 128) == 0) { ++prefix; break; }
        if (prefix == 2) throw ProtocolError("Frame length exceeds 21 bits");
    }
    if (!size) throw ProtocolError("Empty packet frame");
    if (buffered() - prefix < size) return false;
    Reader frame(buffer_.data() + offset_ + prefix, size);
    std::vector<std::uint8_t> payload;
    if (compression_ >= 0) {
        const auto plainSize = frame.varInt();
        if (plainSize < 0 || plainSize > static_cast<int>(MaxPacketSize))
            throw ProtocolError("Decompressed packet size exceeds protocol limit");
        if (plainSize == 0) payload = frame.bytes(frame.remaining());
        else {
            if (plainSize < compression_) throw ProtocolError("Compressed packet is smaller than negotiated threshold");
            payload.resize(static_cast<std::size_t>(plainSize));
            z_stream stream{};
            stream.next_in = const_cast<Bytef*>(frame.current());
            stream.avail_in = static_cast<uInt>(frame.remaining());
            stream.next_out = payload.data();
            stream.avail_out = static_cast<uInt>(payload.size());
            if (inflateInit(&stream) != Z_OK) throw ProtocolError("Could not initialize zlib decoder");
            const int result = inflate(&stream, Z_FINISH);
            const bool valid = result == Z_STREAM_END && stream.total_out == payload.size() && stream.avail_in == 0;
            inflateEnd(&stream);
            if (!valid) throw ProtocolError("Invalid zlib packet or decompressed length mismatch");
        }
    } else payload = frame.bytes(frame.remaining());
    Reader content(payload);
    const auto id = content.varInt();
    if (id < 0) throw ProtocolError("Negative packet ID");
    packet.id = id;
    packet.data = content.bytes(content.remaining());
    offset_ += prefix + size;
    return true;
}
void Framer::setCompression(int threshold) { compression_ = threshold < 0 ? -1 : threshold; }
int Framer::compressionThreshold() const noexcept { return compression_; }
std::size_t Framer::buffered() const noexcept { return buffer_.size() - offset_; }
void Framer::clear() { buffer_.clear(); offset_ = 0; compression_ = -1; }

std::vector<std::uint8_t> encodePacket(std::int32_t id, const std::vector<std::uint8_t>& body, int compressionThreshold) {
    if (id < 0) throw ProtocolError("Negative outgoing packet ID");
    Writer payload;
    payload.varInt(id).bytes(body);
    std::vector<std::uint8_t> framedPayload;
    if (compressionThreshold >= 0) {
        Writer header;
        if (payload.data.size() >= static_cast<std::size_t>(compressionThreshold)) {
            header.varInt(static_cast<std::int32_t>(payload.data.size()));
            uLongf size = compressBound(static_cast<uLong>(payload.data.size()));
            std::vector<std::uint8_t> compressed(static_cast<std::size_t>(size));
            if (compress2(compressed.data(), &size, payload.data.data(), static_cast<uLong>(payload.data.size()),
                    Z_DEFAULT_COMPRESSION) != Z_OK) throw ProtocolError("zlib packet encoding failed");
            framedPayload = std::move(header.data);
            framedPayload.insert(framedPayload.end(), compressed.begin(), compressed.begin() + static_cast<std::ptrdiff_t>(size));
        } else {
            header.varInt(0);
            framedPayload = std::move(header.data);
            framedPayload.insert(framedPayload.end(), payload.data.begin(), payload.data.end());
        }
    } else framedPayload = std::move(payload.data);
    if (framedPayload.empty() || framedPayload.size() > MaxWirePacket) throw ProtocolError("Outgoing frame exceeds 21-bit length limit");
    Writer length;
    length.varInt(static_cast<std::int32_t>(framedPayload.size()));
    auto result = std::move(length.data);
    result.insert(result.end(), framedPayload.begin(), framedPayload.end());
    return result;
}

std::optional<Auth> Auth::fromEnvironment() {
    const char* token = std::getenv("MC_ACCESS_TOKEN");
    const char* uuid = std::getenv("MC_UUID");
    if (!token && !uuid) return std::nullopt;
    return Auth{token ? token : "", uuid ? uuid : ""};
}

ServerAddress resolveServerAddress(const std::string& host, std::uint16_t port, int timeoutMs) {
    ServerAddress original{host, port, false};
    if (port != 25565 || timeoutMs <= 0 || host.empty() || host.size() > 253 || host.find('\0') != std::string::npos)
        return original;
    initializeWinsock();
    in_addr ipv4{};
    in6_addr ipv6{};
    if (inet_pton(AF_INET, host.c_str(), &ipv4) == 1 || inet_pton(AF_INET6, host.c_str(), &ipv6) == 1)
        return original;
    struct SrvJob {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        ServerAddress result;
    };
    auto job = std::make_shared<SrvJob>();
    job->result = original;
    std::thread([job, host] {
        PDNS_RECORDA records = nullptr;
        const auto code = DnsQuery_UTF8(("_minecraft._tcp." + host).c_str(), DNS_TYPE_SRV,
            DNS_QUERY_STANDARD, nullptr, &records, nullptr);
        ServerAddress result = job->result;
        if (code == ERROR_SUCCESS) {
            // Java 1.8.9 takes the first SRV record returned by its resolver.
            for (auto* record = records; record; record = record->pNext) {
                if (record->wType != DNS_TYPE_SRV || !record->Data.SRV.pNameTarget || !record->Data.SRV.wPort) continue;
                result.host = record->Data.SRV.pNameTarget;
                if (result.host.size() > 1 && result.host.back() == '.') result.host.pop_back();
                result.port = record->Data.SRV.wPort;
                result.usedSrv = true;
                break;
            }
        }
        if (records) DnsFree(records, DnsFreeRecordList);
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->result = std::move(result);
            job->done = true;
        }
        job->cv.notify_one();
    }).detach();
    std::unique_lock<std::mutex> lock(job->mutex);
    if (!job->cv.wait_for(lock, std::chrono::milliseconds(std::min(timeoutMs, 60000)), [&] { return job->done; }))
        return original;
    return job->result;
}

struct Client::Impl {
    Socket socket;
    State state = State::Disconnected;
    std::string error, username, uuid;
    std::optional<Auth> auth;
    Framer framer;
    std::deque<std::vector<std::uint8_t>> outgoing;
    std::size_t outgoingOffset = 0, queuedBytes = 0;
    std::uint64_t receivedBytes = 0, sentBytes = 0;
    EncryptionRequestInfo encryptionRequest;
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> encryptContext{nullptr, EVP_CIPHER_CTX_free};
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> decryptContext{nullptr, EVP_CIPHER_CTX_free};
    std::shared_ptr<AuthJob> authJob;
    Clock::time_point loginStarted{}, lastRead{};
#ifdef MC_PROTOCOL_TESTING
    Client::SessionJoinTestHook sessionJoinTestHook;
#endif

    ~Impl() { close(); }

    void close() {
        socket.close();
        outgoing.clear(); outgoingOffset = queuedBytes = 0;
        encryptContext.reset(); decryptContext.reset();
        if (authJob) authJob->cancelled.store(true, std::memory_order_relaxed);
        authJob.reset();
        if (auth) { cleanString(auth->accessToken); auth.reset(); }
    }
    void fail(const std::string& reason) {
        error = reason;
        state = State::Failed;
        close();
    }
    void queue(int id, const std::vector<std::uint8_t>& body) {
        auto bytes = encodePacket(id, body, framer.compressionThreshold());
        if (bytes.size() > MaxQueued - queuedBytes) throw ProtocolError("Outgoing packet queue limit exceeded");
        if (encryptContext) {
            std::vector<std::uint8_t> encrypted(bytes.size() + EVP_MAX_BLOCK_LENGTH);
            int length = 0;
            if (EVP_EncryptUpdate(encryptContext.get(), encrypted.data(), &length, bytes.data(), static_cast<int>(bytes.size())) != 1 ||
                length != static_cast<int>(bytes.size())) throw ProtocolError("AES packet encryption failed");
            encrypted.resize(static_cast<std::size_t>(length));
            bytes = std::move(encrypted);
        }
        queuedBytes += bytes.size();
        outgoing.push_back(std::move(bytes));
    }
    void flush() {
        std::size_t budget = 1024 * 1024;
        while (!outgoing.empty() && socket && budget) {
            auto& bytes = outgoing.front();
            const auto count = std::min(bytes.size() - outgoingOffset, budget);
            const int result = ::send(socket.value, reinterpret_cast<const char*>(bytes.data() + outgoingOffset),
                static_cast<int>(count), 0);
            if (result == SOCKET_ERROR) {
                const int code = WSAGetLastError();
                if (code == WSAEWOULDBLOCK) return;
                throw ProtocolError(socketError("send", code));
            }
            if (result == 0) throw ProtocolError("Server closed while sending");
            outgoingOffset += static_cast<std::size_t>(result);
            queuedBytes -= static_cast<std::size_t>(result);
            budget -= static_cast<std::size_t>(result);
            sentBytes += static_cast<std::uint64_t>(result);
            if (outgoingOffset == bytes.size()) { outgoing.pop_front(); outgoingOffset = 0; }
        }
    }
    void enableEncryption(const std::array<std::uint8_t, 16>& secret) {
        encryptContext.reset(EVP_CIPHER_CTX_new());
        decryptContext.reset(EVP_CIPHER_CTX_new());
        if (!encryptContext || !decryptContext ||
            EVP_EncryptInit_ex(encryptContext.get(), EVP_aes_128_cfb8(), nullptr, secret.data(), secret.data()) != 1 ||
            EVP_DecryptInit_ex(decryptContext.get(), EVP_aes_128_cfb8(), nullptr, secret.data(), secret.data()) != 1)
            throw ProtocolError("Could not initialize AES-128-CFB8");
    }
    void startAuthentication(Reader& packet) {
        if (authJob || encryptContext) throw ProtocolError("Repeated encryption request");
        const auto serverId = packet.string(20);
        const auto publicKey = packet.byteArray(8192);
        const auto verifyToken = packet.byteArray(1024);
        packet.requireEnd();
        encryptionRequest = {true, serverId, publicKey.size(), verifyToken.size()};
        if (!auth) throw ProtocolError("Server requested Minecraft login encryption; a legitimate session is not configured (MC_ACCESS_TOKEN and MC_UUID)");
        const unsigned char* encoded = publicKey.data();
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(d2i_PUBKEY(nullptr, &encoded,
            static_cast<long>(publicKey.size())), EVP_PKEY_free);
        if (!key || encoded != publicKey.data() + publicKey.size() || EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA ||
            EVP_PKEY_bits(key.get()) < 1024 || EVP_PKEY_bits(key.get()) > 8192)
            throw ProtocolError("Invalid RSA public key from server");
        if (verifyToken.empty()) throw ProtocolError("Empty server verification token");
        auto job = std::make_shared<AuthJob>();
        if (RAND_bytes(job->secret.data(), static_cast<int>(job->secret.size())) != 1)
            throw ProtocolError("Cryptographic random key generation failed");
        job->hash = serverHash(serverId, job->secret, publicKey);
        Writer response;
        response.byteArray(rsaEncrypt(key.get(), job->secret.data(), job->secret.size()));
        response.byteArray(rsaEncrypt(key.get(), verifyToken.data(), verifyToken.size()));
        job->response = std::move(response.data);
        job->auth = std::move(*auth); auth.reset();
        authJob = job;
#ifdef MC_PROTOCOL_TESTING
        const auto hook = sessionJoinTestHook;
        std::thread([job, hook] {
#else
        std::thread([job] {
#endif
            try {
                if (!job->cancelled.load(std::memory_order_relaxed)) {
#ifdef MC_PROTOCOL_TESTING
                    job->success = hook ? hook(job->auth, job->hash, job->error) : joinSession(job->auth, job->hash, job->error);
#else
                    job->success = joinSession(job->auth, job->hash, job->error);
#endif
                }
            } catch (...) { job->error = "Minecraft session authorization failed"; }
            cleanString(job->auth.accessToken);
            job->done.store(true, std::memory_order_release);
        }).detach();
    }
    void finishAuthentication() {
        if (!authJob || !authJob->done.load(std::memory_order_acquire)) return;
        const auto job = std::move(authJob);
        if (!job->success) throw ProtocolError(job->error.empty() ? "Minecraft session authorization failed" : job->error);
        // Encryption response itself is plaintext. It stays ahead of all AES
        // bytes in the output queue, even if the socket only writes part of it.
        queue(0x01, job->response);
        enableEncryption(job->secret);
    }
    void handle(Packet packet, std::vector<Packet>& delivered) {
        Reader body(packet.data);
        if (state == State::Login) {
            switch (packet.id) {
            case 0x00:
                error = body.string(32767); body.requireEnd(); state = State::Disconnected; close(); return;
            case 0x01:
                startAuthentication(body); return;
            case 0x02: {
                if (authJob) throw ProtocolError("Login completed before encrypted authentication response");
                const auto profileUuid = body.string(36);
                const auto profileName = body.string(16);
                body.requireEnd();
                normalizedUuid(profileUuid);
                if (!validUsername(profileName)) throw ProtocolError("Invalid username in login response");
                uuid = profileUuid; username = profileName;
                if (auth) { cleanString(auth->accessToken); auth.reset(); }
                state = State::Play; return;
            }
            case 0x03: {
                const auto threshold = body.varInt(); body.requireEnd();
                framer.setCompression(threshold); return;
            }
            default: throw ProtocolError("Unknown login packet ID " + std::to_string(packet.id));
            }
        }
        if (state != State::Play) throw ProtocolError("Packet received outside login/play state");
        if (packet.id == 0x00) {
            const auto token = body.varInt(); body.requireEnd();
            queue(0x00, Writer().varInt(token).data);
            return;
        }
        if (packet.id == 0x46) {
            const auto threshold = body.varInt(); body.requireEnd();
            framer.setCompression(threshold);
            return;
        }
        if (packet.id == 0x40) {
            error = body.string(32767); body.requireEnd();
            delivered.push_back(std::move(packet));
            state = State::Disconnected; close(); return;
        }
        delivered.push_back(std::move(packet));
    }
};

Client::Client() : impl_(new Impl) {}
Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

bool Client::connect(const std::string& host, std::uint16_t port, const std::string& username,
    std::optional<Auth> auth, int timeoutMs) {
    if (!impl_) impl_.reset(new Impl);
    auto& self = *impl_;
    self.close(); self.framer.clear();
    self.error.clear(); self.uuid.clear(); self.username = username;
    self.receivedBytes = self.sentBytes = 0;
    self.encryptionRequest = EncryptionRequestInfo{};
    self.state = State::Connecting;
    try {
        if (timeoutMs <= 0 || timeoutMs > 60000) throw ProtocolError("Connect timeout must be between 1 and 60000 ms");
        if (!validUsername(username)) throw ProtocolError("Minecraft username must contain 1-16 letters, digits or underscores");
        if (auth) {
            if (auth->accessToken.empty() || auth->accessToken.size() > 65536)
                throw ProtocolError("MC_ACCESS_TOKEN is missing or invalid");
            auth->uuid = normalizedUuid(auth->uuid);
            self.auth = std::move(auth);
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        const auto address = resolveServerAddress(host, port, std::min(timeoutMs, 1500));
        self.socket = connectSocket(address.host, address.port, deadline);
        self.loginStarted = self.lastRead = Clock::now();
        self.queue(0x00, handshake(address.host, address.port, 2).data);
        self.queue(0x00, Writer().string(username, 16).data);
        self.state = State::Login;
        self.flush();
        return true;
    } catch (const std::exception& e) { self.fail(e.what()); return false; }
}

std::vector<Packet> Client::poll() {
    std::vector<Packet> delivered;
    if (!impl_ || !impl_->socket || (impl_->state != State::Login && impl_->state != State::Play)) return delivered;
    auto& self = *impl_;
    try {
        self.finishAuthentication(); self.flush();
        std::array<std::uint8_t, 65536> input{};
        std::size_t decodedBytes = 0;
        std::size_t wireBytes = 0;
        std::size_t packetCount = 0;
        bool budgetExhausted = false;
        while (self.socket && !budgetExhausted) {
            Packet packet;
            while (self.framer.next(packet)) {
                decodedBytes += packet.data.size();
                ++packetCount;
                self.handle(std::move(packet), delivered);
                if (!self.socket) break;
                if (decodedBytes >= 8 * 1024 * 1024 || packetCount >= 4096) { budgetExhausted = true; break; }
            }
            if (!self.socket || budgetExhausted || wireBytes >= 2 * 1024 * 1024) break;
            const int size = recv(self.socket.value, reinterpret_cast<char*>(input.data()), static_cast<int>(input.size()), 0);
            if (size == SOCKET_ERROR) {
                const int code = WSAGetLastError();
                if (code == WSAEWOULDBLOCK) break;
                throw ProtocolError(socketError("recv", code));
            }
            if (size == 0) {
                if (self.framer.buffered()) throw ProtocolError("Server closed in the middle of a packet");
                self.error = "Server closed the connection"; self.state = State::Disconnected; self.close(); break;
            }
            self.lastRead = Clock::now();
            self.receivedBytes += static_cast<std::uint64_t>(size);
            wireBytes += static_cast<std::size_t>(size);
            if (self.decryptContext) {
                std::array<std::uint8_t, 65536 + EVP_MAX_BLOCK_LENGTH> plain{};
                int count = 0;
                if (EVP_DecryptUpdate(self.decryptContext.get(), plain.data(), &count, input.data(), size) != 1 || count != size)
                    throw ProtocolError("AES packet decryption failed");
                self.framer.append(plain.data(), static_cast<std::size_t>(count));
            } else self.framer.append(input.data(), static_cast<std::size_t>(size));
        }
        if (self.socket) {
            self.flush();
            const auto now = Clock::now();
            if (self.state == State::Login && now - self.loginStarted > std::chrono::milliseconds(LoginTimeoutMs))
                throw ProtocolError("Minecraft login timed out");
            if (now - self.lastRead > std::chrono::milliseconds(ReadTimeoutMs) && !self.authJob)
                throw ProtocolError("Minecraft server read timed out");
        }
    } catch (const std::exception& e) { self.fail(e.what()); }
    return delivered;
}

bool Client::send(std::int32_t id, const std::vector<std::uint8_t>& body) {
    if (!impl_ || impl_->state != State::Play || !impl_->socket) return false;
    try { impl_->queue(id, body); impl_->flush(); return true; }
    catch (const std::exception& e) { impl_->fail(e.what()); return false; }
}
void Client::disconnect() {
    if (!impl_) return;
    impl_->close(); impl_->framer.clear(); impl_->state = State::Disconnected; impl_->error.clear();
}
State Client::state() const noexcept { return impl_ ? impl_->state : State::Disconnected; }
const std::string& Client::error() const noexcept { static const std::string empty; return impl_ ? impl_->error : empty; }
const std::string& Client::username() const noexcept { static const std::string empty; return impl_ ? impl_->username : empty; }
const std::string& Client::uuid() const noexcept { static const std::string empty; return impl_ ? impl_->uuid : empty; }
int Client::compressionThreshold() const noexcept { return impl_ ? impl_->framer.compressionThreshold() : -1; }
bool Client::encrypted() const noexcept { return impl_ && static_cast<bool>(impl_->encryptContext); }
std::uint64_t Client::bytesReceived() const noexcept { return impl_ ? impl_->receivedBytes : 0; }
std::uint64_t Client::bytesSent() const noexcept { return impl_ ? impl_->sentBytes : 0; }
const EncryptionRequestInfo& Client::encryptionRequest() const noexcept {
    static const EncryptionRequestInfo empty;
    return impl_ ? impl_->encryptionRequest : empty;
}
#ifdef MC_PROTOCOL_TESTING
void Client::setSessionJoinForTests(SessionJoinTestHook hook) { impl_->sessionJoinTestHook = std::move(hook); }
#endif

StatusResult queryStatus(const std::string& host, std::uint16_t port, int timeoutMs) {
    StatusResult result;
    try {
        if (timeoutMs <= 0 || timeoutMs > 60000) throw ProtocolError("Status timeout must be between 1 and 60000 ms");
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        const auto address = resolveServerAddress(host, port, std::min(timeoutMs, 1500));
        auto socket = connectSocket(address.host, address.port, deadline);
        auto outgoing = encodePacket(0x00, handshake(address.host, address.port, 1).data);
        auto request = encodePacket(0x00, {});
        outgoing.insert(outgoing.end(), request.begin(), request.end());
        std::size_t sent = 0;
        Framer framer;
        std::array<std::uint8_t, 65536> input{};
        bool receivedStatus = false, sentPing = false;
        std::int64_t pingPayload = 0;
        Clock::time_point pingStart{};
        while (remainingMs(deadline) > 0) {
            fd_set reads, writes, errors;
            FD_ZERO(&reads); FD_ZERO(&writes); FD_ZERO(&errors);
            FD_SET(socket.value, &reads); FD_SET(socket.value, &errors);
            if (sent < outgoing.size()) FD_SET(socket.value, &writes);
            const int milliseconds = remainingMs(deadline);
            timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
            const int ready = select(0, &reads, &writes, &errors, &timeout);
            if (ready == SOCKET_ERROR) throw ProtocolError(socketError("Status select"));
            if (ready == 0) break;
            if (FD_ISSET(socket.value, &errors)) throw ProtocolError("Status socket reported an error");
            if (FD_ISSET(socket.value, &writes)) {
                const int count = ::send(socket.value, reinterpret_cast<const char*>(outgoing.data() + sent),
                    static_cast<int>(outgoing.size() - sent), 0);
                if (count == SOCKET_ERROR) {
                    const int code = WSAGetLastError();
                    if (code != WSAEWOULDBLOCK) throw ProtocolError(socketError("Status send", code));
                } else if (count == 0) throw ProtocolError("Server closed during status request");
                else sent += static_cast<std::size_t>(count);
            }
            if (FD_ISSET(socket.value, &reads)) {
                const int count = recv(socket.value, reinterpret_cast<char*>(input.data()), static_cast<int>(input.size()), 0);
                if (count == SOCKET_ERROR) {
                    const int code = WSAGetLastError();
                    if (code != WSAEWOULDBLOCK) throw ProtocolError(socketError("Status recv", code));
                } else if (count == 0) throw ProtocolError("Server closed before status ping completed");
                else framer.append(input.data(), static_cast<std::size_t>(count));
            }
            Packet packet;
            while (framer.next(packet)) {
                Reader body(packet.data);
                if (packet.id == 0 && !receivedStatus) {
                    result.json = body.string(32767); body.requireEnd(); receivedStatus = true;
                    pingStart = Clock::now();
                    pingPayload = std::chrono::duration_cast<std::chrono::milliseconds>(pingStart.time_since_epoch()).count();
                    const auto ping = encodePacket(0x01, Writer().i64(pingPayload).data);
                    outgoing.insert(outgoing.end(), ping.begin(), ping.end());
                    sentPing = true;
                } else if (packet.id == 1 && receivedStatus && sentPing) {
                    const auto returned = body.i64(); body.requireEnd();
                    if (returned != pingPayload) throw ProtocolError("Status pong payload does not match ping");
                    result.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - pingStart).count();
                    result.ok = true; return result;
                } else throw ProtocolError("Unexpected server-list status packet");
            }
        }
        throw ProtocolError("Server-list status request timed out");
    } catch (const std::exception& e) { result.error = e.what(); return result; }
}

} // namespace mc::net
