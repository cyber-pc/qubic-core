#include "standalone_score.h"

struct EFI_TIME
{
    unsigned short Year;
    unsigned char Month;
    unsigned char Day;
    unsigned char Hour;
    unsigned char Minute;
    unsigned char Second;
    unsigned char Pad1;
    unsigned int Nanosecond;
    short TimeZone;
    unsigned char Daylight;
    unsigned char Pad2;
}  utcTime;

#include "mining/score_engine.h"

bool isZero(unsigned char* randomSeed, unsigned int sizeInBytes)
{
    for (unsigned int i = 0; i < sizeInBytes; ++i)
    {
        if (randomSeed[i])
        {
            return false;
        }
    }
    return true;
}

void initAVX512K12()
{
#if defined (__AVX512F__) && !GENERIC_K12
    initAVX512KangarooTwelveConstants();
#endif
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long poplulationThreshold,
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
void ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, poplulationThreshold, numberOfMutations, solutionThreshold>::initPool(const unsigned char* miningSeed)
{
    // Init random2 pool with mining seed
    score_engine::generateRandom2Pool(miningSeed, state, externalPoolVec);
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long poplulationThreshold,
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
void ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, poplulationThreshold, numberOfMutations, solutionThreshold>::initMiningData(unsigned char* randomSeed)
{
    // Below assume when a new mining seed is provided, we need to re-calculate the random2 pool
    // Check if random pool need to be re-generated
    if (!isZero(randomSeed, 32))
    {
        initPool(randomSeed);
    }
    //copyMem(currentRandomSeed, randomSeed, 32);

    copyMem(poolVec, externalPoolVec, score_engine::POOL_VEC_PADDING_SIZE);
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long poplulationThreshold,    // 2M
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, poplulationThreshold, numberOfMutations, solutionThreshold>::ScoreKit(int count)
{
    numberOfInstances = count;

    initMemory();
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long poplulationThreshold,    // S
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, poplulationThreshold, numberOfMutations, solutionThreshold>::~ScoreKit()
{
    freeMemory();
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long populationThreshold,
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
void ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold>
    ::freeMemory()
{
    delete[] pImpl;
    delete[] state;
    delete[] externalPoolVec;
    delete[] poolVec;
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long populationThreshold,
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
bool ScoreKit< numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold>
    ::initMemory()
{
    
    state = new unsigned char[score_engine::STATE_SIZE];
    externalPoolVec = new unsigned char[score_engine::POOL_VEC_PADDING_SIZE];
    poolVec = new unsigned char[score_engine::POOL_VEC_PADDING_SIZE];

    pImpl = new score_engine::ScoreEngine
        <numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold>[numberOfInstances];

    // Make sure all padding data is set as zeros
    setMem(pImpl, numberOfInstances * sizeof(score_engine::ScoreEngine<numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold >), 0);

    return true;
}

template <
    unsigned long long numberOfInputNeurons, // K
    unsigned long long numberOfOutputNeurons,// L
    unsigned long long numberOfTicks,        // N
    unsigned long long numberOfNeighbors,    // 2M
    unsigned long long populationThreshold,    // 2M
    unsigned long long numberOfMutations,    // S
    unsigned int solutionThreshold
>
int ScoreKit< numberOfInputNeurons, numberOfOutputNeurons,numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold>
    ::computeScore(int processorIdx, const unsigned char* publicKey, const unsigned char* nonce)
{
    int solutionBufIdx = processorIdx % numberOfInstances;
    int score = 0;
    score = ((score_engine::ScoreEngine<numberOfInputNeurons, numberOfOutputNeurons, numberOfTicks, numberOfNeighbors, populationThreshold, numberOfMutations, solutionThreshold >*)pImpl)[solutionBufIdx].computeScore(
        publicKey, nonce, poolVec);
    return score;
}

template class ScoreKit<64, 64, 50, 64, 178, 50, 36>;
template class ScoreKit<256, 256, 120, 256, 612, 100, 171>;
template class ScoreKit<512, 512, 150, 512, 1174, 150, 300>;
template class ScoreKit<1024, 1024, 200, 1024, 3000, 200, 600>;
template class ScoreKit<NUMBER_OF_INPUT_NEURONS, NUMBER_OF_OUTPUT_NEURONS, NUMBER_OF_TICKS, NUMBER_OF_NEIGHBORS, POPULATION_THRESHOLD, NUMBER_OF_MUTATIONS, SOLUTION_THRESHOLD_DEFAULT>;

