#include "mc/world_io.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>

namespace mc::worldio {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t maxNbtBytes = 64u * 1024 * 1024;
constexpr std::size_t maxLoadedBytes = 512u * 1024 * 1024;

enum class Type : std::uint8_t {
    End,
    Byte,
    Short,
    Int,
    Long,
    Float,
    Double,
    ByteArray,
    String,
    List,
    Compound,
    IntArray,
    LongArray
};
struct Tag {
    Type type{Type::End};
    std::int64_t integer{};
    double real{};
    std::string string;
    Bytes bytes;
    Type listType{Type::End};
    std::vector<Tag> list;
    std::map<std::string, Tag> compound;
    std::vector<std::int32_t> ints;
    std::vector<std::int64_t> longs;
    static Tag number(Type kind, std::int64_t value) {
        Tag t;
        t.type = kind;
        t.integer = value;
        return t;
    }
    static Tag floating(Type kind, double value) {
        Tag t;
        t.type = kind;
        t.real = value;
        return t;
    }
    static Tag text(std::string value) {
        Tag t;
        t.type = Type::String;
        t.string = std::move(value);
        return t;
    }
    static Tag object() {
        Tag t;
        t.type = Type::Compound;
        return t;
    }
    static Tag array(Bytes value) {
        Tag t;
        t.type = Type::ByteArray;
        t.bytes = std::move(value);
        return t;
    }
    static Tag sequence(Type element) {
        Tag t;
        t.type = Type::List;
        t.listType = element;
        return t;
    }
    const Tag *find(const std::string &name) const {
        const auto found = compound.find(name);
        return type == Type::Compound && found != compound.end() ? &found->second : nullptr;
    }
};
struct Document {
    std::string name;
    Tag root;
};
struct TileUpdate {
    int x{}, y{}, z{};
    Tag data;
};
using TileUpdates = std::unordered_map<std::uint64_t, std::vector<TileUpdate>>;

[[noreturn]] void bad(const std::string &message) { throw std::runtime_error(message); }
std::int64_t signed64(std::uint64_t bits) {
    std::int64_t value;
    std::memcpy(&value, &bits, 8);
    return value;
}

void utf8Append(std::string &text, std::uint32_t code) {
    if (code < 0x80)
        text.push_back(static_cast<char>(code));
    else if (code < 0x800) {
        text.push_back(static_cast<char>(0xc0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 63)));
    } else if (code < 0x10000) {
        text.push_back(static_cast<char>(0xe0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        text.push_back(static_cast<char>(0x80 | (code & 63)));
    } else {
        text.push_back(static_cast<char>(0xf0 | (code >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
        text.push_back(static_cast<char>(0x80 | (code & 63)));
    }
}
std::string readModifiedUtf8(const std::uint8_t *data, std::size_t size) {
    std::vector<std::uint16_t> units;
    units.reserve(size);
    for (std::size_t i = 0; i < size;) {
        unsigned c = data[i++], value = c;
        if ((c & 0xe0) == 0xc0) {
            if (i >= size || (data[i] & 0xc0) != 0x80)
                bad("invalid NBT modified UTF-8");
            value = ((c & 31) << 6) | (data[i++] & 63);
        } else if ((c & 0xf0) == 0xe0) {
            if (i + 1 >= size || (data[i] & 0xc0) != 0x80 || (data[i + 1] & 0xc0) != 0x80)
                bad("invalid NBT modified UTF-8");
            value = ((c & 15) << 12) | ((data[i] & 63) << 6) | (data[i + 1] & 63);
            i += 2;
        } else if (c >= 0x80)
            bad("invalid NBT modified UTF-8 lead byte");
        units.push_back(static_cast<std::uint16_t>(value));
    }
    std::string result;
    for (std::size_t i = 0; i < units.size(); ++i) {
        unsigned code = units[i];
        if (code >= 0xd800 && code <= 0xdbff && i + 1 < units.size() && units[i + 1] >= 0xdc00 &&
            units[i + 1] <= 0xdfff)
            code = 0x10000 + ((code - 0xd800) << 10) + (units[++i] - 0xdc00);
        // Unpaired UTF-16 surrogates remain losslessly represented as WTF-8.
        utf8Append(result, code);
    }
    return result;
}
Bytes writeModifiedUtf8(const std::string &text) {
    Bytes result;
    const auto unit = [&](unsigned c) {
        if (c > 0 && c < 0x80)
            result.push_back(static_cast<std::uint8_t>(c));
        else if (c < 0x800) {
            result.push_back(static_cast<std::uint8_t>(0xc0 | (c >> 6)));
            result.push_back(static_cast<std::uint8_t>(0x80 | (c & 63)));
        } else {
            result.push_back(static_cast<std::uint8_t>(0xe0 | (c >> 12)));
            result.push_back(static_cast<std::uint8_t>(0x80 | ((c >> 6) & 63)));
            result.push_back(static_cast<std::uint8_t>(0x80 | (c & 63)));
        }
    };
    for (std::size_t i = 0; i < text.size();) {
        const unsigned c = static_cast<unsigned char>(text[i++]);
        unsigned code = c, tails = 0;
        if ((c & 0xe0) == 0xc0) {
            code = c & 31;
            tails = 1;
        } else if ((c & 0xf0) == 0xe0) {
            code = c & 15;
            tails = 2;
        } else if ((c & 0xf8) == 0xf0) {
            code = c & 7;
            tails = 3;
        } else if (c >= 0x80)
            bad("invalid UTF-8 text for NBT");
        if (i + tails > text.size())
            bad("truncated UTF-8 text for NBT");
        for (unsigned n = 0; n < tails; ++n) {
            const unsigned next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                bad("invalid UTF-8 continuation for NBT");
            code = (code << 6) | (next & 63);
        }
        if (code > 0x10ffff)
            bad("out-of-range Unicode in NBT");
        if (code >= 0x10000) {
            code -= 0x10000;
            unit(0xd800 | (code >> 10));
            unit(0xdc00 | (code & 1023));
        } else
            unit(code);
    }
    if (result.size() > 65535)
        bad("NBT string exceeds 65535 encoded bytes");
    return result;
}

class NbtReader {
    const Bytes &bytes_;
    std::size_t pos_{}, nodes_{};
    void need(std::size_t size) {
        if (size > bytes_.size() - pos_)
            bad("truncated NBT payload");
    }
    std::uint8_t byte() {
        need(1);
        return bytes_[pos_++];
    }
    std::uint16_t shortBits() {
        std::uint16_t v = byte();
        return static_cast<std::uint16_t>((v << 8) | byte());
    }
    std::uint32_t intBits() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value = (value << 8) | byte();
        return value;
    }
    std::uint64_t longBits() {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value = (value << 8) | byte();
        return value;
    }
    std::size_t length(std::size_t elementBytes) {
        const std::uint32_t value = intBits();
        if (value > 0x7fffffff || value > maxNbtBytes / elementBytes)
            bad("invalid NBT array/list length");
        need(static_cast<std::size_t>(value) * elementBytes);
        return value;
    }
    std::string text() {
        const auto size = shortBits();
        need(size);
        auto s = readModifiedUtf8(bytes_.data() + pos_, size);
        pos_ += size;
        return s;
    }
    Tag payload(Type type, unsigned depth) {
        if (depth > 64 || ++nodes_ > 500000)
            bad("NBT nesting or node count exceeds limit");
        if (type == Type::End || static_cast<unsigned>(type) > 12)
            bad("invalid NBT tag type");
        Tag result;
        result.type = type;
        switch (type) {
        case Type::Byte:
            result.integer = static_cast<std::int8_t>(byte());
            break;
        case Type::Short:
            result.integer = static_cast<std::int16_t>(shortBits());
            break;
        case Type::Int:
            result.integer = static_cast<std::int32_t>(intBits());
            break;
        case Type::Long:
            result.integer = signed64(longBits());
            break;
        case Type::Float: {
            const auto bits = intBits();
            float value;
            std::memcpy(&value, &bits, 4);
            result.real = value;
            break;
        }
        case Type::Double: {
            const auto bits = longBits();
            std::memcpy(&result.real, &bits, 8);
            break;
        }
        case Type::ByteArray: {
            const auto count = length(1);
            result.bytes.assign(bytes_.begin() + pos_, bytes_.begin() + pos_ + count);
            pos_ += count;
            break;
        }
        case Type::String:
            result.string = text();
            break;
        case Type::List: {
            result.listType = static_cast<Type>(byte());
            const auto count = length(1);
            if ((result.listType == Type::End && count != 0) || static_cast<unsigned>(result.listType) > 12 ||
                count > 500000 - nodes_)
                bad("invalid NBT list type or excessive element count");
            result.list.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                result.list.push_back(payload(result.listType, depth + 1));
            break;
        }
        case Type::Compound:
            while (true) {
                const Type child = static_cast<Type>(byte());
                if (child == Type::End)
                    break;
                const auto name = text();
                result.compound[name] = payload(child, depth + 1);
            }
            break;
        case Type::IntArray: {
            const auto count = length(4);
            result.ints.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                result.ints.push_back(static_cast<std::int32_t>(intBits()));
            break;
        }
        case Type::LongArray: {
            const auto count = length(8);
            result.longs.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                result.longs.push_back(signed64(longBits()));
            break;
        }
        default:
            bad("unsupported NBT tag");
        }
        return result;
    }

  public:
    explicit NbtReader(const Bytes &bytes) : bytes_(bytes) {}
    Document read() {
        const auto type = static_cast<Type>(byte());
        Document result;
        result.name = text();
        result.root = payload(type, 0);
        if (pos_ != bytes_.size())
            bad("NBT payload has trailing bytes");
        return result;
    }
};

class NbtWriter {
    Bytes bytes_;
    void byte(std::uint8_t value) {
        if (bytes_.size() >= maxNbtBytes)
            bad("encoded NBT exceeds limit");
        bytes_.push_back(value);
    }
    void shortBits(std::uint16_t value) {
        byte(static_cast<std::uint8_t>(value >> 8));
        byte(static_cast<std::uint8_t>(value));
    }
    void intBits(std::uint32_t value) {
        for (int i = 3; i >= 0; --i)
            byte(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void longBits(std::uint64_t value) {
        for (int i = 7; i >= 0; --i)
            byte(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void text(const std::string &text) {
        const auto encoded = writeModifiedUtf8(text);
        shortBits(static_cast<std::uint16_t>(encoded.size()));
        for (auto v : encoded)
            byte(v);
    }
    void payload(const Tag &tag, unsigned depth) {
        if (depth > 64)
            bad("encoded NBT exceeds nesting limit");
        switch (tag.type) {
        case Type::Byte:
            byte(static_cast<std::uint8_t>(tag.integer));
            break;
        case Type::Short:
            shortBits(static_cast<std::uint16_t>(tag.integer));
            break;
        case Type::Int:
            intBits(static_cast<std::uint32_t>(tag.integer));
            break;
        case Type::Long:
            longBits(static_cast<std::uint64_t>(tag.integer));
            break;
        case Type::Float: {
            const float value = static_cast<float>(tag.real);
            std::uint32_t bits;
            std::memcpy(&bits, &value, 4);
            intBits(bits);
            break;
        }
        case Type::Double: {
            std::uint64_t bits;
            std::memcpy(&bits, &tag.real, 8);
            longBits(bits);
            break;
        }
        case Type::ByteArray:
            intBits(static_cast<std::uint32_t>(tag.bytes.size()));
            for (auto v : tag.bytes)
                byte(v);
            break;
        case Type::String:
            text(tag.string);
            break;
        case Type::List:
            byte(static_cast<std::uint8_t>(tag.listType));
            intBits(static_cast<std::uint32_t>(tag.list.size()));
            for (const auto &value : tag.list) {
                if (value.type != tag.listType)
                    bad("heterogeneous NBT list");
                payload(value, depth + 1);
            }
            break;
        case Type::Compound:
            for (const auto &pair : tag.compound) {
                byte(static_cast<std::uint8_t>(pair.second.type));
                text(pair.first);
                payload(pair.second, depth + 1);
            }
            byte(0);
            break;
        case Type::IntArray:
            intBits(static_cast<std::uint32_t>(tag.ints.size()));
            for (auto v : tag.ints)
                intBits(static_cast<std::uint32_t>(v));
            break;
        case Type::LongArray:
            intBits(static_cast<std::uint32_t>(tag.longs.size()));
            for (auto v : tag.longs)
                longBits(static_cast<std::uint64_t>(v));
            break;
        default:
            bad("cannot encode NBT end tag as a value");
        }
    }

  public:
    Bytes write(const Document &doc) {
        byte(static_cast<std::uint8_t>(doc.root.type));
        text(doc.name);
        payload(doc.root, 0);
        return std::move(bytes_);
    }
};

Tag nativeTag(const nbt::Tag &value, unsigned depth, std::size_t &nodes) {
    if (depth > 64 || ++nodes > 500000)
        bad("native tile NBT exceeds depth/node limits");
    if (value.type <= 0 || value.type > 11)
        bad("invalid native tile NBT type");
    Tag result;
    result.type = static_cast<Type>(value.type);
    if (value.type >= 1 && value.type <= 4) {
        if (value.type == 1 && (value.integer < -128 || value.integer > 127))
            bad("native NBT byte is out of range");
        if (value.type == 2 && (value.integer < -32768 || value.integer > 32767))
            bad("native NBT short is out of range");
        if (value.type == 3 && (value.integer < std::numeric_limits<std::int32_t>::min() ||
                                value.integer > std::numeric_limits<std::int32_t>::max()))
            bad("native NBT int is out of range");
        result.integer = value.integer;
    } else if (value.type == 5 || value.type == 6)
        result.real = value.number;
    else if (value.type == 7) {
        if (value.bytes.size() > maxNbtBytes)
            bad("native NBT byte array is too large");
        result.bytes = value.bytes;
    } else if (value.type == 8) {
        if (value.string.size() > 65535)
            bad("native modified UTF-8 string is too long");
        result.string =
            readModifiedUtf8(reinterpret_cast<const std::uint8_t *>(value.string.data()), value.string.size());
    } else if (value.type == 9) {
        if (value.subtype < 0 || value.subtype > 11 || (!value.list.empty() && value.subtype == 0))
            bad("invalid native NBT list subtype");
        result.listType = static_cast<Type>(value.subtype);
        if (value.list.size() > 500000 - nodes)
            bad("native NBT list is too large");
        result.list.reserve(value.list.size());
        for (const auto &entry : value.list) {
            if (entry.type != value.subtype)
                bad("native tile NBT list is heterogeneous");
            result.list.push_back(nativeTag(entry, depth + 1, nodes));
        }
    } else if (value.type == 10) {
        for (const auto &entry : value.compound) {
            if (entry.first.size() > 65535)
                bad("native NBT key is too long");
            const auto name =
                readModifiedUtf8(reinterpret_cast<const std::uint8_t *>(entry.first.data()), entry.first.size());
            result.compound[name] = nativeTag(entry.second, depth + 1, nodes);
        }
    } else if (value.type == 11) {
        if (value.bytes.size() % 4 || value.bytes.size() > maxNbtBytes)
            bad("invalid native NBT int array");
        for (std::size_t i = 0; i < value.bytes.size(); i += 4) {
            const std::uint32_t bits = (std::uint32_t(value.bytes[i]) << 24) |
                                       (std::uint32_t(value.bytes[i + 1]) << 16) |
                                       (std::uint32_t(value.bytes[i + 2]) << 8) | value.bytes[i + 3];
            std::int32_t element;
            std::memcpy(&element, &bits, 4);
            result.ints.push_back(element);
        }
    }
    return result;
}

const Tag &required(const Tag &parent, const std::string &name, Type type) {
    const Tag *value = parent.find(name);
    if (!value || value->type != type)
        bad("missing or wrong NBT type for " + name);
    return *value;
}
std::int64_t integer(const Tag &parent, const std::string &name, std::int64_t fallback = 0) {
    const Tag *value = parent.find(name);
    if (!value)
        return fallback;
    if (value->type < Type::Byte || value->type > Type::Long)
        bad("expected integer NBT tag " + name);
    return value->integer;
}
double number(const Tag &parent, const std::string &name, double fallback = 0) {
    const Tag *value = parent.find(name);
    if (!value)
        return fallback;
    if (value->type >= Type::Byte && value->type <= Type::Long)
        return static_cast<double>(value->integer);
    if (value->type == Type::Float || value->type == Type::Double)
        return value->real;
    bad("expected numeric NBT tag " + name);
}
std::string string(const Tag &parent, const std::string &name, const std::string &fallback) {
    const auto value = parent.find(name);
    return value ? required(parent, name, Type::String).string : fallback;
}

Bytes readFile(const fs::path &path, std::size_t maximum = maxNbtBytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        bad("cannot read " + path.u8string());
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > maximum)
        bad("file exceeds limit: " + path.u8string());
    Bytes result(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!result.empty() &&
        !input.read(reinterpret_cast<char *>(result.data()), static_cast<std::streamsize>(result.size())))
        bad("short read: " + path.u8string());
    return result;
}
void writeFile(const fs::path &path, const Bytes &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || (!bytes.empty() && !output.write(reinterpret_cast<const char *>(bytes.data()),
                                                    static_cast<std::streamsize>(bytes.size()))))
        bad("cannot write " + path.u8string());
    output.close();
    if (!output)
        bad("cannot finish " + path.u8string());
}

Bytes inflateBytes(const Bytes &bytes, int windowBits) {
    if (bytes.size() > std::numeric_limits<uInt>::max())
        bad("compressed NBT exceeds limit");
    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK)
        bad("cannot initialize zlib decoder");
    struct Cleanup {
        z_stream *s;
        ~Cleanup() { inflateEnd(s); }
    } cleanup{&stream};
    stream.next_in = const_cast<Bytef *>(bytes.data());
    stream.avail_in = static_cast<uInt>(bytes.size());
    Bytes result;
    std::array<std::uint8_t, 32768> buffer{};
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
            bad("invalid or truncated compressed NBT stream");
        const auto count = buffer.size() - stream.avail_out;
        if (count > maxNbtBytes - result.size())
            bad("decompressed NBT exceeds 64 MiB limit");
        result.insert(result.end(), buffer.begin(), buffer.begin() + count);
        if (count == 0 && status != Z_STREAM_END && stream.avail_in == 0)
            bad("truncated compressed NBT stream");
    }
    if (stream.avail_in != 0)
        bad("compressed NBT stream has trailing bytes");
    return result;
}
Bytes deflateBytes(const Bytes &bytes, int windowBits) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        bad("cannot initialize zlib encoder");
    struct Cleanup {
        z_stream *s;
        ~Cleanup() { deflateEnd(s); }
    } cleanup{&stream};
    stream.next_in = const_cast<Bytef *>(bytes.data());
    stream.avail_in = static_cast<uInt>(bytes.size());
    Bytes result;
    std::array<std::uint8_t, 32768> buffer{};
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END)
            bad("zlib encoding failed");
        result.insert(result.end(), buffer.begin(), buffer.begin() + buffer.size() - stream.avail_out);
    }
    return result;
}
std::uint32_t big32(const std::uint8_t *data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
}
void setBig32(std::uint8_t *data, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        data[i] = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
}
std::uint64_t chunkKey(int x, int z) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z);
}
int floorDiv32(int value) { return value / 32 - (value % 32 < 0 ? 1 : 0); }

class RegionReader {
    std::ifstream file_;
    std::array<std::uint8_t, 4096> locations_{};
    std::uint64_t size_{};

  public:
    explicit RegionReader(const fs::path &path) : file_(path, std::ios::binary | std::ios::ate) {
        if (!file_)
            bad("cannot read region " + path.u8string());
        const auto end = file_.tellg();
        if (end < 8192)
            bad("truncated region header: " + path.u8string());
        size_ = static_cast<std::uint64_t>(end);
        file_.seekg(0);
        if (!file_.read(reinterpret_cast<char *>(locations_.data()), locations_.size()))
            bad("truncated region locations");
        std::vector<std::pair<std::uint32_t, std::uint32_t>> allocated;
        for (int slot = 0; slot < 1024; ++slot) {
            const auto location = big32(locations_.data() + slot * 4);
            if (!location)
                continue;
            const std::uint32_t offset = location >> 8, sectors = location & 255;
            if (offset < 2 || sectors == 0 || (static_cast<std::uint64_t>(offset) + sectors) * 4096 > size_)
                bad("invalid region sector allocation");
            allocated.emplace_back(offset, offset + sectors);
        }
        std::sort(allocated.begin(), allocated.end());
        for (std::size_t i = 1; i < allocated.size(); ++i)
            if (allocated[i].first < allocated[i - 1].second)
                bad("overlapping region sector allocations");
    }
    bool read(int x, int z, Bytes &result) {
        const unsigned slot = static_cast<unsigned>(x & 31) + static_cast<unsigned>(z & 31) * 32;
        const auto location = big32(locations_.data() + slot * 4);
        if (!location)
            return false;
        file_.clear();
        file_.seekg(static_cast<std::uint64_t>(location >> 8) * 4096);
        std::array<std::uint8_t, 5> header{};
        if (!file_.read(reinterpret_cast<char *>(header.data()), header.size()))
            bad("truncated region chunk header");
        const std::uint32_t length = big32(header.data());
        if (length < 1 || length > (location & 255) * 4096 - 4)
            bad("region chunk length exceeds allocated sectors");
        Bytes encoded(length - 1);
        if (!encoded.empty() && !file_.read(reinterpret_cast<char *>(encoded.data()), encoded.size()))
            bad("truncated region chunk payload");
        if (header[4] == 1)
            result = inflateBytes(encoded, 31);
        else if (header[4] == 2)
            result = inflateBytes(encoded, 15);
        else if (header[4] == 3)
            result = std::move(encoded);
        else
            bad("unsupported region compression (expected gzip, zlib or uncompressed legacy NBT)");
        return true;
    }
};

fs::path dimensionPath(const fs::path &directory, int dimension) {
    if (dimension == 0)
        return directory;
    if (dimension == -1)
        return directory / "DIM-1";
    if (dimension == 1)
        return directory / "DIM1";
    bad("unsupported save dimension (only vanilla -1, 0, 1)");
}
void loadColumn(World &world, const Document &doc, int cx, int cz, bool skyLight) {
    const Tag &level = required(doc.root, "Level", Type::Compound);
    if (integer(level, "xPos", cx) != cx || integer(level, "zPos", cz) != cz)
        bad("chunk NBT coordinates differ from region slot");
    const Tag &sections = required(level, "Sections", Type::List);
    if (sections.listType != Type::Compound && !sections.list.empty())
        bad("chunk Sections list must contain compounds");
    std::array<const Tag *, 16> ordered{};
    std::uint16_t mask = 0;
    for (const auto &section : sections.list) {
        const auto y = integer(section, "Y", -1);
        if (y < 0 || y >= 16 || ordered[static_cast<std::size_t>(y)])
            bad("invalid or duplicate Anvil section Y");
        ordered[static_cast<std::size_t>(y)] = &section;
        mask |= static_cast<std::uint16_t>(1u << y);
    }
    const auto array = [](const Tag &section, const std::string &name, std::size_t size) -> const Bytes & {
        const auto &bytes = required(section, name, Type::ByteArray).bytes;
        if (bytes.size() != size)
            bad("invalid Anvil " + name + " byte count");
        return bytes;
    };
    Bytes wire;
    wire.reserve(World::chunkDataSize(mask, skyLight, true));
    for (const auto section : ordered)
        if (section) {
            const auto &blocks = array(*section, "Blocks", 4096);
            const auto &data = array(*section, "Data", 2048);
            const Tag *add = section->find("Add");
            if (add && (add->type != Type::ByteArray || add->bytes.size() != 2048))
                bad("invalid Anvil Add byte count");
            for (std::size_t i = 0; i < 4096; ++i) {
                const unsigned shift = (i & 1) * 4;
                const unsigned high = add ? (add->bytes[i >> 1] >> shift) & 15 : 0;
                const unsigned state = ((blocks[i] | (high << 8)) << 4) | ((data[i >> 1] >> shift) & 15);
                wire.push_back(static_cast<std::uint8_t>(state));
                wire.push_back(static_cast<std::uint8_t>(state >> 8));
            }
        }
    for (const auto section : ordered)
        if (section) {
            const auto &light = array(*section, "BlockLight", 2048);
            wire.insert(wire.end(), light.begin(), light.end());
        }
    if (skyLight)
        for (const auto section : ordered)
            if (section) {
                const auto &light = array(*section, "SkyLight", 2048);
                wire.insert(wire.end(), light.begin(), light.end());
            }
    if (const Tag *biomes = level.find("Biomes")) {
        if (biomes->type != Type::ByteArray || biomes->bytes.size() != 256)
            bad("invalid Anvil Biomes byte count");
        wire.insert(wire.end(), biomes->bytes.begin(), biomes->bytes.end());
    } else
        wire.insert(wire.end(), 256, 1);
    std::string error;
    if (!world.applyBulk({{cx, cz, mask}}, skyLight, wire.data(), wire.size(), &error))
        bad(error);
}
nbt::Tag exportedTag(const Tag &value) {
    Document doc;
    // The wire-NBT parser accepts a compound root. Wrap scalar/list level.dat
    // members so their original types survive this bridge as well.
    doc.root = Tag::object();
    doc.root.compound["value"] = value;
    auto root = nbt::parse(NbtWriter().write(doc));
    return std::move(root.compound.at("value"));
}
void collectTiles(const Document &doc, int cx, int cz, std::vector<worldgen::BlockEntityUpdate> &out) {
    const auto &level = required(doc.root, "Level", Type::Compound);
    const auto *tiles = level.find("TileEntities");
    if (!tiles)
        return;
    if (tiles->type != Type::List || (!tiles->list.empty() && tiles->listType != Type::Compound))
        bad("invalid TileEntities list");
    for (const auto &tile : tiles->list) {
        int x = int(integer(tile, "x", 0)), y = int(integer(tile, "y", 0)), z = int(integer(tile, "z", 0));
        if (floorDiv16(x) != cx || floorDiv16(z) != cz || y < 0 || y > 255)
            continue;
        if (out.size() >= 65536)
            bad("too many loaded block entities");
        out.push_back({x, y, z, exportedTag(tile)});
    }
}
} // namespace

struct SaveMetadata {
    Document level;
    std::unordered_map<std::uint64_t, Document> chunks;
    std::filesystem::path sourceDirectory;
    std::size_t loadedBytes{};
};

bool loadWorld(const std::string &directory, World &destination, SaveInfo &info, int radiusChunks, std::string *error) {
    if (error)
        error->clear();
    try {
        if (radiusChunks < 0 || radiusChunks > 32)
            bad("world import radius must be between 0 and 32 chunks");
        const fs::path base = fs::weakly_canonical(fs::absolute(fs::u8path(directory)));
        auto metadata = std::make_shared<SaveMetadata>();
        metadata->sourceDirectory = base;
        Bytes inflated = inflateBytes(readFile(base / "level.dat", 16u * 1024 * 1024), 31);
        std::size_t loadedBytes = inflated.size();
        metadata->level = NbtReader(inflated).read();
        const Tag &data = required(metadata->level.root, "Data", Type::Compound);
        if (integer(data, "version", 19133) != 19133)
            bad("only Anvil (version 19133) saves are supported");
        if (integer(data, "DataVersion", 0) >= 1451)
            bad("flattened/newer Minecraft world format is not supported");
        SaveInfo loaded;
        loaded.levelName = string(data, "LevelName", base.filename().u8string());
        loaded.seed = integer(data, "RandomSeed", 0);
        loaded.gameType = int(integer(data, "GameType", 1));
        nbt::Tag levelState;
        levelState.type = 10;
        for (const char *name : {"Time", "DayTime", "raining", "rainTime", "thundering", "thunderTime",
                                 "clearWeatherTime", "Difficulty", "GameRules"})
            if (const auto *field = data.find(name))
                levelState.compound[name] = exportedTag(*field);
        loaded.levelUpdates = std::move(levelState);
        if (integer(data, "NativeVoxelBaseGeneration", 0) == 1) {
            GenerationInfo generation;
            auto type = string(data, "generatorName", "default");
            generation.type = type == "largeBiomes" ? worldgen::WorldType::LargeBiomes
                              : type == "amplified" ? worldgen::WorldType::Amplified
                                                    : worldgen::WorldType::Default;
            generation.mapFeatures = integer(data, "MapFeatures", 0) != 0;
            generation.gameType = int(integer(data, "GameType", 1));
            generation.allowCommands = integer(data, "allowCommands", 0) != 0;
            loaded.generation = generation;
        }
        loaded.spawnX = static_cast<int>(integer(data, "SpawnX", 0));
        loaded.spawnY = static_cast<int>(integer(data, "SpawnY", 64));
        loaded.spawnZ = static_cast<int>(integer(data, "SpawnZ", 0));
        loaded.x = loaded.spawnX + .5;
        loaded.y = loaded.spawnY;
        loaded.z = loaded.spawnZ + .5;
        if (const Tag *player = data.find("Player")) {
            if (player->type != Type::Compound)
                bad("Player NBT must be a compound");
            const auto &pos = required(*player, "Pos", Type::List);
            const auto &rotation = required(*player, "Rotation", Type::List);
            if (pos.listType != Type::Double || pos.list.size() != 3 || rotation.listType != Type::Float ||
                rotation.list.size() != 2)
                bad("invalid saved player position/rotation lists");
            loaded.x = pos.list[0].real;
            loaded.y = pos.list[1].real;
            loaded.z = pos.list[2].real;
            loaded.yaw = static_cast<float>(rotation.list[0].real);
            loaded.pitch = static_cast<float>(rotation.list[1].real);
            loaded.dimension = static_cast<int>(integer(*player, "Dimension", 0));
            loaded.hasPlayer = true;
            loaded.playerData = exportedTag(*player);
        }
        if (!std::isfinite(loaded.x) || !std::isfinite(loaded.y) || !std::isfinite(loaded.z) ||
            !std::isfinite(loaded.yaw) || !std::isfinite(loaded.pitch) || std::abs(loaded.x) >= 30000000 ||
            std::abs(loaded.z) >= 30000000 || std::abs(loaded.y) > 32000000)
            bad("saved player/spawn is non-finite or out of world bounds");
        World candidate;
        candidate.setWorldBorder(number(data, "BorderCenterX"), number(data, "BorderCenterZ"),
                                 number(data, "BorderSize", 60000000.0));
        const fs::path regions = dimensionPath(base, loaded.dimension) / "region";
        const int centerX = floorDiv16(floorBlock(loaded.x)), centerZ = floorDiv16(floorBlock(loaded.z));
        std::map<std::pair<int, int>, std::unique_ptr<RegionReader>> readers;
        for (int cz = centerZ - radiusChunks; cz <= centerZ + radiusChunks; ++cz)
            for (int cx = centerX - radiusChunks; cx <= centerX + radiusChunks; ++cx) {
                const int rx = floorDiv32(cx), rz = floorDiv32(cz);
                const auto key = std::make_pair(rx, rz);
                auto found = readers.find(key);
                if (found == readers.end()) {
                    const fs::path path = regions / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca");
                    std::unique_ptr<RegionReader> reader;
                    if (fs::exists(path))
                        reader = std::make_unique<RegionReader>(path);
                    found = readers.emplace(key, std::move(reader)).first;
                }
                if (!found->second)
                    continue;
                Bytes bytes;
                if (!found->second->read(cx, cz, bytes))
                    continue;
                if (bytes.size() > maxLoadedBytes - loadedBytes)
                    bad("world import exceeds 512 MiB NBT memory budget; reduce radius");
                loadedBytes += bytes.size();
                auto doc = NbtReader(bytes).read();
                loadColumn(candidate, doc, cx, cz, loaded.dimension == 0);
                collectTiles(doc, cx, cz, loaded.blockEntityUpdates);
                metadata->chunks.emplace(chunkKey(cx, cz), std::move(doc));
                ++loaded.loadedChunks;
            }
        if (loaded.loadedChunks == 0)
            bad("no saved Anvil chunks exist around the selected player/spawn position");
        metadata->loadedBytes = loadedBytes;
        loaded.metadata = std::move(metadata);
        destination = std::move(candidate);
        info = std::move(loaded);
        return true;
    } catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

bool loadChunk(World &destination, SaveInfo &info, int cx, int cz, bool &found, std::string *error) {
    found = false;
    if (error)
        error->clear();
    if (!info.metadata)
        return true;
    try {
        if (cx < -1875000 || cx >= 1875000 || cz < -1875000 || cz >= 1875000)
            bad("chunk coordinate outside world bounds");
        auto &metadata = *info.metadata;
        const auto path = dimensionPath(metadata.sourceDirectory, info.dimension) / "region" /
                          ("r." + std::to_string(floorDiv32(cx)) + "." + std::to_string(floorDiv32(cz)) + ".mca");
        if (!fs::exists(path))
            return true;
        RegionReader reader(path);
        Bytes bytes;
        if (!reader.read(cx, cz, bytes))
            return true;
        if (bytes.size() > maxLoadedBytes - std::min(metadata.loadedBytes, maxLoadedBytes))
            bad("loaded saved chunks exceed NBT budget");
        auto doc = NbtReader(bytes).read();
        std::vector<worldgen::BlockEntityUpdate> tiles;
        collectTiles(doc, cx, cz, tiles);
        loadColumn(destination, doc, cx, cz, info.dimension == 0);
        metadata.loadedBytes += bytes.size();
        metadata.chunks[chunkKey(cx, cz)] = std::move(doc);
        info.blockEntityUpdates.insert(info.blockEntityUpdates.end(), std::make_move_iterator(tiles.begin()),
                                       std::make_move_iterator(tiles.end()));
        ++info.loadedChunks;
        found = true;
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = e.what();
        return false;
    }
}

namespace {
TileUpdates tileUpdates(const SaveInfo &info) {
    TileUpdates result;
    for (const auto &update : info.blockEntityUpdates) {
        if (update.x < -30000000 || update.x >= 30000000 || update.z < -30000000 || update.z >= 30000000 ||
            update.y < 0 || update.y >= 256)
            bad("tile update position is outside world bounds");
        Tag data;
        if (update.data.type) {
            if (update.data.type != 10)
                bad("tile update must be an NBT compound or TAG_End");
            std::size_t nodes = 0;
            data = nativeTag(update.data, 0, nodes);
            const auto &name = required(data, "id", Type::String);
            if (name.string.empty())
                bad("tile update id must be nonempty");
            if (required(data, "x", Type::Int).integer != update.x ||
                required(data, "y", Type::Int).integer != update.y ||
                required(data, "z", Type::Int).integer != update.z)
                bad("tile update NBT coordinates disagree with its position");
        }
        result[chunkKey(floorDiv16(update.x), floorDiv16(update.z))].push_back(
            {update.x, update.y, update.z, std::move(data)});
    }
    return result;
}
void mergeTiles(Tag &level, const std::vector<TileUpdate> &updates) {
    auto &list = level.compound["TileEntities"];
    if (list.type == Type::End)
        list = Tag::sequence(Type::End);
    if (list.type != Type::List || (!list.list.empty() && list.listType != Type::Compound))
        bad("retained TileEntities is not a compound list");
    for (const auto &update : updates) {
        auto matches = [&](const Tag &tile) {
            const auto *x = tile.find("x");
            const auto *y = tile.find("y");
            const auto *z = tile.find("z");
            return x && y && z && x->type >= Type::Byte && x->type <= Type::Long && y->type >= Type::Byte &&
                   y->type <= Type::Long && z->type >= Type::Byte && z->type <= Type::Long && x->integer == update.x &&
                   y->integer == update.y && z->integer == update.z;
        };
        auto found = std::find_if(list.list.begin(), list.list.end(), matches);
        if (update.data.type == Type::End)
            list.list.erase(std::remove_if(list.list.begin(), list.list.end(), matches), list.list.end());
        else if (found == list.list.end())
            list.list.push_back(update.data);
        else {
            *found = update.data;
            list.list.erase(std::remove_if(found + 1, list.list.end(), matches), list.list.end());
        }
    }
    list.listType = list.list.empty() ? Type::End : Type::Compound;
}
Tag &objectMember(Tag &parent, const std::string &name) {
    if (parent.type != Type::Compound)
        bad("cannot update a non-compound NBT root");
    auto &result = parent.compound[name];
    if (result.type == Type::End)
        result = Tag::object();
    if (result.type != Type::Compound)
        bad("expected compound NBT member " + name);
    return result;
}
void listIfMissing(Tag &parent, const std::string &name) {
    if (!parent.find(name))
        parent.compound[name] = Tag::sequence(Type::Compound);
}
Document columnDocument(const Chunk &chunk, const SaveInfo &info, const TileUpdates &updates) {
    Document result;
    result.root = Tag::object();
    if (info.metadata) {
        const auto original = info.metadata->chunks.find(chunkKey(chunk.x, chunk.z));
        if (original != info.metadata->chunks.end())
            result = original->second;
    }
    auto &level = objectMember(result.root, "Level");
    level.compound["xPos"] = Tag::number(Type::Int, chunk.x);
    level.compound["zPos"] = Tag::number(Type::Int, chunk.z);
    if (!level.find("LastUpdate"))
        level.compound["LastUpdate"] = Tag::number(Type::Long, 0);
    if (!level.find("InhabitedTime"))
        level.compound["InhabitedTime"] = Tag::number(Type::Long, 0);
    if (info.generation || !level.find("TerrainPopulated"))
        level.compound["TerrainPopulated"] =
            Tag::number(Type::Byte, info.generation ? (info.generation->terrainPopulated ? 1 : 0) : 1);
    // Vanilla can repair lighting after opening an exported partial terrain set.
    level.compound["LightPopulated"] = Tag::number(Type::Byte, 0);
    Tag heights;
    heights.type = Type::IntArray;
    heights.ints.assign(chunk.heightMap.begin(), chunk.heightMap.end());
    level.compound["HeightMap"] = std::move(heights);
    level.compound["Biomes"] = Tag::array(Bytes(chunk.biomes.begin(), chunk.biomes.end()));
    if (!chunk.hasBiomes)
        level.compound["Biomes"].bytes.assign(256, 1);

    std::array<const Tag *, 16> previous{};
    if (const Tag *sections = level.find("Sections"))
        if (sections->type == Type::List && sections->listType == Type::Compound)
            for (const auto &section : sections->list) {
                const auto y = integer(section, "Y", -1);
                if (y >= 0 && y < 16)
                    previous[static_cast<std::size_t>(y)] = &section;
            }
    Tag sections = Tag::sequence(Type::Compound);
    for (int y = 0; y < 16; ++y)
        if (chunk.sections[y]) {
            const auto &native = *chunk.sections[y];
            Tag section = previous[y] ? *previous[y] : Tag::object();
            section.compound["Y"] = Tag::number(Type::Byte, y);
            Bytes blocks(4096), data(2048), add(2048);
            bool needsAdd = false;
            for (std::size_t i = 0; i < native.blocks.size(); ++i) {
                const unsigned state = native.blocks[i], id = state >> 4, shift = (i & 1) * 4;
                blocks[i] = static_cast<std::uint8_t>(id);
                data[i >> 1] |= static_cast<std::uint8_t>((state & 15) << shift);
                add[i >> 1] |= static_cast<std::uint8_t>((id >> 8) << shift);
                needsAdd = needsAdd || id > 255;
            }
            section.compound["Blocks"] = Tag::array(std::move(blocks));
            section.compound["Data"] = Tag::array(std::move(data));
            if (needsAdd)
                section.compound["Add"] = Tag::array(std::move(add));
            else
                section.compound.erase("Add");
            section.compound["BlockLight"] = Tag::array(Bytes(native.blockLight.begin(), native.blockLight.end()));
            section.compound["SkyLight"] =
                Tag::array(chunk.hasSkyLight ? Bytes(native.skyLight.begin(), native.skyLight.end()) : Bytes(2048));
            sections.list.push_back(std::move(section));
        }
    level.compound["Sections"] = std::move(sections);
    listIfMissing(level, "Entities");
    listIfMissing(level, "TileEntities");
    listIfMissing(level, "TileTicks");
    const auto changed = updates.find(chunkKey(chunk.x, chunk.z));
    if (changed != updates.end())
        mergeTiles(level, changed->second);
    return result;
}

Document levelDocument(const World &world, const Player &player, const SaveInfo &info) {
    Document result;
    result.root = Tag::object();
    if (info.metadata)
        result = info.metadata->level;
    auto &data = objectMember(result.root, "Data");
    data.compound["version"] = Tag::number(Type::Int, 19133);
    data.compound["LevelName"] = Tag::text(info.levelName);
    data.compound["RandomSeed"] = Tag::number(Type::Long, info.seed);
    data.compound["SpawnX"] = Tag::number(Type::Int, info.spawnX);
    data.compound["SpawnY"] = Tag::number(Type::Int, info.spawnY);
    data.compound["SpawnZ"] = Tag::number(Type::Int, info.spawnZ);
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    data.compound["LastPlayed"] = Tag::number(Type::Long, now);
    if (!info.metadata) {
        data.compound["GameType"] = Tag::number(Type::Int, 1);
        data.compound["generatorName"] = Tag::text("flat");
        data.compound["generatorVersion"] = Tag::number(Type::Int, 0);
        data.compound["generatorOptions"] = Tag::text("3;minecraft:bedrock,62*minecraft:stone,minecraft:grass;1;");
        data.compound["MapFeatures"] = Tag::number(Type::Byte, 0);
        data.compound["Time"] = Tag::number(Type::Long, 0);
        data.compound["DayTime"] = Tag::number(Type::Long, info.generation ? 0 : 6000);
        data.compound["allowCommands"] = Tag::number(Type::Byte, 1);
        data.compound["hardcore"] = Tag::number(Type::Byte, 0);
        data.compound["initialized"] = Tag::number(Type::Byte, 1);
    }
    if (info.generation) {
        const auto &generation = *info.generation;
        const char *name = nullptr;
        int version = 0;
        switch (generation.type) {
        case worldgen::WorldType::Default:
            name = "default";
            version = 1;
            break;
        case worldgen::WorldType::LargeBiomes:
            name = "largeBiomes";
            break;
        case worldgen::WorldType::Amplified:
            name = "amplified";
            break;
        default:
            bad("unknown native world generator type");
        }
        if (generation.gameType < 0 || generation.gameType > 3)
            bad("invalid generated world game type");
        data.compound["GameType"] = Tag::number(Type::Int, generation.gameType);
        data.compound["generatorName"] = Tag::text(name);
        data.compound["generatorVersion"] = Tag::number(Type::Int, version);
        data.compound["generatorOptions"] = Tag::text("");
        data.compound["MapFeatures"] = Tag::number(Type::Byte, generation.mapFeatures ? 1 : 0);
        data.compound["allowCommands"] = Tag::number(Type::Byte, generation.allowCommands ? 1 : 0);
        data.compound["NativeVoxelBaseGeneration"] = Tag::number(Type::Byte, 1);
    }
    if (info.levelUpdates) {
        std::size_t nodes = 0;
        auto updates = nativeTag(*info.levelUpdates, 0, nodes);
        if (updates.type != Type::Compound)
            bad("level updates must be a compound");
        for (auto &entry : updates.compound)
            data.compound[entry.first] = std::move(entry.second);
    }
    const auto &border = world.border();
    data.compound["BorderCenterX"] = Tag::floating(Type::Double, (border.minX + border.maxX) / 2.0);
    data.compound["BorderCenterZ"] = Tag::floating(Type::Double, (border.minZ + border.maxZ) / 2.0);
    data.compound["BorderSize"] =
        Tag::floating(Type::Double, std::min(border.maxX - border.minX, border.maxZ - border.minZ));
    data.compound["BorderSizeLerpTarget"] = data.compound["BorderSize"];
    data.compound["BorderSizeLerpTime"] = Tag::number(Type::Long, 0);

    auto &savedPlayer = objectMember(data, "Player");
    if (info.playerData) {
        std::size_t nodes = 0;
        auto playerTag = nativeTag(*info.playerData, 0, nodes);
        if (playerTag.type != Type::Compound)
            bad("player data must be a compound");
        for (auto &entry : playerTag.compound)
            savedPlayer.compound[entry.first] = std::move(entry.second);
    }
    savedPlayer.compound["Dimension"] = Tag::number(Type::Int, info.dimension);
    Tag pos = Tag::sequence(Type::Double);
    for (const double value : {player.x, player.y, player.z})
        pos.list.push_back(Tag::floating(Type::Double, value));
    savedPlayer.compound["Pos"] = std::move(pos);
    Tag motion = Tag::sequence(Type::Double);
    for (const double value : {player.motionX, player.motionY, player.motionZ})
        motion.list.push_back(Tag::floating(Type::Double, value));
    savedPlayer.compound["Motion"] = std::move(motion);
    Tag rotation = Tag::sequence(Type::Float);
    rotation.list.push_back(Tag::floating(Type::Float, player.yaw));
    rotation.list.push_back(Tag::floating(Type::Float, player.pitch));
    savedPlayer.compound["Rotation"] = std::move(rotation);
    savedPlayer.compound["OnGround"] = Tag::number(Type::Byte, player.onGround ? 1 : 0);
    savedPlayer.compound["FallDistance"] = Tag::floating(Type::Float, player.fallDistance);
    if (!info.playerData || !info.playerData->get("foodLevel"))
        savedPlayer.compound["foodLevel"] = Tag::number(Type::Int, player.foodLevel);
    if (!savedPlayer.find("Health"))
        savedPlayer.compound["Health"] = Tag::floating(Type::Float, 20);
    if (!savedPlayer.find("foodSaturationLevel"))
        savedPlayer.compound["foodSaturationLevel"] = Tag::floating(Type::Float, 5);
    if (!savedPlayer.find("Air"))
        savedPlayer.compound["Air"] = Tag::number(Type::Short, 300);
    listIfMissing(savedPlayer, "Inventory");
    listIfMissing(savedPlayer, "EnderItems");
    auto &abilities = objectMember(savedPlayer, "abilities");
    abilities.compound["flying"] = Tag::number(Type::Byte, player.flying ? 1 : 0);
    abilities.compound["mayfly"] = Tag::number(Type::Byte, (player.allowFlying || !info.metadata) ? 1 : 0);
    abilities.compound["flySpeed"] = Tag::floating(Type::Float, player.flySpeed);
    if (!abilities.find("walkSpeed"))
        abilities.compound["walkSpeed"] = Tag::floating(Type::Float, .1f);
    if (!info.metadata) {
        abilities.compound["invulnerable"] = Tag::number(Type::Byte, 1);
        abilities.compound["instabuild"] = Tag::number(Type::Byte, 1);
        abilities.compound["mayBuild"] = Tag::number(Type::Byte, 1);
    }
    {
        const int mode = info.generation ? info.generation->gameType : info.gameType;
        if (mode < 0 || mode > 3)
            bad("invalid saved game type");
        savedPlayer.compound["playerGameType"] = Tag::number(Type::Int, mode);
        abilities.compound["invulnerable"] = Tag::number(Type::Byte, mode == 1 || mode == 3);
        abilities.compound["instabuild"] = Tag::number(Type::Byte, mode == 1);
        abilities.compound["mayBuild"] = Tag::number(Type::Byte, mode == 0 || mode == 1);
        abilities.compound["mayfly"] = Tag::number(Type::Byte, player.allowFlying || mode == 1 || mode == 3);
        abilities.compound["flying"] = Tag::number(Type::Byte, player.flying || mode == 3);
    }
    return result;
}

void writeRegion(const fs::path &path, std::vector<const Chunk *> chunks, const SaveInfo &info,
                 const TileUpdates &updates) {
    std::sort(chunks.begin(), chunks.end(),
              [](const Chunk *a, const Chunk *b) { return a->z != b->z ? a->z < b->z : a->x < b->x; });
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        bad("cannot write new region " + path.u8string());
    std::array<std::uint8_t, 8192> header{};
    file.write(reinterpret_cast<const char *>(header.data()), header.size());
    std::uint32_t sector = 2;
    const auto now = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    for (const auto chunk : chunks) {
        const auto compressed = deflateBytes(NbtWriter().write(columnDocument(*chunk, info, updates)), 15);
        const auto count = static_cast<std::uint32_t>((compressed.size() + 5 + 4095) / 4096);
        if (count == 0 || count > 255 || sector >= 0x1000000u)
            bad("chunk exceeds legacy Anvil sector capacity");
        const unsigned slot = static_cast<unsigned>(chunk->x & 31) + static_cast<unsigned>(chunk->z & 31) * 32;
        setBig32(header.data() + slot * 4, (sector << 8) | count);
        setBig32(header.data() + 4096 + slot * 4, now);
        std::array<std::uint8_t, 5> chunkHeader{};
        setBig32(chunkHeader.data(), static_cast<std::uint32_t>(compressed.size() + 1));
        chunkHeader[4] = 2;
        file.write(reinterpret_cast<const char *>(chunkHeader.data()), chunkHeader.size());
        file.write(reinterpret_cast<const char *>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        const Bytes padding(count * 4096 - compressed.size() - 5);
        file.write(reinterpret_cast<const char *>(padding.data()), static_cast<std::streamsize>(padding.size()));
        if (!file)
            bad("failed writing region chunk");
        sector += count;
    }
    file.seekp(0);
    file.write(reinterpret_cast<const char *>(header.data()), header.size());
    file.close();
    if (!file)
        bad("failed finishing region file");
}

bool pathWithin(const fs::path &target, const fs::path &original) {
    auto candidate = target.generic_u8string(), source = original.generic_u8string();
#ifdef _WIN32
    const auto lower = [](unsigned char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : static_cast<char>(value);
    };
    std::transform(candidate.begin(), candidate.end(), candidate.begin(), lower);
    std::transform(source.begin(), source.end(), source.begin(), lower);
#endif
    if (candidate == source)
        return true;
    if (!source.empty() && source.back() != '/')
        source.push_back('/');
    return candidate.size() >= source.size() && candidate.compare(0, source.size(), source) == 0;
}
} // namespace

bool saveNewWorld(const std::string &directory, const World &world, const Player &player, const SaveInfo &info,
                  std::string *error) {
    if (error)
        error->clear();
    fs::path target;
    bool created = false;
    try {
        if (world.chunks().empty())
            bad("cannot export an empty world");
        Player validator;
        validator.teleport(player.x, player.y, player.z);
        if (!std::isfinite(player.yaw) || !std::isfinite(player.pitch) || !std::isfinite(player.motionX) ||
            !std::isfinite(player.motionY) || !std::isfinite(player.motionZ))
            bad("cannot export non-finite player state");
        target = fs::weakly_canonical(fs::absolute(fs::u8path(directory)));
        if (target == target.root_path() || target.filename().empty())
            bad("export destination must be a new named directory");
        const auto status = fs::symlink_status(target);
        if (fs::exists(status) || fs::is_symlink(status))
            bad("export refuses an existing destination: " + target.u8string());
        if (info.metadata && pathWithin(target, info.metadata->sourceDirectory))
            bad("export cannot create files inside the original imported save");
        // The metadata and dimension are validated before any directory is made.
        auto level = levelDocument(world, player, info);
        const auto levelBytes = deflateBytes(NbtWriter().write(level), 31);
        const auto relativeDimension = dimensionPath(fs::path{}, info.dimension);
        const auto updates = tileUpdates(info);
        fs::create_directories(target.parent_path());
        // Exclusive directory creation is the overwrite guard; an empty existing
        // folder is refused as well. Cleanup only targets this newly-owned folder.
        if (!fs::create_directory(target))
            bad("export destination appeared before exclusive creation");
        created = true;
        const fs::path regions = target / relativeDimension / "region";
        fs::create_directories(regions);
        std::map<std::pair<int, int>, std::vector<const Chunk *>> grouped;
        for (const auto &entry : world.chunks())
            grouped[{floorDiv32(entry.second.x), floorDiv32(entry.second.z)}].push_back(&entry.second);
        for (const auto &pair : grouped) {
            const auto path =
                regions / ("r." + std::to_string(pair.first.first) + "." + std::to_string(pair.first.second) + ".mca");
            writeRegion(path, pair.second, info, updates);
        }
        writeFile(target / "level.dat", levelBytes);
        Bytes lock(8);
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        for (int i = 0; i < 8; ++i)
            lock[i] = static_cast<std::uint8_t>(stamp >> ((7 - i) * 8));
        writeFile(target / "session.lock", lock);
        return true;
    } catch (const std::exception &ex) {
        if (created) {
            // Re-resolve before recursive cleanup and refuse to traverse a link
            // or any path different from the absolute directory created above.
            std::error_code ignored;
            const auto status = fs::symlink_status(target, ignored);
            if (!ignored && fs::is_directory(status) && !fs::is_symlink(status) &&
                fs::weakly_canonical(target, ignored) == target && !ignored)
                fs::remove_all(target, ignored);
        }
        if (error)
            *error = ex.what();
        return false;
    }
}

} // namespace mc::worldio
