// Generated from original GenLayer cell rules by tools/port_genlayers.py.
class GenLayerIsland final : public GenLayer {
public:
    explicit GenLayerIsland(std::int64_t seed):GenLayer(seed){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(areaX + j), (std::int64_t)(areaY + i));
                aint[j + i * areaWidth] = this->nextInt(10) == 0 ? 1 : 0;
            }
        }

        if (areaX > -areaWidth && areaX <= 0 && areaY > -areaHeight && areaY <= 0)
        {
            aint[-areaX + -areaY * areaWidth] = 1;
        }

        return aint;
    }
};

class GenLayerZoom : public GenLayer {
public:
    GenLayerZoom(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = floorPower2(areaX,1);
        int j = floorPower2(areaY,1);
        int k = (areaWidth >> 1) + 2;
        int l = (areaHeight >> 1) + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        int i1 = (k - 1) << 1;
        int j1 = (l - 1) << 1;
        IntArray aint1 = makeArray(i1 * j1);

        for (int k1 = 0; k1 < l - 1; ++k1)
        {
            int l1 = (k1 << 1) * i1;
            int i2 = 0;
            int j2 = aint[i2 + 0 + (k1 + 0) * k];

            for (int k2 = aint[i2 + 0 + (k1 + 1) * k]; i2 < k - 1; ++i2)
            {
                this->initChunkSeed((std::int64_t(i2 + i) * 2), (std::int64_t(k1 + j) * 2));
                int l2 = aint[i2 + 1 + (k1 + 0) * k];
                int i3 = aint[i2 + 1 + (k1 + 1) * k];
                aint1[l1] = j2;
                aint1[l1++ + i1] = this->selectRandom({j2, k2});
                aint1[l1] = this->selectRandom({j2, l2});
                aint1[l1++ + i1] = this->selectModeOrRandom(j2, l2, k2, i3);
                j2 = l2;
                k2 = i3;
            }
        }

        IntArray aint2 = makeArray(areaWidth * areaHeight);

        for (int j3 = 0; j3 < areaHeight; ++j3)
        {
            copyArray(aint1, (j3 + (areaY & 1)) * i1 + (areaX & 1), aint2, j3 * areaWidth, areaWidth);
        }

        return aint2;
    }
};

class GenLayerFuzzyZoom final : public GenLayerZoom {
public:
    GenLayerFuzzyZoom(std::int64_t seed,Layer parent):GenLayerZoom(seed,std::move(parent)){}
    int selectModeOrRandom(int p_151617_1_, int p_151617_2_, int p_151617_3_, int p_151617_4_) override {
        return this->selectRandom({p_151617_1_, p_151617_2_, p_151617_3_, p_151617_4_});
    }
};

class GenLayerAddIsland final : public GenLayer {
public:
    GenLayerAddIsland(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 0 + (i1 + 0) * k];
                int l1 = aint[j1 + 2 + (i1 + 0) * k];
                int i2 = aint[j1 + 0 + (i1 + 2) * k];
                int j2 = aint[j1 + 2 + (i1 + 2) * k];
                int k2 = aint[j1 + 1 + (i1 + 1) * k];
                this->initChunkSeed((std::int64_t)(j1 + areaX), (std::int64_t)(i1 + areaY));

                if (k2 != 0 || (k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0))
                {
                    if (k2 > 0 && (k1 == 0 || l1 == 0 || i2 == 0 || j2 == 0))
                    {
                        if (this->nextInt(5) == 0)
                        {
                            if (k2 == 4)
                            {
                                aint1[j1 + i1 * areaWidth] = 4;
                            }
                            else
                            {
                                aint1[j1 + i1 * areaWidth] = 0;
                            }
                        }
                        else
                        {
                            aint1[j1 + i1 * areaWidth] = k2;
                        }
                    }
                    else
                    {
                        aint1[j1 + i1 * areaWidth] = k2;
                    }
                }
                else
                {
                    int l2 = 1;
                    int i3 = 1;

                    if (k1 != 0 && this->nextInt(l2++) == 0)
                    {
                        i3 = k1;
                    }

                    if (l1 != 0 && this->nextInt(l2++) == 0)
                    {
                        i3 = l1;
                    }

                    if (i2 != 0 && this->nextInt(l2++) == 0)
                    {
                        i3 = i2;
                    }

                    if (j2 != 0 && this->nextInt(l2++) == 0)
                    {
                        i3 = j2;
                    }

                    if (this->nextInt(3) == 0)
                    {
                        aint1[j1 + i1 * areaWidth] = i3;
                    }
                    else if (i3 == 4)
                    {
                        aint1[j1 + i1 * areaWidth] = 4;
                    }
                    else
                    {
                        aint1[j1 + i1 * areaWidth] = 0;
                    }
                }
            }
        }

        return aint1;
    }
};

class GenLayerRemoveTooMuchOcean final : public GenLayer {
public:
    GenLayerRemoveTooMuchOcean(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (areaWidth + 2)];
                int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (areaWidth + 2)];
                int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (areaWidth + 2)];
                int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (areaWidth + 2)];
                int k2 = aint[j1 + 1 + (i1 + 1) * k];
                aint1[j1 + i1 * areaWidth] = k2;
                this->initChunkSeed((std::int64_t)(j1 + areaX), (std::int64_t)(i1 + areaY));

                if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && this->nextInt(2) == 0)
                {
                    aint1[j1 + i1 * areaWidth] = 1;
                }
            }
        }

        return aint1;
    }
};

class GenLayerAddSnow final : public GenLayer {
public:
    GenLayerAddSnow(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 1 + (i1 + 1) * k];
                this->initChunkSeed((std::int64_t)(j1 + areaX), (std::int64_t)(i1 + areaY));

                if (k1 == 0)
                {
                    aint1[j1 + i1 * areaWidth] = 0;
                }
                else
                {
                    int l1 = this->nextInt(6);

                    if (l1 == 0)
                    {
                        l1 = 4;
                    }
                    else if (l1 <= 1)
                    {
                        l1 = 3;
                    }
                    else
                    {
                        l1 = 1;
                    }

                    aint1[j1 + i1 * areaWidth] = l1;
                }
            }
        }

        return aint1;
    }
};

class GenLayerEdge final : public GenLayer {
public:
    enum class Mode {COOL_WARM,HEAT_ICE,SPECIAL};
    Mode field_151627_c;
    GenLayerEdge(std::int64_t seed,Layer parent,Mode mode):GenLayer(seed,std::move(parent)),field_151627_c(mode){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        switch (this->field_151627_c)
        {
            case Mode::COOL_WARM:
            default:
                return this->getIntsCoolWarm(areaX, areaY, areaWidth, areaHeight);

            case Mode::HEAT_ICE:
                return this->getIntsHeatIce(areaX, areaY, areaWidth, areaHeight);

            case Mode::SPECIAL:
                return this->getIntsSpecial(areaX, areaY, areaWidth, areaHeight);
        }
    }
    IntArray getIntsCoolWarm(int p_151626_1_, int p_151626_2_, int p_151626_3_, int p_151626_4_) {
        int i = p_151626_1_ - 1;
        int j = p_151626_2_ - 1;
        int k = 1 + p_151626_3_ + 1;
        int l = 1 + p_151626_4_ + 1;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(p_151626_3_ * p_151626_4_);

        for (int i1 = 0; i1 < p_151626_4_; ++i1)
        {
            for (int j1 = 0; j1 < p_151626_3_; ++j1)
            {
                this->initChunkSeed((std::int64_t)(j1 + p_151626_1_), (std::int64_t)(i1 + p_151626_2_));
                int k1 = aint[j1 + 1 + (i1 + 1) * k];

                if (k1 == 1)
                {
                    int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                    int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                    int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                    int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                    bool flag = l1 == 3 || i2 == 3 || j2 == 3 || k2 == 3;
                    bool flag1 = l1 == 4 || i2 == 4 || j2 == 4 || k2 == 4;

                    if (flag || flag1)
                    {
                        k1 = 2;
                    }
                }

                aint1[j1 + i1 * p_151626_3_] = k1;
            }
        }

        return aint1;
    }
    IntArray getIntsHeatIce(int p_151624_1_, int p_151624_2_, int p_151624_3_, int p_151624_4_) {
        int i = p_151624_1_ - 1;
        int j = p_151624_2_ - 1;
        int k = 1 + p_151624_3_ + 1;
        int l = 1 + p_151624_4_ + 1;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(p_151624_3_ * p_151624_4_);

        for (int i1 = 0; i1 < p_151624_4_; ++i1)
        {
            for (int j1 = 0; j1 < p_151624_3_; ++j1)
            {
                int k1 = aint[j1 + 1 + (i1 + 1) * k];

                if (k1 == 4)
                {
                    int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                    int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                    int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                    int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                    bool flag = l1 == 2 || i2 == 2 || j2 == 2 || k2 == 2;
                    bool flag1 = l1 == 1 || i2 == 1 || j2 == 1 || k2 == 1;

                    if (flag1 || flag)
                    {
                        k1 = 3;
                    }
                }

                aint1[j1 + i1 * p_151624_3_] = k1;
            }
        }

        return aint1;
    }
    IntArray getIntsSpecial(int p_151625_1_, int p_151625_2_, int p_151625_3_, int p_151625_4_) {
        IntArray aint = this->parent->getInts(p_151625_1_, p_151625_2_, p_151625_3_, p_151625_4_);
        IntArray aint1 = makeArray(p_151625_3_ * p_151625_4_);

        for (int i = 0; i < p_151625_4_; ++i)
        {
            for (int j = 0; j < p_151625_3_; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + p_151625_1_), (std::int64_t)(i + p_151625_2_));
                int k = aint[j + i * p_151625_3_];

                if (k != 0 && this->nextInt(13) == 0)
                {
                    k |= ((1 + this->nextInt(15)) << 8) & 3840;
                }

                aint1[j + i * p_151625_3_] = k;
            }
        }

        return aint1;
    }
};

class GenLayerAddMushroomIsland final : public GenLayer {
public:
    GenLayerAddMushroomIsland(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 0 + (i1 + 0) * k];
                int l1 = aint[j1 + 2 + (i1 + 0) * k];
                int i2 = aint[j1 + 0 + (i1 + 2) * k];
                int j2 = aint[j1 + 2 + (i1 + 2) * k];
                int k2 = aint[j1 + 1 + (i1 + 1) * k];
                this->initChunkSeed((std::int64_t)(j1 + areaX), (std::int64_t)(i1 + areaY));

                if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && this->nextInt(100) == 0)
                {
                    aint1[j1 + i1 * areaWidth] = biome_ids::mushroomIsland;
                }
                else
                {
                    aint1[j1 + i1 * areaWidth] = k2;
                }
            }
        }

        return aint1;
    }
};

class GenLayerDeepOcean final : public GenLayer {
public:
    GenLayerDeepOcean(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (areaWidth + 2)];
                int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (areaWidth + 2)];
                int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (areaWidth + 2)];
                int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (areaWidth + 2)];
                int k2 = aint[j1 + 1 + (i1 + 1) * k];
                int l2 = 0;

                if (k1 == 0)
                {
                    ++l2;
                }

                if (l1 == 0)
                {
                    ++l2;
                }

                if (i2 == 0)
                {
                    ++l2;
                }

                if (j2 == 0)
                {
                    ++l2;
                }

                if (k2 == 0 && l2 > 3)
                {
                    aint1[j1 + i1 * areaWidth] = biome_ids::deepOcean;
                }
                else
                {
                    aint1[j1 + i1 * areaWidth] = k2;
                }
            }
        }

        return aint1;
    }
};

class GenLayerRiverInit final : public GenLayer {
public:
    GenLayerRiverInit(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX, areaY, areaWidth, areaHeight);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                aint1[j + i * areaWidth] = aint[j + i * areaWidth] > 0 ? this->nextInt(299999) + 2 : 0;
            }
        }

        return aint1;
    }
};

class GenLayerBiome final : public GenLayer {
public:
    std::array<int,6> field_151623_c{{biome_ids::desert,biome_ids::desert,biome_ids::desert,biome_ids::savanna,biome_ids::savanna,biome_ids::plains}};
    std::array<int,6> field_151621_d{{biome_ids::forest,biome_ids::roofedForest,biome_ids::extremeHills,biome_ids::plains,biome_ids::birchForest,biome_ids::swampland}};
    std::array<int,4> field_151622_e{{biome_ids::forest,biome_ids::extremeHills,biome_ids::taiga,biome_ids::plains}};
    std::array<int,4> field_151620_f{{biome_ids::icePlains,biome_ids::icePlains,biome_ids::icePlains,biome_ids::coldTaiga}};
    GenLayerBiome(std::int64_t seed,Layer parent,WorldType):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX, areaY, areaWidth, areaHeight);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                int k = aint[j + i * areaWidth];
                int l = (k & 3840) >> 8;
                k = k & -3841;

                if (isBiomeOceanic(k))
                {
                    aint1[j + i * areaWidth] = k;
                }
                else if (k == biome_ids::mushroomIsland)
                {
                    aint1[j + i * areaWidth] = k;
                }
                else if (k == 1)
                {
                    if (l > 0)
                    {
                        if (this->nextInt(3) == 0)
                        {
                            aint1[j + i * areaWidth] = biome_ids::mesaPlateau;
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = biome_ids::mesaPlateau_F;
                        }
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = this->field_151623_c[this->nextInt(this->field_151623_c.size())];
                    }
                }
                else if (k == 2)
                {
                    if (l > 0)
                    {
                        aint1[j + i * areaWidth] = biome_ids::jungle;
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = this->field_151621_d[this->nextInt(this->field_151621_d.size())];
                    }
                }
                else if (k == 3)
                {
                    if (l > 0)
                    {
                        aint1[j + i * areaWidth] = biome_ids::megaTaiga;
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = this->field_151622_e[this->nextInt(this->field_151622_e.size())];
                    }
                }
                else if (k == 4)
                {
                    aint1[j + i * areaWidth] = this->field_151620_f[this->nextInt(this->field_151620_f.size())];
                }
                else
                {
                    aint1[j + i * areaWidth] = biome_ids::mushroomIsland;
                }
            }
        }

        return aint1;
    }
};

class GenLayerBiomeEdge final : public GenLayer {
public:
    GenLayerBiomeEdge(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];

                if (!this->replaceBiomeEdgeIfNecessary(aint, aint1, j, i, areaWidth, k, biome_ids::extremeHills, biome_ids::extremeHillsEdge) && !this->replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, biome_ids::mesaPlateau_F, biome_ids::mesa) && !this->replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, biome_ids::mesaPlateau, biome_ids::mesa) && !this->replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, biome_ids::megaTaiga, biome_ids::taiga))
                {
                    if (k == biome_ids::desert)
                    {
                        int l1 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int i2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int j2 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int k2 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                        if (l1 != biome_ids::icePlains && i2 != biome_ids::icePlains && j2 != biome_ids::icePlains && k2 != biome_ids::icePlains)
                        {
                            aint1[j + i * areaWidth] = k;
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = biome_ids::extremeHillsPlus;
                        }
                    }
                    else if (k == biome_ids::swampland)
                    {
                        int l = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int i1 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int j1 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int k1 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                        if (l != biome_ids::desert && i1 != biome_ids::desert && j1 != biome_ids::desert && k1 != biome_ids::desert && l != biome_ids::coldTaiga && i1 != biome_ids::coldTaiga && j1 != biome_ids::coldTaiga && k1 != biome_ids::coldTaiga && l != biome_ids::icePlains && i1 != biome_ids::icePlains && j1 != biome_ids::icePlains && k1 != biome_ids::icePlains)
                        {
                            if (l != biome_ids::jungle && k1 != biome_ids::jungle && i1 != biome_ids::jungle && j1 != biome_ids::jungle)
                            {
                                aint1[j + i * areaWidth] = k;
                            }
                            else
                            {
                                aint1[j + i * areaWidth] = biome_ids::jungleEdge;
                            }
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = biome_ids::plains;
                        }
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = k;
                    }
                }
            }
        }

        return aint1;
    }
    bool replaceBiomeEdgeIfNecessary(IntArray& p_151636_1_, IntArray& p_151636_2_, int p_151636_3_, int p_151636_4_, int p_151636_5_, int p_151636_6_, int p_151636_7_, int p_151636_8_) {
        if (!biomesEqualOrMesaPlateau(p_151636_6_, p_151636_7_))
        {
            return false;
        }
        else
        {
            int i = p_151636_1_[p_151636_3_ + 1 + (p_151636_4_ + 1 - 1) * (p_151636_5_ + 2)];
            int j = p_151636_1_[p_151636_3_ + 1 + 1 + (p_151636_4_ + 1) * (p_151636_5_ + 2)];
            int k = p_151636_1_[p_151636_3_ + 1 - 1 + (p_151636_4_ + 1) * (p_151636_5_ + 2)];
            int l = p_151636_1_[p_151636_3_ + 1 + (p_151636_4_ + 1 + 1) * (p_151636_5_ + 2)];

            if (this->canBiomesBeNeighbors(i, p_151636_7_) && this->canBiomesBeNeighbors(j, p_151636_7_) && this->canBiomesBeNeighbors(k, p_151636_7_) && this->canBiomesBeNeighbors(l, p_151636_7_))
            {
                p_151636_2_[p_151636_3_ + p_151636_4_ * p_151636_5_] = p_151636_6_;
            }
            else
            {
                p_151636_2_[p_151636_3_ + p_151636_4_ * p_151636_5_] = p_151636_8_;
            }

            return true;
        }
    }
    bool replaceBiomeEdge(IntArray& p_151635_1_, IntArray& p_151635_2_, int p_151635_3_, int p_151635_4_, int p_151635_5_, int p_151635_6_, int p_151635_7_, int p_151635_8_) {
        if (p_151635_6_ != p_151635_7_)
        {
            return false;
        }
        else
        {
            int i = p_151635_1_[p_151635_3_ + 1 + (p_151635_4_ + 1 - 1) * (p_151635_5_ + 2)];
            int j = p_151635_1_[p_151635_3_ + 1 + 1 + (p_151635_4_ + 1) * (p_151635_5_ + 2)];
            int k = p_151635_1_[p_151635_3_ + 1 - 1 + (p_151635_4_ + 1) * (p_151635_5_ + 2)];
            int l = p_151635_1_[p_151635_3_ + 1 + (p_151635_4_ + 1 + 1) * (p_151635_5_ + 2)];

            if (biomesEqualOrMesaPlateau(i, p_151635_7_) && biomesEqualOrMesaPlateau(j, p_151635_7_) && biomesEqualOrMesaPlateau(k, p_151635_7_) && biomesEqualOrMesaPlateau(l, p_151635_7_))
            {
                p_151635_2_[p_151635_3_ + p_151635_4_ * p_151635_5_] = p_151635_6_;
            }
            else
            {
                p_151635_2_[p_151635_3_ + p_151635_4_ * p_151635_5_] = p_151635_8_;
            }

            return true;
        }
    }
    bool canBiomesBeNeighbors(int p_151634_1_, int p_151634_2_) {
        if (biomesEqualOrMesaPlateau(p_151634_1_, p_151634_2_))
        {
            return true;
        }
        else
        {
            const BiomeDescription* biomegenbase = biomeDefinition(p_151634_1_);
            const BiomeDescription* biomegenbase1 = biomeDefinition(p_151634_2_);

            if (biomegenbase != nullptr && biomegenbase1 != nullptr)
            {
                TemperatureCategory biomegenbase_tempcategory = biomegenbase->getTempCategory();
                TemperatureCategory biomegenbase_tempcategory1 = biomegenbase1->getTempCategory();
                return biomegenbase_tempcategory == biomegenbase_tempcategory1 || biomegenbase_tempcategory == TemperatureCategory::MEDIUM || biomegenbase_tempcategory1 == TemperatureCategory::MEDIUM;
            }
            else
            {
                return false;
            }
        }
    }
};

class GenLayerHills final : public GenLayer {
public:
    Layer field_151628_d;
    GenLayerHills(std::int64_t seed,Layer parent,Layer river):GenLayer(seed,std::move(parent)),field_151628_d(std::move(river)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
        IntArray aint1 = this->field_151628_d->getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
        IntArray aint2 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                int l = aint1[j + 1 + (i + 1) * (areaWidth + 2)];
                bool flag = (l - 2) % 29 == 0;



                if (k != 0 && l >= 2 && (l - 2) % 29 == 1 && k < 128)
                {
                    if (biomeDefinition(k + 128) != nullptr)
                    {
                        aint2[j + i * areaWidth] = k + 128;
                    }
                    else
                    {
                        aint2[j + i * areaWidth] = k;
                    }
                }
                else if (this->nextInt(3) != 0 && !flag)
                {
                    aint2[j + i * areaWidth] = k;
                }
                else
                {
                    int i1 = k;

                    if (k == biome_ids::desert)
                    {
                        i1 = biome_ids::desertHills;
                    }
                    else if (k == biome_ids::forest)
                    {
                        i1 = biome_ids::forestHills;
                    }
                    else if (k == biome_ids::birchForest)
                    {
                        i1 = biome_ids::birchForestHills;
                    }
                    else if (k == biome_ids::roofedForest)
                    {
                        i1 = biome_ids::plains;
                    }
                    else if (k == biome_ids::taiga)
                    {
                        i1 = biome_ids::taigaHills;
                    }
                    else if (k == biome_ids::megaTaiga)
                    {
                        i1 = biome_ids::megaTaigaHills;
                    }
                    else if (k == biome_ids::coldTaiga)
                    {
                        i1 = biome_ids::coldTaigaHills;
                    }
                    else if (k == biome_ids::plains)
                    {
                        if (this->nextInt(3) == 0)
                        {
                            i1 = biome_ids::forestHills;
                        }
                        else
                        {
                            i1 = biome_ids::forest;
                        }
                    }
                    else if (k == biome_ids::icePlains)
                    {
                        i1 = biome_ids::iceMountains;
                    }
                    else if (k == biome_ids::jungle)
                    {
                        i1 = biome_ids::jungleHills;
                    }
                    else if (k == biome_ids::ocean)
                    {
                        i1 = biome_ids::deepOcean;
                    }
                    else if (k == biome_ids::extremeHills)
                    {
                        i1 = biome_ids::extremeHillsPlus;
                    }
                    else if (k == biome_ids::savanna)
                    {
                        i1 = biome_ids::savannaPlateau;
                    }
                    else if (biomesEqualOrMesaPlateau(k, biome_ids::mesaPlateau_F))
                    {
                        i1 = biome_ids::mesa;
                    }
                    else if (k == biome_ids::deepOcean && this->nextInt(3) == 0)
                    {
                        int j1 = this->nextInt(2);

                        if (j1 == 0)
                        {
                            i1 = biome_ids::plains;
                        }
                        else
                        {
                            i1 = biome_ids::forest;
                        }
                    }

                    if (flag && i1 != k)
                    {
                        if (biomeDefinition(i1 + 128) != nullptr)
                        {
                            i1 += 128;
                        }
                        else
                        {
                            i1 = k;
                        }
                    }

                    if (i1 == k)
                    {
                        aint2[j + i * areaWidth] = k;
                    }
                    else
                    {
                        int k2 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int k1 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int l1 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int i2 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                        int j2 = 0;

                        if (biomesEqualOrMesaPlateau(k2, k))
                        {
                            ++j2;
                        }

                        if (biomesEqualOrMesaPlateau(k1, k))
                        {
                            ++j2;
                        }

                        if (biomesEqualOrMesaPlateau(l1, k))
                        {
                            ++j2;
                        }

                        if (biomesEqualOrMesaPlateau(i2, k))
                        {
                            ++j2;
                        }

                        if (j2 >= 3)
                        {
                            aint2[j + i * areaWidth] = i1;
                        }
                        else
                        {
                            aint2[j + i * areaWidth] = k;
                        }
                    }
                }
            }
        }

        return aint2;
    }
};

class GenLayerRareBiome final : public GenLayer {
public:
    GenLayerRareBiome(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];

                if (this->nextInt(57) == 0)
                {
                    if (k == biome_ids::plains)
                    {
                        aint1[j + i * areaWidth] = biome_ids::plains + 128;
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = k;
                    }
                }
                else
                {
                    aint1[j + i * areaWidth] = k;
                }
            }
        }

        return aint1;
    }
};

class GenLayerShore final : public GenLayer {
public:
    GenLayerShore(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->parent->getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaHeight; ++i)
        {
            for (int j = 0; j < areaWidth; ++j)
            {
                this->initChunkSeed((std::int64_t)(j + areaX), (std::int64_t)(i + areaY));
                int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                const BiomeDescription* biomegenbase = biomeDefinition(k);

                if (k == biome_ids::mushroomIsland)
                {
                    int j2 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                    int i3 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                    int l3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                    int k4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                    if (j2 != biome_ids::ocean && i3 != biome_ids::ocean && l3 != biome_ids::ocean && k4 != biome_ids::ocean)
                    {
                        aint1[j + i * areaWidth] = k;
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = biome_ids::mushroomIslandShore;
                    }
                }
                else if (biomegenbase != nullptr && biomegenbase->getBiomeClass() == BiomeClass::BiomeGenJungle)
                {
                    int i2 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                    int l2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                    int k3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                    int j4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                    if (this->func_151631_c(i2) && this->func_151631_c(l2) && this->func_151631_c(k3) && this->func_151631_c(j4))
                    {
                        if (!isBiomeOceanic(i2) && !isBiomeOceanic(l2) && !isBiomeOceanic(k3) && !isBiomeOceanic(j4))
                        {
                            aint1[j + i * areaWidth] = k;
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = biome_ids::beach;
                        }
                    }
                    else
                    {
                        aint1[j + i * areaWidth] = biome_ids::jungleEdge;
                    }
                }
                else if (k != biome_ids::extremeHills && k != biome_ids::extremeHillsPlus && k != biome_ids::extremeHillsEdge)
                {
                    if (biomegenbase != nullptr && biomegenbase->isSnowyBiome())
                    {
                        this->func_151632_a(aint, aint1, j, i, areaWidth, k, biome_ids::coldBeach);
                    }
                    else if (k != biome_ids::mesa && k != biome_ids::mesaPlateau_F)
                    {
                        if (k != biome_ids::ocean && k != biome_ids::deepOcean && k != biome_ids::river && k != biome_ids::swampland)
                        {
                            int l1 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                            int k2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                            int j3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                            int i4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                            if (!isBiomeOceanic(l1) && !isBiomeOceanic(k2) && !isBiomeOceanic(j3) && !isBiomeOceanic(i4))
                            {
                                aint1[j + i * areaWidth] = k;
                            }
                            else
                            {
                                aint1[j + i * areaWidth] = biome_ids::beach;
                            }
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = k;
                        }
                    }
                    else
                    {
                        int l = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int i1 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int j1 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int k1 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];

                        if (!isBiomeOceanic(l) && !isBiomeOceanic(i1) && !isBiomeOceanic(j1) && !isBiomeOceanic(k1))
                        {
                            if (this->func_151633_d(l) && this->func_151633_d(i1) && this->func_151633_d(j1) && this->func_151633_d(k1))
                            {
                                aint1[j + i * areaWidth] = k;
                            }
                            else
                            {
                                aint1[j + i * areaWidth] = biome_ids::desert;
                            }
                        }
                        else
                        {
                            aint1[j + i * areaWidth] = k;
                        }
                    }
                }
                else
                {
                    this->func_151632_a(aint, aint1, j, i, areaWidth, k, biome_ids::stoneBeach);
                }
            }
        }

        return aint1;
    }
    void func_151632_a(IntArray& p_151632_1_, IntArray& p_151632_2_, int p_151632_3_, int p_151632_4_, int p_151632_5_, int p_151632_6_, int p_151632_7_) {
        if (isBiomeOceanic(p_151632_6_))
        {
            p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_6_;
        }
        else
        {
            int i = p_151632_1_[p_151632_3_ + 1 + (p_151632_4_ + 1 - 1) * (p_151632_5_ + 2)];
            int j = p_151632_1_[p_151632_3_ + 1 + 1 + (p_151632_4_ + 1) * (p_151632_5_ + 2)];
            int k = p_151632_1_[p_151632_3_ + 1 - 1 + (p_151632_4_ + 1) * (p_151632_5_ + 2)];
            int l = p_151632_1_[p_151632_3_ + 1 + (p_151632_4_ + 1 + 1) * (p_151632_5_ + 2)];

            if (!isBiomeOceanic(i) && !isBiomeOceanic(j) && !isBiomeOceanic(k) && !isBiomeOceanic(l))
            {
                p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_6_;
            }
            else
            {
                p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_7_;
            }
        }
    }
    bool func_151631_c(int p_151631_1_) {
        return biomeDefinition(p_151631_1_) != nullptr && biomeDefinition(p_151631_1_)->getBiomeClass() == BiomeClass::BiomeGenJungle ? true : p_151631_1_ == biome_ids::jungleEdge || p_151631_1_ == biome_ids::jungle || p_151631_1_ == biome_ids::jungleHills || p_151631_1_ == biome_ids::forest || p_151631_1_ == biome_ids::taiga || isBiomeOceanic(p_151631_1_);
    }
    bool func_151633_d(int p_151633_1_) {
        return isMesa(p_151633_1_);
    }
};

class GenLayerRiver final : public GenLayer {
public:
    GenLayerRiver(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = this->func_151630_c(aint[j1 + 0 + (i1 + 1) * k]);
                int l1 = this->func_151630_c(aint[j1 + 2 + (i1 + 1) * k]);
                int i2 = this->func_151630_c(aint[j1 + 1 + (i1 + 0) * k]);
                int j2 = this->func_151630_c(aint[j1 + 1 + (i1 + 2) * k]);
                int k2 = this->func_151630_c(aint[j1 + 1 + (i1 + 1) * k]);

                if (k2 == k1 && k2 == i2 && k2 == l1 && k2 == j2)
                {
                    aint1[j1 + i1 * areaWidth] = -1;
                }
                else
                {
                    aint1[j1 + i1 * areaWidth] = biome_ids::river;
                }
            }
        }

        return aint1;
    }
    int func_151630_c(int p_151630_1_) {
        return p_151630_1_ >= 2 ? 2 + (p_151630_1_ & 1) : p_151630_1_;
    }
};

class GenLayerSmooth final : public GenLayer {
public:
    GenLayerSmooth(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        int i = areaX - 1;
        int j = areaY - 1;
        int k = areaWidth + 2;
        int l = areaHeight + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        IntArray aint1 = makeArray(areaWidth * areaHeight);

        for (int i1 = 0; i1 < areaHeight; ++i1)
        {
            for (int j1 = 0; j1 < areaWidth; ++j1)
            {
                int k1 = aint[j1 + 0 + (i1 + 1) * k];
                int l1 = aint[j1 + 2 + (i1 + 1) * k];
                int i2 = aint[j1 + 1 + (i1 + 0) * k];
                int j2 = aint[j1 + 1 + (i1 + 2) * k];
                int k2 = aint[j1 + 1 + (i1 + 1) * k];

                if (k1 == l1 && i2 == j2)
                {
                    this->initChunkSeed((std::int64_t)(j1 + areaX), (std::int64_t)(i1 + areaY));

                    if (this->nextInt(2) == 0)
                    {
                        k2 = k1;
                    }
                    else
                    {
                        k2 = i2;
                    }
                }
                else
                {
                    if (k1 == l1)
                    {
                        k2 = k1;
                    }

                    if (i2 == j2)
                    {
                        k2 = i2;
                    }
                }

                aint1[j1 + i1 * areaWidth] = k2;
            }
        }

        return aint1;
    }
};

class GenLayerRiverMix final : public GenLayer {
public:
    Layer biomePatternGeneratorChain,riverPatternGeneratorChain;
    GenLayerRiverMix(std::int64_t seed,Layer biomes,Layer river):GenLayer(seed),biomePatternGeneratorChain(std::move(biomes)),riverPatternGeneratorChain(std::move(river)){}
    void initWorldGenSeed(std::int64_t seed) override {
        this->biomePatternGeneratorChain->initWorldGenSeed(seed);
        this->riverPatternGeneratorChain->initWorldGenSeed(seed);
        GenLayer::initWorldGenSeed(seed);
    }
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        IntArray aint = this->biomePatternGeneratorChain->getInts(areaX, areaY, areaWidth, areaHeight);
        IntArray aint1 = this->riverPatternGeneratorChain->getInts(areaX, areaY, areaWidth, areaHeight);
        IntArray aint2 = makeArray(areaWidth * areaHeight);

        for (int i = 0; i < areaWidth * areaHeight; ++i)
        {
            if (aint[i] != biome_ids::ocean && aint[i] != biome_ids::deepOcean)
            {
                if (aint1[i] == biome_ids::river)
                {
                    if (aint[i] == biome_ids::icePlains)
                    {
                        aint2[i] = biome_ids::frozenRiver;
                    }
                    else if (aint[i] != biome_ids::mushroomIsland && aint[i] != biome_ids::mushroomIslandShore)
                    {
                        aint2[i] = aint1[i] & 255;
                    }
                    else
                    {
                        aint2[i] = biome_ids::mushroomIslandShore;
                    }
                }
                else
                {
                    aint2[i] = aint[i];
                }
            }
            else
            {
                aint2[i] = aint[i];
            }
        }

        return aint2;
    }
};

class GenLayerVoronoiZoom final : public GenLayer {
public:
    GenLayerVoronoiZoom(std::int64_t seed,Layer parent):GenLayer(seed,std::move(parent)){}
    IntArray getInts(int areaX, int areaY, int areaWidth, int areaHeight) override {
        areaX = areaX - 2;
        areaY = areaY - 2;
        int i = floorPower2(areaX,2);
        int j = floorPower2(areaY,2);
        int k = (areaWidth >> 2) + 2;
        int l = (areaHeight >> 2) + 2;
        IntArray aint = this->parent->getInts(i, j, k, l);
        int i1 = (k - 1) << 2;
        int j1 = (l - 1) << 2;
        IntArray aint1 = makeArray(i1 * j1);

        for (int k1 = 0; k1 < l - 1; ++k1)
        {
            int l1 = 0;
            int i2 = aint[l1 + 0 + (k1 + 0) * k];

            for (int j2 = aint[l1 + 0 + (k1 + 1) * k]; l1 < k - 1; ++l1)
            {

                this->initChunkSeed((std::int64_t(l1 + i) * 4), (std::int64_t(k1 + j) * 4));
                double d1 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6;
                double d2 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6;
                this->initChunkSeed((std::int64_t(l1 + i + 1) * 4), (std::int64_t(k1 + j) * 4));
                double d3 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6 + 4.0;
                double d4 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6;
                this->initChunkSeed((std::int64_t(l1 + i) * 4), (std::int64_t(k1 + j + 1) * 4));
                double d5 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6;
                double d6 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6 + 4.0;
                this->initChunkSeed((std::int64_t(l1 + i + 1) * 4), (std::int64_t(k1 + j + 1) * 4));
                double d7 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6 + 4.0;
                double d8 = ((double)this->nextInt(1024) / 1024.0 - 0.5) * 3.6 + 4.0;
                int k2 = aint[l1 + 1 + (k1 + 0) * k] & 255;
                int l2 = aint[l1 + 1 + (k1 + 1) * k] & 255;

                for (int i3 = 0; i3 < 4; ++i3)
                {
                    int j3 = ((k1 << 2) + i3) * i1 + (l1 << 2);

                    for (int k3 = 0; k3 < 4; ++k3)
                    {
                        double d9 = ((double)i3 - d2) * ((double)i3 - d2) + ((double)k3 - d1) * ((double)k3 - d1);
                        double d10 = ((double)i3 - d4) * ((double)i3 - d4) + ((double)k3 - d3) * ((double)k3 - d3);
                        double d11 = ((double)i3 - d6) * ((double)i3 - d6) + ((double)k3 - d5) * ((double)k3 - d5);
                        double d12 = ((double)i3 - d8) * ((double)i3 - d8) + ((double)k3 - d7) * ((double)k3 - d7);

                        if (d9 < d10 && d9 < d11 && d9 < d12)
                        {
                            aint1[j3++] = i2;
                        }
                        else if (d10 < d9 && d10 < d11 && d10 < d12)
                        {
                            aint1[j3++] = k2;
                        }
                        else if (d11 < d9 && d11 < d10 && d11 < d12)
                        {
                            aint1[j3++] = j2;
                        }
                        else
                        {
                            aint1[j3++] = l2;
                        }
                    }
                }

                i2 = k2;
                j2 = l2;
            }
        }

        IntArray aint2 = makeArray(areaWidth * areaHeight);

        for (int l3 = 0; l3 < areaHeight; ++l3)
        {
            copyArray(aint1, (l3 + (areaY & 3)) * i1 + (areaX & 3), aint2, l3 * areaWidth, areaWidth);
        }

        return aint2;
    }
};
