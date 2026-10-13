#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc::worldgen {

// java.util.Random's 48-bit LCG and exact draw consumption. Instances are not
// shared between threads. nextGaussian uses the platform log/sqrt library.
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }
    void setSeed(std::int64_t seed);
    std::int32_t nextInt();
    std::int32_t nextInt(std::int32_t bound);
    std::int64_t nextLong();
    bool nextBoolean();
    float nextFloat();
    double nextDouble();
    double nextGaussian();
    void nextBytes(std::uint8_t* output, std::size_t size);
    std::uint64_t internalSeed() const noexcept { return seed_; }
private:
    std::uint64_t seed_{};
    bool hasGaussian_{};
    double gaussian_{};
    std::uint32_t next(unsigned bits);
};

class NoiseImproved {
public:
    explicit NoiseImproved(JavaRandom& random);
    // Adds to the caller's array in X/Z/Y order. The ySize==1 path is the
    // original specialized 2-D routine, not a slice of the 3-D routine.
    void add(double* output, double xOffset, double yOffset, double zOffset,
             int xSize, int ySize, int zSize, double xScale, double yScale,
             double zScale, double noiseScale) const;
private:
    std::array<int,512> permutation_{};
    double x_{}, y_{}, z_{};
    static double lerp(double amount, double from, double to);
    static double grad2(int hash, double x, double z);
    static double grad3(int hash, double x, double y, double z);
};

class NoiseOctaves {
public:
    NoiseOctaves(JavaRandom& random, int count);
    void generate(std::vector<double>& output, int xOffset, int yOffset, int zOffset,
                  int xSize, int ySize, int zSize,
                  double xScale, double yScale, double zScale) const;
    // The last argument exists in MCP's overload but is intentionally unused.
    void generate2D(std::vector<double>& output, int xOffset, int zOffset,
                    int xSize, int zSize, double xScale, double zScale,
                    double unused = .5) const;
private:
    std::vector<NoiseImproved> octaves_;
};

class NoiseSimplex {
public:
    explicit NoiseSimplex(JavaRandom& random);
    // Scalar sampling does not add the constructor's coordinate offsets;
    // region sampling does, matching the two distinct vanilla methods.
    double sample(double x, double z) const;
    void add(double* output, double xOffset, double zOffset, int xSize, int zSize,
             double xScale, double zScale, double amplitude) const;
private:
    std::array<int,512> permutation_{};
    double x_{}, z_{}, unused_{};
};

class NoisePerlin {
public:
    NoisePerlin(JavaRandom& random, int count);
    double sample(double x, double z) const;
    void generate(std::vector<double>& output, double xOffset, double zOffset,
                  int xSize, int zSize, double xScale, double zScale,
                  double lacunarity, double persistence = .5) const;
private:
    std::vector<NoiseSimplex> octaves_;
};

} // namespace mc::worldgen
