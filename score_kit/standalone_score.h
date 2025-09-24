#pragma once


void initAVX512K12();

static constexpr unsigned long long NUMBER_OF_INPUT_NEURONS = 512;     // K
static constexpr unsigned long long NUMBER_OF_OUTPUT_NEURONS = 512;    // L
static constexpr unsigned long long NUMBER_OF_TICKS = 1000;               // N
static constexpr unsigned long long NUMBER_OF_NEIGHBORS = 728;    // 2M. Must be divided by 2
static constexpr unsigned long long NUMBER_OF_MUTATIONS = 150;
static constexpr unsigned long long POPULATION_THRESHOLD = NUMBER_OF_INPUT_NEURONS + NUMBER_OF_OUTPUT_NEURONS + NUMBER_OF_MUTATIONS; // P
static constexpr unsigned int SOLUTION_THRESHOLD_DEFAULT = 321;

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long populationThreshold,
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
class ScoreKit
{
public:
    explicit ScoreKit(int numberOfInstances);
    ~ScoreKit();

    void initMiningData(unsigned char* randomSeed);
    void freeMemory();
    int computeScore(int solutionBufIdx, const unsigned char* publicKey, const unsigned char* nonce);

private:
    bool initMemory();
    void initPool(const unsigned char* miningSeed);

    unsigned char* state;
    unsigned char* externalPoolVec;
    unsigned char* poolVec;
    void* pImpl;
    int numberOfInstances;
};

extern template class ScoreKit<64, 64, 50, 64, 178, 50, 36>;
extern template class ScoreKit<256, 256, 120, 256, 612, 100, 171>;
extern template class ScoreKit<512, 512, 150, 512, 1174, 150, 300>;
extern template class ScoreKit<1024, 1024, 200, 1024, 3000, 200, 600>;
extern template class ScoreKit<NUMBER_OF_INPUT_NEURONS, NUMBER_OF_OUTPUT_NEURONS, NUMBER_OF_TICKS, NUMBER_OF_NEIGHBORS, POPULATION_THRESHOLD, NUMBER_OF_MUTATIONS, SOLUTION_THRESHOLD_DEFAULT>;

