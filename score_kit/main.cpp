#define EXPECT_TRUE

#include "standalone_score.h"

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <memory>
#include <immintrin.h>


static std::vector<unsigned long long> convertULLFromString(std::string& rStr)
{
    std::vector<unsigned long long> values;
    std::stringstream ss(rStr);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, '-'))
    {
        values.push_back(std::stoull(item));
    }
    return values;
}

// Function to read and parse the CSV file
static std::vector<std::vector<std::string>> readCSV(const std::string& filename)
{
    std::vector<std::vector<std::string>> data;
    std::ifstream file(filename);
    std::string line;

    // Read each line from the file
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string item;
        std::vector<std::string> parsedLine;

        // Parse each item separated by commas
        while (std::getline(ss, item, ','))
        {
            // Remove any spaces in the string
            item.erase(remove_if(item.begin(), item.end(), isspace), item.end());

            parsedLine.push_back(item);
        }
        data.push_back(parsedLine);
    }
    return data;
}

static std::vector<unsigned char> hexTo32Bytes(const std::string& hex, const int sizeInByte)
{
    if (hex.length() != sizeInByte * 2)
    {
        throw std::invalid_argument("Hex string length does not match the expected size");
    }

    std::vector<unsigned char> byteArray(sizeInByte);
    for (size_t i = 0; i < sizeInByte; ++i)
    {
        byteArray[i] = std::stoi(hex.substr(i * 2, 2), nullptr, 16);
    }

    return byteArray;
}

static const std::string COMMON_TEST_SAMPLES_FILE_NAME = "data/samples_20240815.csv";
static const std::string COMMON_TEST_SCORES_FILE_NAME = "data/scores_v5.csv";
static constexpr bool PRINT_DETAILED_INFO = false;

// set to 0 for run all available samples
// For profiling enable, run all available samples
static constexpr unsigned long long COMMON_TEST_NUMBER_OF_SAMPLES = 32;
static constexpr unsigned long long PROFILING_NUMBER_OF_SAMPLES = 32;


// set 0 for run maximum number of threads of the computer.
// For profiling enable, set it equal to deployment setting
static constexpr int MAX_NUMBER_OF_THREADS = 0;
static constexpr int MAX_NUMBER_OF_PROFILING_THREADS = 12;
static bool gCompareReference = false;

std::vector<std::vector<unsigned int>> gScoresGroundTruth;
std::atomic<unsigned long long> gScoreProcessingTime(0);
std::vector<std::vector<unsigned char>> gMiningSeeds;
std::vector<std::vector<unsigned char>> gPublicKeys;
std::vector<std::vector<unsigned char>> gNonces;


using ScoreKitProfile = ScoreKit<NUMBER_OF_INPUT_NEURONS, NUMBER_OF_OUTPUT_NEURONS, NUMBER_OF_TICKS, NUMBER_OF_NEIGHBORS, POPULATION_THRESHOLD, NUMBER_OF_MUTATIONS, SOLUTION_THRESHOLD_DEFAULT>;
std::unique_ptr<ScoreKitProfile> gClangScoreProfile;

std::map<int, int> settingCountMap;
using ScoreVariants = std::tuple<
    ScoreKit<64, 64, 50, 64, 178, 50, 36>,
    ScoreKit<256, 256, 120, 256, 612, 100, 171>,
    ScoreKit<512, 512, 150, 512, 1174, 150, 300>,
    ScoreKit<1024, 1024, 200, 1024, 3000, 200, 600>
>;
std::vector<std::tuple<int, int, int>> SETTING_FAILURE_LIST;
std::mutex matchingMutex;

template<typename ScoreType>
void runTemplateVariant(int templateIndex, int numThreads, int numSamples)
{
    std::vector<std::thread> workers;

    for (int t = 0; t < numThreads; ++t)
    {
        workers.emplace_back([t, numThreads, numSamples, templateIndex]()
            {
                for (int i = t; i < numSamples; i += numThreads)
                {
                    ScoreType scoreInstance(4);  // new instance per run
                    scoreInstance.initMiningData(gMiningSeeds[i].data());
                    int score = scoreInstance.computeScore(0, gPublicKeys[i].data(), gNonces[i].data());

                    // Comparision
                    if (gScoresGroundTruth[i][templateIndex] != score)
                    {
                        std::lock_guard<std::mutex> lock(matchingMutex);
                        SETTING_FAILURE_LIST.emplace_back(templateIndex, score, gScoresGroundTruth[i][templateIndex]);
                        std::cout << " - [FAILED] Setting " << templateIndex << ", " << i << ", " << score << " vs " << gScoresGroundTruth[i][templateIndex] << "\n";
                    }
                }
            });
    }

    for (auto& th : workers) th.join();
}

template <typename Tuple, typename F, std::size_t... Is>
void for_each_type_impl(F&& f, std::index_sequence<Is...>)
{
    (f.template operator() < std::tuple_element_t<Is, Tuple> > (Is), ...);
}

template <typename Tuple, typename F>
void for_each_type(F&& f)
{
    for_each_type_impl<Tuple>(std::forward<F>(f),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

struct Runner
{
    int numThreads;
    int numSamples;
    Runner(int n, int s) : numThreads(n), numSamples(s) {}

    template <typename ScoreType>
    void operator()(int index)
    {
        settingCountMap[index] = 1;
        runTemplateVariant<ScoreType>(index, numThreads, numSamples);
    }
};

static bool runMatch()
{
    SETTING_FAILURE_LIST.clear();

    int numberOfThreads = std::thread::hardware_concurrency();
    if (MAX_NUMBER_OF_THREADS > 0)
    {
        numberOfThreads = numberOfThreads > MAX_NUMBER_OF_THREADS ? MAX_NUMBER_OF_THREADS : numberOfThreads;
    }

    auto sampleString = readCSV(COMMON_TEST_SAMPLES_FILE_NAME);
    auto scoresString = readCSV(COMMON_TEST_SCORES_FILE_NAME);
    int numberOfSamplesReadFromFile = (int)sampleString.size();
    int numberOfSamples = numberOfSamplesReadFromFile;
    int requestedNumberOfSamples = COMMON_TEST_NUMBER_OF_SAMPLES;

    if (requestedNumberOfSamples > 0)
    {
        numberOfSamples = std::min(requestedNumberOfSamples, numberOfSamples);
    }


    // Init the data samples
    gMiningSeeds.resize(numberOfSamples);
    gPublicKeys.resize(numberOfSamples);
    gNonces.resize(numberOfSamples);

    // Reading the input samples
    for (unsigned long long i = 0; i < numberOfSamples; ++i)
    {
        gMiningSeeds[i] = hexTo32Bytes(sampleString[i][0], 32);
        gPublicKeys[i] = hexTo32Bytes(sampleString[i][1], 32);
        gNonces[i] = hexTo32Bytes(sampleString[i][2], 32);
    }

    // Read the gt
    auto scoreHeader = scoresString[0];
    // Read the groudtruth scores and init result scores
    numberOfSamples = std::min(numberOfSamples, (int)(scoresString.size() - 1));
    gScoresGroundTruth.resize(numberOfSamples);
    for (size_t i = 0; i < numberOfSamples; ++i)
    {
        auto scoresStr = scoresString[i + 1];
        size_t scoreSize = scoresStr.size();
        for (size_t j = 0; j < scoreSize; ++j)
        {
            gScoresGroundTruth[i].push_back(std::stoi(scoresStr[j]));
        }
    }

    // Run
    std::cout << "Run match test with " << numberOfSamples << " samples. Threads: " << numberOfThreads << std::endl;
    for_each_type<ScoreVariants>(Runner{ numberOfThreads, numberOfSamples });

    // Check for matching result
    if (SETTING_FAILURE_LIST.size() > 0)
    {
        return false;
    }

    return true;
}

static void processElementWithPerformance(unsigned char* publicKey, unsigned char* nonce, int threadId , int sampleIndex)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    unsigned int score_value = gClangScoreProfile->computeScore(threadId, publicKey, nonce);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto d = t1 - t0;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();

    gScoreProcessingTime += elapsed;
}

static void runPerformance()
{
    gClangScoreProfile.reset(new ScoreKitProfile(MAX_NUMBER_OF_PROFILING_THREADS));


    int numberOfSamples = PROFILING_NUMBER_OF_SAMPLES;

    unsigned char miningSeeds[32];
    std::vector< std::vector<unsigned char>> publicKeys(numberOfSamples);
    std::vector< std::vector<unsigned char>> nonces(numberOfSamples);

    // Random the input samples
    _rdrand64_step((unsigned long long*) & miningSeeds[0]);
    _rdrand64_step((unsigned long long*) & miningSeeds[8]);
    _rdrand64_step((unsigned long long*) & miningSeeds[16]);
    _rdrand64_step((unsigned long long*) & miningSeeds[24]);

    for (unsigned long long i = 0; i < numberOfSamples; ++i)
    {
        publicKeys[i].resize(32);
        nonces[i].resize(32);

        _rdrand64_step((unsigned long long*) & publicKeys[i][0]);
        _rdrand64_step((unsigned long long*) & publicKeys[i][8]);
        _rdrand64_step((unsigned long long*) & publicKeys[i][16]);
        _rdrand64_step((unsigned long long*) & publicKeys[i][24]);

        _rdrand64_step((unsigned long long*) & nonces[i][0]);
        _rdrand64_step((unsigned long long*) & nonces[i][8]);
        _rdrand64_step((unsigned long long*) & nonces[i][16]);
        _rdrand64_step((unsigned long long*) & nonces[i][24]);

        
    }

    gClangScoreProfile->initMiningData(&miningSeeds[0]);
    std::vector<std::thread> scoreThreads;
    for (int threadId = 0; threadId < MAX_NUMBER_OF_PROFILING_THREADS; ++threadId)
    {
        scoreThreads.emplace_back([&, threadId]()
            {
                for (int i = threadId; i < numberOfSamples; i += MAX_NUMBER_OF_PROFILING_THREADS)
                {
                    processElementWithPerformance(publicKeys[i].data(), nonces[i].data(), threadId, i);
                }
            });
    }

    for (auto& th : scoreThreads)
    {
        th.join();
    }
    std::cout << "Avg processing time: " << (float)gScoreProcessingTime.load() / numberOfSamples << " ms\n";
}

int main()
{
#if defined (__AVX512F__)
    initAVX512K12();
#endif

    // Matching result
    if (runMatch())
    {
        std::cout << "[SUCCESS] Mached result. Total " << settingCountMap.size() << " settings check." << "\n";
    }
    else
    {
        std::cout << "[FAILED] Unmached result. Skip profiling run!" << "\n";
        return 1;
    }
    
    // Profiling
    runPerformance();

    return 0;
}
