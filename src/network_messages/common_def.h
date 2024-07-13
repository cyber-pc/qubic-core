#pragma once

#define SIGNATURE_SIZE 64
#define NUMBER_OF_TRANSACTIONS_PER_TICK 1024 // Must be 2^N
#define MAX_NUMBER_OF_CONTRACTS 1024 // Must be 1024
#define NUMBER_OF_COMPUTORS 676
#define NUMBER_OF_EXCHANGED_PEERS 4
#define SPECTRUM_DEPTH 24 // Defines SPECTRUM_CAPACITY (1 << SPECTRUM_DEPTH)

#define ASSETS_CAPACITY 0x1000000ULL // Must be 2^N
#define ASSETS_DEPTH 24 // Is derived from ASSETS_CAPACITY (=N)

#define MAX_INPUT_SIZE 1024ULL
#define ISSUANCE_RATE 1000000000000LL
#define MAX_AMOUNT (ISSUANCE_RATE * 1000ULL)


// If you want to use the network_meassges directory in your project without dependencies to other code,
// you may define NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES before including any header or change the
// following line to "#if 1#.
#if defined(NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES)

#include <intrin.h>

typedef union m256i
{
    __int8              m256i_i8[32];
    __int16             m256i_i16[16];
    __int32             m256i_i32[8];
    __int64             m256i_i64[4];
    unsigned __int8     m256i_u8[32];
    unsigned __int16    m256i_u16[16];
    unsigned __int32    m256i_u32[8];
    unsigned __int64    m256i_u64[4];
} m256i;

#else

#include "../platform/m256.h"

#endif

typedef union IPv4Address
{
    unsigned __int8     u8[4];
    unsigned __int32    u32;
} IPv4Address;

static_assert(sizeof(IPv4Address) == 4, "Unexpected size!");

static inline bool operator==(const IPv4Address& a, const IPv4Address& b)
{
    return a.u32 == b.u32;
}

static inline bool operator!=(const IPv4Address& a, const IPv4Address& b)
{
    return a.u32 != b.u32;
}

// Compute the siblings array of each level of tree. This function is not thread safe
// make sure resource protection is handled outside
template <unsigned int depth>
static void getSiblings(int digestIndex, const m256i* digests, m256i siblings[depth])
{
    const unsigned int capacity = (1ULL << depth);
    int siblingIndex = digestIndex;
    unsigned int digestOffset = 0;
    for (unsigned int j = 0; j < depth; j++)
    {
        siblings[j] = digests[digestOffset + (siblingIndex ^ 1)];
        digestOffset += (capacity >> j);
        siblingIndex >>= 1;
    }
}
