#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdint>

constexpr int NUMBER_OF_COMPUTORS = 676; 
#define QUORUM (NUMBER_OF_COMPUTORS * 2 / 3 + 1)
#define MAX_INPUT_SIZE 1024ULL
#define ISSUANCE_RATE 1000000000000LL
#define MAX_AMOUNT (ISSUANCE_RATE * 1000ULL)
#define MAX_SUPPLY (ISSUANCE_RATE * 200ULL)

constexpr size_t UINT64_SIZE = sizeof(uint64_t);

void calculateRev(unsigned long long* revenueScore, unsigned long long* computorRev)
{
    // Sort revenue scores to get lowest score of quorum
    unsigned long long sortedRevenueScore[QUORUM + 1];
    memset(sortedRevenueScore, 0, sizeof(sortedRevenueScore));
    for (unsigned short computorIndex = 0; computorIndex < NUMBER_OF_COMPUTORS; computorIndex++)
    {
        sortedRevenueScore[QUORUM] = revenueScore[computorIndex];
        unsigned int i = QUORUM;
        while (i
            && sortedRevenueScore[i - 1] < sortedRevenueScore[i])
        {
            const unsigned long long tmp = sortedRevenueScore[i - 1];
            sortedRevenueScore[i - 1] = sortedRevenueScore[i];
            sortedRevenueScore[i--] = tmp;
        }
    }
    if (!sortedRevenueScore[QUORUM - 1])
    {
        sortedRevenueScore[QUORUM - 1] = 1;
    }

    // Compute revenue of computors and arbitrator
    long long arbitratorRevenue = ISSUANCE_RATE;
    constexpr long long issuancePerComputor = ISSUANCE_RATE / NUMBER_OF_COMPUTORS;
    constexpr long long scalingThreshold = 0xFFFFFFFFFFFFFFFFULL / issuancePerComputor;
    //static_assert(MAX_NUMBER_OF_TICKS_PER_EPOCH <= 605020, "Redefine scalingFactor");
    // maxRevenueScore for 605020 ticks = ((7099 * 605020) / 676) * 605020 * 675
    constexpr unsigned scalingFactor = 208100; // >= (maxRevenueScore600kTicks / 0xFFFFFFFFFFFFFFFFULL) * issuancePerComputor =(approx)= 208078.5
    for (unsigned int computorIndex = 0; computorIndex < NUMBER_OF_COMPUTORS; computorIndex++)
    {
        // Compute initial computor revenue, reducing arbitrator revenue
        long long revenue;
        if (revenueScore[computorIndex] >= sortedRevenueScore[QUORUM - 1])
            revenue = issuancePerComputor;
        else
        {
            if (revenueScore[computorIndex] > scalingThreshold)
            {
                // scale down to prevent overflow, then scale back up after division
                unsigned long long scaledRev = revenueScore[computorIndex] / scalingFactor;
                revenue = ((issuancePerComputor * scaledRev) / sortedRevenueScore[QUORUM - 1]);
                revenue *= scalingFactor;
            }
            else
            {
                revenue = ((issuancePerComputor * ((unsigned long long)revenueScore[computorIndex])) / sortedRevenueScore[QUORUM - 1]);
            }
        }
        computorRev[computorIndex] = revenue;
    }

}

struct CustomMiningRev
{
    unsigned long long revenueOldScore[NUMBER_OF_COMPUTORS]; // vote_count * tx
    unsigned long long customMiningShareCount[NUMBER_OF_COMPUTORS];

    unsigned long long customMiningScore[NUMBER_OF_COMPUTORS];

    unsigned long long computorOldRev[NUMBER_OF_COMPUTORS];
    unsigned long long computorNewRev[NUMBER_OF_COMPUTORS];

    void revFormula()
    {
        // Formula: newScore =  vote_count * tx * customMiningShare = revenueOldScore * customMiningShare
        for (unsigned short computorIndex = 0; computorIndex < NUMBER_OF_COMPUTORS; computorIndex++)
        {
            unsigned long long shareCoutn = customMiningShareCount[computorIndex];
            customMiningScore[computorIndex] = revenueOldScore[computorIndex] * shareCoutn;
        }

        // Calculate revenue
        calculateRev(revenueOldScore, computorOldRev);
        calculateRev(customMiningScore, computorNewRev);
    }
};



void readCustomMiningRev(const std::string& input_file, CustomMiningRev& custom_mining_rev)
{
    std::ifstream file(input_file, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + input_file);
    }

    // Read old_final_score
    for (int i = 0; i < NUMBER_OF_COMPUTORS; ++i)
    {
        file.read(reinterpret_cast<char*>(&custom_mining_rev.revenueOldScore[i]), UINT64_SIZE);
        if (file.gcount() != UINT64_SIZE)
        {
            throw std::runtime_error("Unexpected end of file while reading old_final_score");
        }
    }

    // Read custom_mining_score
    for (int i = 0; i < NUMBER_OF_COMPUTORS; ++i)
    {
        file.read(reinterpret_cast<char*>(&custom_mining_rev.customMiningShareCount[i]), UINT64_SIZE);
        if (file.gcount() != UINT64_SIZE)
        {
            throw std::runtime_error("Unexpected end of file while reading custom_mining_score");
        }
    }


}

void writeCustomMiningRevToCSV(const std::string& output_file, const CustomMiningRev& rev)
{
    std::ofstream out(output_file);
    if (!out.is_open())
    {
        throw std::runtime_error("Failed to open file: " + output_file);
    }

    // Write CSV header
    out << "Index,OldFinalScore,CustomMiningShareCount,OldRev,NewRev\n";

    // Write data rows
    for (int i = 0; i < NUMBER_OF_COMPUTORS; ++i)
    {
        out << i << "," << rev.revenueOldScore[i] << "," << rev.customMiningShareCount[i]
            << "," << rev.computorOldRev[i] << "," << rev.computorNewRev[i] << "\n";
    }

    out.close();
    std::cout << "CSV written to: " << output_file << std::endl;
}

int main(int argc, char* argv[])
{
    std::string inputFile, outputFile;
    if (argc != 3)
    {
        printf("Usage:   revenue_calculator [custom_revenue.eoe] [custom_revenue.csv] \n");
        return 1;
    }
    else
    {
        inputFile = argv[1];
        outputFile = argv[2];
    }

    CustomMiningRev custom_mining_rev;
    readCustomMiningRev(inputFile, custom_mining_rev);

    custom_mining_rev.revFormula();

    writeCustomMiningRevToCSV(outputFile, custom_mining_rev);

    return 0;
}
