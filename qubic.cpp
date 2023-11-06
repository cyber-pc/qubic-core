#include <intrin.h>

#include "smart_contracts.h"

#include "private_settings.h"
#include "public_settings.h"



////////// C++ helpers \\\\\\\\\\

#define EQUAL(a, b) (_mm256_movemask_epi8(_mm256_cmpeq_epi64(a, b)) == 0xFFFFFFFF)
#define ACQUIRE(lock) while (_InterlockedCompareExchange8(&lock, 1, 0)) _mm_pause()
#define RELEASE(lock) lock = 0

// TODO: Use "long long" instead of "int" for DB indices


#include "uefi.h"

#include "text_output.h"
#include "time.h"

#include "kangaroo_twelve.h"
#include "four_q.h"



////////// Qubic \\\\\\\\\\

#define ASSETS_CAPACITY 0x1000000ULL // Must be 2^N
#define ASSETS_DEPTH 24 // Is derived from ASSETS_CAPACITY (=N)
#define BUFFER_SIZE 33554432
#define CONTRACT_STATES_DEPTH 10 // Is derived from MAX_NUMBER_OF_CONTRACTS (=N)
#define TARGET_TICK_DURATION 4000
#define TICK_REQUESTING_PERIOD 500
#define DEJAVU_SWAP_LIMIT 1000000
#define DISSEMINATION_MULTIPLIER 4
#define FIRST_TICK_TRANSACTION_OFFSET sizeof(unsigned long long)
#define ISSUANCE_RATE 1000000000000LL
#define MAX_AMOUNT (ISSUANCE_RATE * 1000ULL)
#define MAX_INPUT_SIZE (MAX_TRANSACTION_SIZE - (sizeof(Transaction) + SIGNATURE_SIZE))
#define MAX_NUMBER_OF_MINERS 8192
#define NUMBER_OF_MINER_SOLUTION_FLAGS 0x100000000
#define MAX_NUMBER_OF_PROCESSORS 32
#define MAX_NUMBER_OF_PUBLIC_PEERS 1024
#define MAX_NUMBER_OF_SOLUTIONS 65536 // Must be 2^N
#define MAX_TRANSACTION_SIZE 1024ULL
#define MAX_MESSAGE_PAYLOAD_SIZE MAX_TRANSACTION_SIZE
#define NUMBER_OF_COMPUTORS 676
#define MAX_NUMBER_OF_TICKS_PER_EPOCH (((((60 * 60 * 24 * 7) / (TARGET_TICK_DURATION / 1000)) + NUMBER_OF_COMPUTORS - 1) / NUMBER_OF_COMPUTORS) * NUMBER_OF_COMPUTORS)
#define MAX_CONTRACT_STATE_SIZE 1073741824
#define MAX_UNIVERSE_SIZE 1073741824
#define MESSAGE_DISSEMINATION_THRESHOLD 1000000000
#define MESSAGE_TYPE_SOLUTION 0
#define NUMBER_OF_EXCHANGED_PEERS 4
#define NUMBER_OF_OUTGOING_CONNECTIONS 4
#define NUMBER_OF_INCOMING_CONNECTIONS 28
#define NUMBER_OF_TRANSACTIONS_PER_TICK 1024 // Must be 2^N
#define PEER_REFRESHING_PERIOD 120000
#define PORT 21841
#define QUORUM (NUMBER_OF_COMPUTORS * 2 / 3 + 1)
#define READING_CHUNK_SIZE 1048576
#define WRITING_CHUNK_SIZE 1048576
#define REQUEST_QUEUE_BUFFER_SIZE 1073741824
#define REQUEST_QUEUE_LENGTH 65536 // Must be 65536
#define RESPONSE_QUEUE_BUFFER_SIZE 1073741824
#define RESPONSE_QUEUE_LENGTH 65536 // Must be 65536
#define SIGNATURE_SIZE 64
#define SPECTRUM_CAPACITY 0x1000000ULL // Must be 2^N
#define SPECTRUM_DEPTH 24 // Is derived from SPECTRUM_CAPACITY (=N)
#define SYSTEM_DATA_SAVING_PERIOD 300000
#define TICK_TRANSACTIONS_PUBLICATION_OFFSET 2 // Must be only 2
#define MIN_MINING_SOLUTIONS_PUBLICATION_OFFSET 3 // Must be 3+
#define TIME_ACCURACY 60000
#define TRANSACTION_SPARSENESS 6
#define VOLUME_LABEL L"Qubic"

#define EMPTY 0
#define ISSUANCE 1
#define OWNERSHIP 2
#define POSSESSION 3

#define AMPERE 0
#define CANDELA 1
#define KELVIN 2
#define KILOGRAM 3
#define METER 4
#define MOLE 5
#define SECOND 6

struct Asset
{
    union
    {
        struct
        {
            unsigned char publicKey[32];
            unsigned char type;
            char name[7]; // Capital letters + digits
            char numberOfDecimalPlaces;
            char unitOfMeasurement[7]; // Powers of the corresponding SI base units going in alphabetical order
        } issuance;

        struct
        {
            unsigned char publicKey[32];
            unsigned char type;
            char padding[1];
            unsigned short managingContractIndex;
            unsigned int issuanceIndex;
            long long numberOfUnits;
        } ownership;

        struct
        {
            unsigned char publicKey[32];
            unsigned char type;
            char padding[1];
            unsigned short managingContractIndex;
            unsigned int ownershipIndex;
            long long numberOfUnits;
        } possession;
    } varStruct;
};

typedef struct
{
    EFI_TCP4_PROTOCOL* tcp4Protocol;
    EFI_TCP4_LISTEN_TOKEN connectAcceptToken;
    unsigned char address[4];
    void* receiveBuffer;
    EFI_TCP4_RECEIVE_DATA receiveData;
    EFI_TCP4_IO_TOKEN receiveToken;
    EFI_TCP4_TRANSMIT_DATA transmitData;
    EFI_TCP4_IO_TOKEN transmitToken;
    char* dataToTransmit;
    unsigned int dataToTransmitSize;
    BOOLEAN isConnectingAccepting;
    BOOLEAN isConnectedAccepted;
    BOOLEAN isReceiving, isTransmitting;
    BOOLEAN exchangedPublicPeers;
    BOOLEAN isClosing;
} Peer;

typedef struct
{
    bool isVerified;
    unsigned char address[4];
} PublicPeer;

typedef struct
{
    EFI_EVENT event;
    Peer* peer;
    void* buffer;
} Processor;

struct RequestResponseHeader
{
private:
    unsigned char _size[3];
    unsigned char _type;
    unsigned int _dejavu;

public:
    inline unsigned int size()
    {
        return (*((unsigned int*)_size)) & 0xFFFFFF;
    }

    inline void setSize(unsigned int size)
    {
        _size[0] = (unsigned char)size;
        _size[1] = (unsigned char)(size >> 8);
        _size[2] = (unsigned char)(size >> 16);
    }

    inline bool isDejavuZero()
    {
        return !_dejavu;
    }

    inline unsigned int dejavu()
    {
        return _dejavu;
    }

    inline void setDejavu(unsigned int dejavu)
    {
        _dejavu = dejavu;
    }

    inline void randomizeDejavu()
    {
        _rdrand32_step(&_dejavu);
        if (!_dejavu)
        {
            _dejavu = 1;
        }
    }

    inline unsigned char type()
    {
        return _type;
    }

    inline void setType(const unsigned char type)
    {
        _type = type;
    }
};

#define EXCHANGE_PUBLIC_PEERS 0

typedef struct
{
    unsigned char peers[NUMBER_OF_EXCHANGED_PEERS][4];
} ExchangePublicPeers;

#define BROADCAST_MESSAGE 1

typedef struct
{
    unsigned char sourcePublicKey[32];
    unsigned char destinationPublicKey[32];
    unsigned char gammingNonce[32];
} Message;

#define BROADCAST_COMPUTORS 2

typedef struct
{
    // TODO: Padding
    unsigned short epoch;
    unsigned char publicKeys[NUMBER_OF_COMPUTORS][32];
    unsigned char signature[SIGNATURE_SIZE];
} Computors;

typedef struct
{
    Computors computors;
} BroadcastComputors;

#define BROADCAST_TICK 3

typedef struct
{
    unsigned short computorIndex;
    unsigned short epoch;
    unsigned int tick;

    unsigned short millisecond;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char year;

    unsigned long long prevResourceTestingDigest;
    unsigned long long saltedResourceTestingDigest;

    unsigned char prevSpectrumDigest[32];
    unsigned char prevUniverseDigest[32];
    unsigned char prevComputerDigest[32];
    unsigned char saltedSpectrumDigest[32];
    unsigned char saltedUniverseDigest[32];
    unsigned char saltedComputerDigest[32];

    unsigned char transactionDigest[32];
    unsigned char expectedNextTickTransactionDigest[32];

    unsigned char signature[SIGNATURE_SIZE];
} Tick;

typedef struct
{
    unsigned short millisecond;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char year;
    unsigned char prevSpectrumDigest[32];
    unsigned char prevUniverseDigest[32];
    unsigned char prevComputerDigest[32];
    unsigned char transactionDigest[32];
} TickEssence;

typedef struct
{
    Tick tick;
} BroadcastTick;

#define BROADCAST_FUTURE_TICK_DATA 8

typedef struct
{
    unsigned short computorIndex;
    unsigned short epoch;
    unsigned int tick;

    unsigned short millisecond;
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char year;

    union
    {
        struct
        {
            unsigned char uriSize;
            unsigned char uri[255];
        } proposal;
        struct
        {
            unsigned char zero;
            unsigned char votes[(NUMBER_OF_COMPUTORS * 3 + 7) / 8];
            unsigned char quasiRandomNumber;
        } ballot;
    } varStruct;

    unsigned char timelock[32];
    unsigned char transactionDigests[NUMBER_OF_TRANSACTIONS_PER_TICK][32];
    long long contractFees[MAX_NUMBER_OF_CONTRACTS];

    unsigned char signature[SIGNATURE_SIZE];
} TickData;

typedef struct
{
    TickData tickData;
} BroadcastFutureTickData;

#define REQUEST_COMPUTORS 11

#define REQUEST_QUORUM_TICK 14

typedef struct
{
    unsigned int tick;
    unsigned char voteFlags[(NUMBER_OF_COMPUTORS + 7) / 8];
} RequestedQuorumTick;

typedef struct
{
    RequestedQuorumTick quorumTick;
} RequestQuorumTick;

#define REQUEST_TICK_DATA 16

typedef struct
{
    unsigned int tick;
} RequestedTickData;

typedef struct
{
    RequestedTickData requestedTickData;
} RequestTickData;

#define BROADCAST_TRANSACTION 24

typedef struct
{
    unsigned char sourcePublicKey[32];
    unsigned char destinationPublicKey[32];
    long long amount;
    unsigned int tick;
    unsigned short inputType;
    unsigned short inputSize;
} Transaction;

struct ContractIPOBid
{
    long long price;
    unsigned short quantity;
};

#define REQUEST_CURRENT_TICK_INFO 27

#define RESPOND_CURRENT_TICK_INFO 28

typedef struct
{
    unsigned short tickDuration;
    unsigned short epoch;
    unsigned int tick;
    unsigned short numberOfAlignedVotes;
    unsigned short numberOfMisalignedVotes;
} CurrentTickInfo;

#define REQUEST_TICK_TRANSACTIONS 29

typedef struct
{
    unsigned int tick;
    unsigned char transactionFlags[NUMBER_OF_TRANSACTIONS_PER_TICK / 8];
} RequestedTickTransactions;

#define REQUEST_ENTITY 31

typedef struct
{
    unsigned char publicKey[32];
} RequestedEntity;

#define RESPOND_ENTITY 32

typedef struct
{
    ::Entity entity;
    unsigned int tick;
    int spectrumIndex;
    unsigned char siblings[SPECTRUM_DEPTH][32];
} RespondedEntity;

#define REQUEST_CONTRACT_IPO 33

typedef struct
{
    unsigned int contractIndex;
} RequestContractIPO;

#define RESPOND_CONTRACT_IPO 34

typedef struct
{
    unsigned int contractIndex;
    unsigned int tick;
    unsigned char publicKeys[NUMBER_OF_COMPUTORS][32];
    long long prices[NUMBER_OF_COMPUTORS];
} RespondContractIPO;

#define END_RESPONSE 35

#define REQUEST_ISSUED_ASSETS 36

typedef struct
{
    unsigned char publicKey[32];
} RequestIssuedAssets;

#define RESPOND_ISSUED_ASSETS 37

typedef struct
{
    Asset asset;
    unsigned int tick;
    // TODO: Add siblings
} RespondIssuedAssets;

#define REQUEST_OWNED_ASSETS 38

typedef struct
{
    unsigned char publicKey[32];
} RequestOwnedAssets;

#define RESPOND_OWNED_ASSETS 39

typedef struct
{
    Asset asset;
    Asset issuanceAsset;
    unsigned int tick;
    // TODO: Add siblings
} RespondOwnedAssets;

#define REQUEST_POSSESSED_ASSETS 40

typedef struct
{
    unsigned char publicKey[32];
} RequestPossessedAssets;

#define RESPOND_POSSESSED_ASSETS 41

typedef struct
{
    Asset asset;
    Asset ownershipAsset;
    Asset issuanceAsset;
    unsigned int tick;
    // TODO: Add siblings
} RespondPossessedAssets;

struct ComputorProposal
{
    unsigned char uriSize;
    unsigned char uri[255];
};
struct ComputorBallot
{
    unsigned char zero;
    unsigned char votes[(NUMBER_OF_COMPUTORS * 3 + 7) / 8];
    unsigned char quasiRandomNumber;
};

#define PROCESS_SPECIAL_COMMAND 255

struct SpecialCommand
{
    unsigned long long everIncreasingNonceAndCommandType;
};

#define SPECIAL_COMMAND_SHUT_DOWN 0ULL

#define SPECIAL_COMMAND_GET_PROPOSAL_AND_BALLOT_REQUEST 1ULL
struct SpecialCommandGetProposalAndBallotRequest
{
    unsigned long long everIncreasingNonceAndCommandType;
    unsigned short computorIndex;
    unsigned char padding[6];
    unsigned char signature[SIGNATURE_SIZE];
};

#define SPECIAL_COMMAND_GET_PROPOSAL_AND_BALLOT_RESPONSE 2ULL
struct SpecialCommandGetProposalAndBallotResponse
{
    unsigned long long everIncreasingNonceAndCommandType;
    unsigned short computorIndex;
    unsigned char padding[6];
    ComputorProposal proposal;
    ComputorBallot ballot;
};

#define SPECIAL_COMMAND_SET_PROPOSAL_AND_BALLOT_REQUEST 3ULL
struct SpecialCommandSetProposalAndBallotRequest
{
    unsigned long long everIncreasingNonceAndCommandType;
    unsigned short computorIndex;
    unsigned char padding[6];
    ComputorProposal proposal;
    ComputorBallot ballot;
    unsigned char signature[SIGNATURE_SIZE];
};

#define SPECIAL_COMMAND_SET_PROPOSAL_AND_BALLOT_RESPONSE 4ULL
struct SpecialCommandSetProposalAndBallotResponse
{
    unsigned long long everIncreasingNonceAndCommandType;
    unsigned short computorIndex;
    unsigned char padding[6];
};

static const unsigned short revenuePoints[1 + 1024] = { 0, 710, 1125, 1420, 1648, 1835, 1993, 2129, 2250, 2358, 2455, 2545, 2627, 2702, 2773, 2839, 2901, 2960, 3015, 3068, 3118, 3165, 3211, 3254, 3296, 3336, 3375, 3412, 3448, 3483, 3516, 3549, 3580, 3611, 3641, 3670, 3698, 3725, 3751, 3777, 3803, 3827, 3851, 3875, 3898, 3921, 3943, 3964, 3985, 4006, 4026, 4046, 4066, 4085, 4104, 4122, 4140, 4158, 4175, 4193, 4210, 4226, 4243, 4259, 4275, 4290, 4306, 4321, 4336, 4350, 4365, 4379, 4393, 4407, 4421, 4435, 4448, 4461, 4474, 4487, 4500, 4512, 4525, 4537, 4549, 4561, 4573, 4585, 4596, 4608, 4619, 4630, 4641, 4652, 4663, 4674, 4685, 4695, 4705, 4716, 4726, 4736, 4746, 4756, 4766, 4775, 4785, 4795, 4804, 4813, 4823, 4832, 4841, 4850, 4859, 4868, 4876, 4885, 4894, 4902, 4911, 4919, 4928, 4936, 4944, 4952, 4960, 4968, 4976, 4984, 4992, 5000, 5008, 5015, 5023, 5031, 5038, 5046, 5053, 5060, 5068, 5075, 5082, 5089, 5096, 5103, 5110, 5117, 5124, 5131, 5138, 5144, 5151, 5158, 5164, 5171, 5178, 5184, 5191, 5197, 5203, 5210, 5216, 5222, 5228, 5235, 5241, 5247, 5253, 5259, 5265, 5271, 5277, 5283, 5289, 5295, 5300, 5306, 5312, 5318, 5323, 5329, 5335, 5340, 5346, 5351, 5357, 5362, 5368, 5373, 5378, 5384, 5389, 5394, 5400, 5405, 5410, 5415, 5420, 5425, 5431, 5436, 5441, 5446, 5451, 5456, 5461, 5466, 5471, 5475, 5480, 5485, 5490, 5495, 5500, 5504, 5509, 5514, 5518, 5523, 5528, 5532, 5537, 5542, 5546, 5551, 5555, 5560, 5564, 5569, 5573, 5577, 5582, 5586, 5591, 5595, 5599, 5604, 5608, 5612, 5616, 5621, 5625, 5629, 5633, 5637, 5642, 5646, 5650, 5654, 5658, 5662, 5666, 5670, 5674, 5678, 5682, 5686, 5690, 5694, 5698, 5702, 5706, 5710, 5714, 5718, 5721, 5725, 5729, 5733, 5737, 5740, 5744, 5748, 5752, 5755, 5759, 5763, 5766, 5770, 5774, 5777, 5781, 5785, 5788, 5792, 5795, 5799, 5802, 5806, 5809, 5813, 5816, 5820, 5823, 5827, 5830, 5834, 5837, 5841, 5844, 5847, 5851, 5854, 5858, 5861, 5864, 5868, 5871, 5874, 5878, 5881, 5884, 5887, 5891, 5894, 5897, 5900, 5904, 5907, 5910, 5913, 5916, 5919, 5923, 5926, 5929, 5932, 5935, 5938, 5941, 5944, 5948, 5951, 5954, 5957, 5960, 5963, 5966, 5969, 5972, 5975, 5978, 5981, 5984, 5987, 5990, 5993, 5996, 5999, 6001, 6004, 6007, 6010, 6013, 6016, 6019, 6022, 6025, 6027, 6030, 6033, 6036, 6039, 6041, 6044, 6047, 6050, 6053, 6055, 6058, 6061, 6064, 6066, 6069, 6072, 6075, 6077, 6080, 6083, 6085, 6088, 6091, 6093, 6096, 6099, 6101, 6104, 6107, 6109, 6112, 6115, 6117, 6120, 6122, 6125, 6128, 6130, 6133, 6135, 6138, 6140, 6143, 6145, 6148, 6151, 6153, 6156, 6158, 6161, 6163, 6166, 6168, 6170, 6173, 6175, 6178, 6180, 6183, 6185, 6188, 6190, 6193, 6195, 6197, 6200, 6202, 6205, 6207, 6209, 6212, 6214, 6216, 6219, 6221, 6224, 6226, 6228, 6231, 6233, 6235, 6238, 6240, 6242, 6244, 6247, 6249, 6251, 6254, 6256, 6258, 6260, 6263, 6265, 6267, 6269, 6272, 6274, 6276, 6278, 6281, 6283, 6285, 6287, 6289, 6292, 6294, 6296, 6298, 6300, 6303, 6305, 6307, 6309, 6311, 6313, 6316, 6318, 6320, 6322, 6324, 6326, 6328, 6330, 6333, 6335, 6337, 6339, 6341, 6343, 6345, 6347, 6349, 6351, 6353, 6356, 6358, 6360, 6362, 6364, 6366, 6368, 6370, 6372, 6374, 6376, 6378, 6380, 6382, 6384, 6386, 6388, 6390, 6392, 6394, 6396, 6398, 6400, 6402, 6404, 6406, 6408, 6410, 6412, 6414, 6416, 6418, 6420, 6421, 6423, 6425, 6427, 6429, 6431, 6433, 6435, 6437, 6439, 6441, 6443, 6444, 6446, 6448, 6450, 6452, 6454, 6456, 6458, 6459, 6461, 6463, 6465, 6467, 6469, 6471, 6472, 6474, 6476, 6478, 6480, 6482, 6483, 6485, 6487, 6489, 6491, 6493, 6494, 6496, 6498, 6500, 6502, 6503, 6505, 6507, 6509, 6510, 6512, 6514, 6516, 6518, 6519, 6521, 6523, 6525, 6526, 6528, 6530, 6532, 6533, 6535, 6537, 6538, 6540, 6542, 6544, 6545, 6547, 6549, 6550, 6552, 6554, 6556, 6557, 6559, 6561, 6562, 6564, 6566, 6567, 6569, 6571, 6572, 6574, 6576, 6577, 6579, 6581, 6582, 6584, 6586, 6587, 6589, 6591, 6592, 6594, 6596, 6597, 6599, 6600, 6602, 6604, 6605, 6607, 6609, 6610, 6612, 6613, 6615, 6617, 6618, 6620, 6621, 6623, 6625, 6626, 6628, 6629, 6631, 6632, 6634, 6636, 6637, 6639, 6640, 6642, 6643, 6645, 6647, 6648, 6650, 6651, 6653, 6654, 6656, 6657, 6659, 6660, 6662, 6663, 6665, 6667, 6668, 6670, 6671, 6673, 6674, 6676, 6677, 6679, 6680, 6682, 6683, 6685, 6686, 6688, 6689, 6691, 6692, 6694, 6695, 6697, 6698, 6699, 6701, 6702, 6704, 6705, 6707, 6708, 6710, 6711, 6713, 6714, 6716, 6717, 6718, 6720, 6721, 6723, 6724, 6726, 6727, 6729, 6730, 6731, 6733, 6734, 6736, 6737, 6739, 6740, 6741, 6743, 6744, 6746, 6747, 6748, 6750, 6751, 6753, 6754, 6755, 6757, 6758, 6760, 6761, 6762, 6764, 6765, 6767, 6768, 6769, 6771, 6772, 6773, 6775, 6776, 6778, 6779, 6780, 6782, 6783, 6784, 6786, 6787, 6788, 6790, 6791, 6793, 6794, 6795, 6797, 6798, 6799, 6801, 6802, 6803, 6805, 6806, 6807, 6809, 6810, 6811, 6813, 6814, 6815, 6816, 6818, 6819, 6820, 6822, 6823, 6824, 6826, 6827, 6828, 6830, 6831, 6832, 6833, 6835, 6836, 6837, 6839, 6840, 6841, 6842, 6844, 6845, 6846, 6848, 6849, 6850, 6851, 6853, 6854, 6855, 6856, 6858, 6859, 6860, 6862, 6863, 6864, 6865, 6867, 6868, 6869, 6870, 6872, 6873, 6874, 6875, 6877, 6878, 6879, 6880, 6882, 6883, 6884, 6885, 6886, 6888, 6889, 6890, 6891, 6893, 6894, 6895, 6896, 6897, 6899, 6900, 6901, 6902, 6904, 6905, 6906, 6907, 6908, 6910, 6911, 6912, 6913, 6914, 6916, 6917, 6918, 6919, 6920, 6921, 6923, 6924, 6925, 6926, 6927, 6929, 6930, 6931, 6932, 6933, 6934, 6936, 6937, 6938, 6939, 6940, 6941, 6943, 6944, 6945, 6946, 6947, 6948, 6950, 6951, 6952, 6953, 6954, 6955, 6957, 6958, 6959, 6960, 6961, 6962, 6963, 6965, 6966, 6967, 6968, 6969, 6970, 6971, 6972, 6974, 6975, 6976, 6977, 6978, 6979, 6980, 6981, 6983, 6984, 6985, 6986, 6987, 6988, 6989, 6990, 6991, 6993, 6994, 6995, 6996, 6997, 6998, 6999, 7000, 7001, 7003, 7004, 7005, 7006, 7007, 7008, 7009, 7010, 7011, 7012, 7013, 7015, 7016, 7017, 7018, 7019, 7020, 7021, 7022, 7023, 7024, 7025, 7026, 7027, 7029, 7030, 7031, 7032, 7033, 7034, 7035, 7036, 7037, 7038, 7039, 7040, 7041, 7042, 7043, 7044, 7046, 7047, 7048, 7049, 7050, 7051, 7052, 7053, 7054, 7055, 7056, 7057, 7058, 7059, 7060, 7061, 7062, 7063, 7064, 7065, 7066, 7067, 7068, 7069, 7070, 7071, 7073, 7074, 7075, 7076, 7077, 7078, 7079, 7080, 7081, 7082, 7083, 7084, 7085, 7086, 7087, 7088, 7089, 7090, 7091, 7092, 7093, 7094, 7095, 7096, 7097, 7098, 7099 };

static volatile int state = 0;
static volatile bool isMain = false;
static volatile bool listOfPeersIsStatic = false;
static volatile bool forceNextTick = false;
static volatile char criticalSituation = 0;
static volatile bool systemMustBeSaved = false, spectrumMustBeSaved = false, universeMustBeSaved = false, computerMustBeSaved = false;

static unsigned char operatorPublicKey[32];
static unsigned char computorSubseeds[sizeof(computorSeeds) / sizeof(computorSeeds[0])][32], computorPrivateKeys[sizeof(computorSeeds) / sizeof(computorSeeds[0])][32], computorPublicKeys[sizeof(computorSeeds) / sizeof(computorSeeds[0])][32];
static __m256i arbitratorPublicKey;

static struct
{
    RequestResponseHeader header;
    BroadcastComputors broadcastComputors;
} broadcastedComputors;

static CHAR16 message[16384], timestampedMessage[16384];

static EFI_FILE_PROTOCOL* root = NULL;

static struct System
{
    short version;
    unsigned short epoch;
    unsigned int tick;
    unsigned int initialTick;
    unsigned int latestCreatedTick, latestLedTick;

    unsigned short initialMillisecond;
    unsigned char initialSecond;
    unsigned char initialMinute;
    unsigned char initialHour;
    unsigned char initialDay;
    unsigned char initialMonth;
    unsigned char initialYear;

    unsigned long long latestOperatorNonce;

    ComputorProposal proposals[NUMBER_OF_COMPUTORS];
    ComputorBallot ballots[NUMBER_OF_COMPUTORS];

    unsigned int numberOfSolutions;
    struct Solution
    {
        unsigned char computorPublicKey[32];
        unsigned char nonce[32];
    } solutions[MAX_NUMBER_OF_SOLUTIONS];

    __m256i futureComputors[NUMBER_OF_COMPUTORS];
} system;
static int solutionPublicationTicks[MAX_NUMBER_OF_SOLUTIONS];
static unsigned long long faultyComputorFlags[(NUMBER_OF_COMPUTORS + 63) / 64];
static unsigned int tickPhase = 0, tickNumberOfComputors = 0, tickTotalNumberOfComputors = 0, futureTickTotalNumberOfComputors = 0;
static unsigned int nextTickTransactionsSemaphore = 0, numberOfNextTickTransactions = 0, numberOfKnownNextTickTransactions = 0;
static unsigned short numberOfOwnComputorIndices = 0;
static unsigned short ownComputorIndices[sizeof(computorSeeds) / sizeof(computorSeeds[0])];
static unsigned short ownComputorIndicesMapping[sizeof(computorSeeds) / sizeof(computorSeeds[0])];

static Tick* ticks = NULL;
static TickData* tickData = NULL;
static volatile char tickDataLock = 0;
static Tick etalonTick;
static TickData nextTickData;
static volatile char tickTransactionsLock = 0;
static unsigned char* tickTransactions = NULL;
static unsigned long long tickTransactionOffsets[MAX_NUMBER_OF_TICKS_PER_EPOCH][NUMBER_OF_TRANSACTIONS_PER_TICK];
static unsigned long long nextTickTransactionOffset = FIRST_TICK_TRANSACTION_OFFSET;

static __m256i uniqueNextTickTransactionDigests[NUMBER_OF_COMPUTORS];
static unsigned int uniqueNextTickTransactionDigestCounters[NUMBER_OF_COMPUTORS];

static void* reorgBuffer = NULL;

static unsigned long long resourceTestingDigest = 0;

static volatile char spectrumLock = 0;
static ::Entity* spectrum = NULL;
static unsigned int numberOfEntities = 0;
static unsigned int numberOfTransactions = 0;
static volatile char entityPendingTransactionsLock = 0;
static unsigned char* entityPendingTransactions = NULL;
static unsigned char* entityPendingTransactionDigests = NULL;
static unsigned int entityPendingTransactionIndices[SPECTRUM_CAPACITY];
static unsigned long long spectrumChangeFlags[SPECTRUM_CAPACITY / (sizeof(unsigned long long) * 8)];
static __m256i* spectrumDigests = NULL;

static volatile char universeLock = 0;
static Asset* assets = NULL;
static __m256i* assetDigests = NULL;
static unsigned long long* assetChangeFlags = NULL;
static char CONTRACT_ASSET_UNIT_OF_MEASUREMENT[7] = { 0, 0, 0, 0, 0, 0, 0 };

static volatile char computerLock = 0;
static unsigned long long mainLoopNumerator = 0, mainLoopDenominator = 0;
static unsigned char contractProcessorState = 0;
static unsigned int contractProcessorPhase;
static EFI_EVENT contractProcessorEvent;
static __m256i currentContract;
static unsigned char* contractStates[sizeof(contractDescriptions) / sizeof(contractDescriptions[0])];
static __m256i contractStateDigests[MAX_NUMBER_OF_CONTRACTS * 2 - 1];
static unsigned long long* contractStateChangeFlags = NULL;
static unsigned long long* functionFlags = NULL;
static unsigned char executedContractInput[65536];

static volatile char tickLocks[NUMBER_OF_COMPUTORS];
static bool targetNextTickDataDigestIsKnown = false;
static unsigned int testFlags = 0;
static __m256i targetNextTickDataDigest;
static unsigned long long tickTicks[11];

static unsigned char releasedPublicKeys[NUMBER_OF_COMPUTORS][32];
static long long releasedAmounts[NUMBER_OF_COMPUTORS];
static unsigned int numberOfReleasedEntities;

static unsigned long long* dejavu0 = NULL;
static unsigned long long* dejavu1 = NULL;
static unsigned int dejavuSwapCounter = DEJAVU_SWAP_LIMIT;

static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* simpleFileSystemProtocol;

static EFI_MP_SERVICES_PROTOCOL* mpServicesProtocol;
static unsigned long long frequency;
static unsigned int numberOfProcessors = 0;
static Processor processors[MAX_NUMBER_OF_PROCESSORS];
static volatile long long numberOfProcessedRequests = 0, prevNumberOfProcessedRequests = 0;
static volatile long long numberOfDiscardedRequests = 0, prevNumberOfDiscardedRequests = 0;
static volatile long long numberOfDuplicateRequests = 0, prevNumberOfDuplicateRequests = 0;
static volatile long long numberOfDisseminatedRequests = 0, prevNumberOfDisseminatedRequests = 0;
static unsigned char* requestQueueBuffer = NULL;
static unsigned char* responseQueueBuffer = NULL;
static struct Request
{
    Peer* peer;
    unsigned int offset;
} requestQueueElements[REQUEST_QUEUE_LENGTH];
static struct Response
{
    Peer* peer;
    unsigned int offset;
} responseQueueElements[RESPONSE_QUEUE_LENGTH];
static volatile unsigned int requestQueueBufferHead = 0, requestQueueBufferTail = 0;
static volatile unsigned int responseQueueBufferHead = 0, responseQueueBufferTail = 0;
static volatile unsigned short requestQueueElementHead = 0, requestQueueElementTail = 0;
static volatile unsigned short responseQueueElementHead = 0, responseQueueElementTail = 0;
static volatile char requestQueueTailLock = 0;
static volatile char responseQueueHeadLock = 0;
static volatile unsigned long long queueProcessingNumerator = 0, queueProcessingDenominator = 0;
static volatile unsigned long long tickerLoopNumerator = 0, tickerLoopDenominator = 0;

static EFI_GUID tcp4ServiceBindingProtocolGuid = EFI_TCP4_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_SERVICE_BINDING_PROTOCOL* tcp4ServiceBindingProtocol;
static EFI_GUID tcp4ProtocolGuid = EFI_TCP4_PROTOCOL_GUID;
static EFI_TCP4_PROTOCOL* peerTcp4Protocol;
static Peer peers[NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS];
static volatile long long numberOfReceivedBytes = 0, prevNumberOfReceivedBytes = 0;
static volatile long long numberOfTransmittedBytes = 0, prevNumberOfTransmittedBytes = 0;

static volatile char publicPeersLock = 0;
static unsigned int numberOfPublicPeers = 0;
static PublicPeer publicPeers[MAX_NUMBER_OF_PUBLIC_PEERS];

static int miningData[DATA_LENGTH];
static struct
{
    int input[DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH];
    int output[INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH];
} neurons[MAX_NUMBER_OF_PROCESSORS];
static struct
{
    char input[(NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH)];
    char output[(NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH)];
    unsigned short lengths[MAX_INPUT_DURATION * (NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + MAX_OUTPUT_DURATION * (NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH)];
} synapses[MAX_NUMBER_OF_PROCESSORS];

static volatile char solutionsLock = 0;
static unsigned long long* minerSolutionFlags = NULL;
static volatile unsigned char minerPublicKeys[MAX_NUMBER_OF_MINERS][32];
static volatile unsigned int minerScores[MAX_NUMBER_OF_MINERS];
static volatile unsigned int numberOfMiners = NUMBER_OF_COMPUTORS;
static __m256i competitorPublicKeys[(NUMBER_OF_COMPUTORS - QUORUM) * 2];
static unsigned int competitorScores[(NUMBER_OF_COMPUTORS - QUORUM) * 2];
static bool competitorComputorStatuses[(NUMBER_OF_COMPUTORS - QUORUM) * 2];
static unsigned int minimumComputorScore = 0, minimumCandidateScore = 0;

BroadcastFutureTickData broadcastedFutureTickData;

static struct
{
    RequestResponseHeader header;
} requestedComputors;

static struct
{
    RequestResponseHeader header;
    RequestQuorumTick requestQuorumTick;
} requestedQuorumTick;

static struct
{
    RequestResponseHeader header;
    RequestTickData requestTickData;
} requestedTickData;

static struct
{
    RequestResponseHeader header;
    RequestedTickTransactions requestedTickTransactions;
} requestedTickTransactions;

static bool disableLogging = false;

static void log(const CHAR16* message)
{
    if (disableLogging)
    {
        return;
    }

    timestampedMessage[0] = (time.Year % 100) / 10 + L'0';
    timestampedMessage[1] = time.Year % 10 + L'0';
    timestampedMessage[2] = time.Month / 10 + L'0';
    timestampedMessage[3] = time.Month % 10 + L'0';
    timestampedMessage[4] = time.Day / 10 + L'0';
    timestampedMessage[5] = time.Day % 10 + L'0';
    timestampedMessage[6] = time.Hour / 10 + L'0';
    timestampedMessage[7] = time.Hour % 10 + L'0';
    timestampedMessage[8] = time.Minute / 10 + L'0';
    timestampedMessage[9] = time.Minute % 10 + L'0';
    timestampedMessage[10] = time.Second / 10 + L'0';
    timestampedMessage[11] = time.Second % 10 + L'0';
    timestampedMessage[12] = ' ';
    timestampedMessage[13] = 0;

    switch (tickPhase)
    {
    case 0: appendText(timestampedMessage, L"A"); break;
    case 1: appendText(timestampedMessage, L"B"); break;
    case 2: appendText(timestampedMessage, L"C"); break;
    case 3: appendText(timestampedMessage, L"D"); break;
    case 4: appendText(timestampedMessage, L"E"); break;
    default: appendText(timestampedMessage, L"?");
    }
    if (testFlags)
    {
        appendNumber(timestampedMessage, testFlags, TRUE);
    }
    appendText(timestampedMessage, targetNextTickDataDigestIsKnown ? L"+ " : L"- ");
    appendNumber(timestampedMessage, tickNumberOfComputors / 100, FALSE);
    appendNumber(timestampedMessage, (tickNumberOfComputors % 100) / 10, FALSE);
    appendNumber(timestampedMessage, tickNumberOfComputors % 10, FALSE);
    appendText(timestampedMessage, L":");
    appendNumber(timestampedMessage, (tickTotalNumberOfComputors - tickNumberOfComputors) / 100, FALSE);
    appendNumber(timestampedMessage, ((tickTotalNumberOfComputors - tickNumberOfComputors) % 100) / 10, FALSE);
    appendNumber(timestampedMessage, (tickTotalNumberOfComputors - tickNumberOfComputors) % 10, FALSE);
    appendText(timestampedMessage, L"(");
    appendNumber(timestampedMessage, futureTickTotalNumberOfComputors / 100, FALSE);
    appendNumber(timestampedMessage, (futureTickTotalNumberOfComputors % 100) / 10, FALSE);
    appendNumber(timestampedMessage, futureTickTotalNumberOfComputors % 10, FALSE);
    appendText(timestampedMessage, L").");
    appendNumber(timestampedMessage, system.tick, FALSE);
    appendText(timestampedMessage, L".");
    appendNumber(timestampedMessage, system.epoch, FALSE);
    appendText(timestampedMessage, L" ");

    appendText(timestampedMessage, message);
    appendText(timestampedMessage, L"\r\n");

    st->ConOut->OutputString(st->ConOut, timestampedMessage);
}

static void logStatus(const CHAR16* message, const EFI_STATUS status, const unsigned int lineNumber)
{
    setText(::message, message);
    appendText(::message, L" (");
    appendErrorStatus(::message, status);
    appendText(::message, L") near line ");
    appendNumber(::message, lineNumber, FALSE);
    appendText(::message, L"!");
    log(::message);
}

static int spectrumIndex(unsigned char* publicKey)
{
    if (EQUAL(*((__m256i*)publicKey), _mm256_setzero_si256()))
    {
        return -1;
    }

    unsigned int index = (*((unsigned int*)publicKey)) & (SPECTRUM_CAPACITY - 1);

    ACQUIRE(spectrumLock);

iteration:
    if (EQUAL(*((__m256i*)spectrum[index].publicKey), *((__m256i*)publicKey)))
    {
        RELEASE(spectrumLock);

        return index;
    }
    else
    {
        if (EQUAL(*((__m256i*)spectrum[index].publicKey), _mm256_setzero_si256()))
        {
            RELEASE(spectrumLock);

            return -1;
        }
        else
        {
            index = (index + 1) & (SPECTRUM_CAPACITY - 1);

            goto iteration;
        }
    }
}

static long long energy(const int index)
{
    return spectrum[index].incomingAmount - spectrum[index].outgoingAmount;
}

static void increaseEnergy(unsigned char* publicKey, long long amount)
{
    if (!EQUAL(*((__m256i*)publicKey), _mm256_setzero_si256()) && amount >= 0)
    {
        // TODO: numberOfEntities!

        unsigned int index = (*((unsigned int*)publicKey)) & (SPECTRUM_CAPACITY - 1);

        ACQUIRE(spectrumLock);

    iteration:
        if (EQUAL(*((__m256i*)spectrum[index].publicKey), *((__m256i*)publicKey)))
        {
            spectrum[index].incomingAmount += amount;
            spectrum[index].numberOfIncomingTransfers++;
            spectrum[index].latestIncomingTransferTick = system.tick;
        }
        else
        {
            if (EQUAL(*((__m256i*)spectrum[index].publicKey), _mm256_setzero_si256()))
            {
                *((__m256i*)spectrum[index].publicKey) = *((__m256i*)publicKey);
                spectrum[index].incomingAmount = amount;
                spectrum[index].numberOfIncomingTransfers = 1;
                spectrum[index].latestIncomingTransferTick = system.tick;
            }
            else
            {
                index = (index + 1) & (SPECTRUM_CAPACITY - 1);

                goto iteration;
            }
        }

        RELEASE(spectrumLock);
    }
}

static bool decreaseEnergy(const int index, long long amount)
{
    if (amount >= 0)
    {
        ACQUIRE(spectrumLock);

        if (energy(index) >= amount)
        {
            spectrum[index].outgoingAmount += amount;
            spectrum[index].numberOfOutgoingTransfers++;
            spectrum[index].latestOutgoingTransferTick = system.tick;

            RELEASE(spectrumLock);

            return true;
        }

        RELEASE(spectrumLock);
    }

    return false;
}

static void issueAsset(unsigned char* issuerPublicKey, char name[7], char numberOfDecimalPlaces, char unitOfMeasurement[7], long long numberOfUnits,
    int* issuanceIndex, int* ownershipIndex, int* possessionIndex)
{
    *issuanceIndex = (*((unsigned int*)issuerPublicKey)) & (ASSETS_CAPACITY - 1);

    ACQUIRE(universeLock);

iteration:
    if (assets[*issuanceIndex].varStruct.issuance.type == EMPTY)
    {
        *((__m256i*)assets[*issuanceIndex].varStruct.issuance.publicKey) = *((__m256i*)issuerPublicKey);
        assets[*issuanceIndex].varStruct.issuance.type = ISSUANCE;
        bs->CopyMem(assets[*issuanceIndex].varStruct.issuance.name, name, sizeof(assets[*issuanceIndex].varStruct.issuance.name));
        assets[*issuanceIndex].varStruct.issuance.numberOfDecimalPlaces = numberOfDecimalPlaces;
        bs->CopyMem(assets[*issuanceIndex].varStruct.issuance.unitOfMeasurement, unitOfMeasurement, sizeof(assets[*issuanceIndex].varStruct.issuance.unitOfMeasurement));

        *ownershipIndex = (*issuanceIndex + 1) & (ASSETS_CAPACITY - 1);
    iteration2:
        if (assets[*ownershipIndex].varStruct.ownership.type == EMPTY)
        {
            *((__m256i*)assets[*ownershipIndex].varStruct.ownership.publicKey) = *((__m256i*)issuerPublicKey);
            assets[*ownershipIndex].varStruct.ownership.type = OWNERSHIP;
            assets[*ownershipIndex].varStruct.ownership.managingContractIndex = QX_CONTRACT_INDEX;
            assets[*ownershipIndex].varStruct.ownership.issuanceIndex = *issuanceIndex;
            assets[*ownershipIndex].varStruct.ownership.numberOfUnits = numberOfUnits;

            *possessionIndex = (*ownershipIndex + 1) & (ASSETS_CAPACITY - 1);
        iteration3:
            if (assets[*possessionIndex].varStruct.possession.type == EMPTY)
            {
                *((__m256i*)assets[*possessionIndex].varStruct.possession.publicKey) = *((__m256i*)issuerPublicKey);
                assets[*possessionIndex].varStruct.possession.type = POSSESSION;
                assets[*possessionIndex].varStruct.possession.managingContractIndex = QX_CONTRACT_INDEX;
                assets[*possessionIndex].varStruct.possession.ownershipIndex = *ownershipIndex;
                assets[*possessionIndex].varStruct.possession.numberOfUnits = numberOfUnits;

                assetChangeFlags[*issuanceIndex >> 6] |= (1ULL << (*issuanceIndex & 63));
                assetChangeFlags[*ownershipIndex >> 6] |= (1ULL << (*ownershipIndex & 63));
                assetChangeFlags[*possessionIndex >> 6] |= (1ULL << (*possessionIndex & 63));

                RELEASE(universeLock);
            }
            else
            {
                *possessionIndex = (*possessionIndex + 1) & (ASSETS_CAPACITY - 1);

                goto iteration3;
            }
        }
        else
        {
            *ownershipIndex = (*ownershipIndex + 1) & (ASSETS_CAPACITY - 1);

            goto iteration2;
        }
    }
    else
    {
        *issuanceIndex = (*issuanceIndex + 1) & (ASSETS_CAPACITY - 1);

        goto iteration;
    }
}

static bool transferAssetOwnershipAndPossession(int sourceOwnershipIndex, int sourcePossessionIndex, unsigned char* destinationPublicKey, long long numberOfUnits,
    int* destinationOwnershipIndex, int* destinationPossessionIndex)
{
    if (numberOfUnits <= 0)
    {
        return false;
    }

    ACQUIRE(universeLock);

    if (assets[sourceOwnershipIndex].varStruct.ownership.type != OWNERSHIP || assets[sourceOwnershipIndex].varStruct.ownership.numberOfUnits < numberOfUnits
        || assets[sourcePossessionIndex].varStruct.possession.type != POSSESSION || assets[sourcePossessionIndex].varStruct.possession.numberOfUnits < numberOfUnits
        || assets[sourcePossessionIndex].varStruct.possession.ownershipIndex != sourceOwnershipIndex)
    {
        RELEASE(universeLock);

        return false;
    }

    *destinationOwnershipIndex = (*((unsigned int*)destinationPublicKey)) & (ASSETS_CAPACITY - 1);
iteration:
    if (assets[*destinationOwnershipIndex].varStruct.ownership.type == EMPTY
        || (assets[*destinationOwnershipIndex].varStruct.ownership.type == OWNERSHIP
            && assets[*destinationOwnershipIndex].varStruct.ownership.managingContractIndex == assets[sourceOwnershipIndex].varStruct.ownership.managingContractIndex
            && assets[*destinationOwnershipIndex].varStruct.ownership.issuanceIndex == assets[sourceOwnershipIndex].varStruct.ownership.issuanceIndex
            && EQUAL(*((__m256i*)assets[*destinationOwnershipIndex].varStruct.ownership.publicKey), *((__m256i*)destinationPublicKey))))
    {
        assets[sourceOwnershipIndex].varStruct.ownership.numberOfUnits -= numberOfUnits;

        if (assets[*destinationOwnershipIndex].varStruct.ownership.type == EMPTY)
        {
            *((__m256i*)assets[*destinationOwnershipIndex].varStruct.ownership.publicKey) = *((__m256i*)destinationPublicKey);
            assets[*destinationOwnershipIndex].varStruct.ownership.type = OWNERSHIP;
            assets[*destinationOwnershipIndex].varStruct.ownership.managingContractIndex = assets[sourceOwnershipIndex].varStruct.ownership.managingContractIndex;
            assets[*destinationOwnershipIndex].varStruct.ownership.issuanceIndex = assets[sourceOwnershipIndex].varStruct.ownership.issuanceIndex;
        }
        assets[*destinationOwnershipIndex].varStruct.ownership.numberOfUnits += numberOfUnits;

        *destinationPossessionIndex = (*((unsigned int*)destinationPublicKey)) & (ASSETS_CAPACITY - 1);
    iteration2:
        if (assets[*destinationPossessionIndex].varStruct.possession.type == EMPTY
            || (assets[*destinationPossessionIndex].varStruct.possession.type == POSSESSION
                && assets[*destinationPossessionIndex].varStruct.possession.managingContractIndex == assets[sourcePossessionIndex].varStruct.possession.managingContractIndex
                && assets[*destinationPossessionIndex].varStruct.possession.ownershipIndex == *destinationOwnershipIndex
                && EQUAL(*((__m256i*)assets[*destinationPossessionIndex].varStruct.possession.publicKey), *((__m256i*)destinationPublicKey))))
        {
            assets[sourcePossessionIndex].varStruct.possession.numberOfUnits -= numberOfUnits;

            if (assets[*destinationPossessionIndex].varStruct.possession.type == EMPTY)
            {
                *((__m256i*)assets[*destinationPossessionIndex].varStruct.possession.publicKey) = *((__m256i*)destinationPublicKey);
                assets[*destinationPossessionIndex].varStruct.possession.type = POSSESSION;
                assets[*destinationPossessionIndex].varStruct.possession.managingContractIndex = assets[sourcePossessionIndex].varStruct.possession.managingContractIndex;
                assets[*destinationPossessionIndex].varStruct.possession.ownershipIndex = *destinationOwnershipIndex;
            }
            assets[*destinationPossessionIndex].varStruct.possession.numberOfUnits += numberOfUnits;

            assetChangeFlags[sourceOwnershipIndex >> 6] |= (1ULL << (sourceOwnershipIndex & 63));
            assetChangeFlags[sourcePossessionIndex >> 6] |= (1ULL << (sourcePossessionIndex & 63));
            assetChangeFlags[*destinationOwnershipIndex >> 6] |= (1ULL << (*destinationOwnershipIndex & 63));
            assetChangeFlags[*destinationPossessionIndex >> 6] |= (1ULL << (*destinationPossessionIndex & 63));

            RELEASE(universeLock);

            return true;
        }
        else
        {
            *destinationPossessionIndex = (*destinationPossessionIndex + 1) & (ASSETS_CAPACITY - 1);

            goto iteration2;
        }
    }
    else
    {
        *destinationOwnershipIndex = (*destinationOwnershipIndex + 1) & (ASSETS_CAPACITY - 1);

        goto iteration;
    }
}

inline static unsigned int random(const unsigned int range)
{
    unsigned int value;
    _rdrand32_step(&value);

    return value % range;
}

static void forget(int address)
{
    if (listOfPeersIsStatic)
    {
        return;
    }

    ACQUIRE(publicPeersLock);

    for (unsigned int i = 0; numberOfPublicPeers > NUMBER_OF_EXCHANGED_PEERS && i < numberOfPublicPeers; i++)
    {
        if (*((int*)publicPeers[i].address) == address)
        {
            if (!publicPeers[i].isVerified && i != --numberOfPublicPeers)
            {
                bs->CopyMem(&publicPeers[i], &publicPeers[numberOfPublicPeers], sizeof(PublicPeer));
            }

            break;
        }
    }

    RELEASE(publicPeersLock);
}

static void addPublicPeer(unsigned char address[4])
{
    if ((!address[0])
        || (address[0] == 127)
        || (address[0] == 10)
        || (address[0] == 172 && address[1] >= 16 && address[1] <= 31)
        || (address[0] == 192 && address[1] == 168)
        || (address[0] == 255))
    {
        return;
    }
    for (unsigned int i = 0; i < numberOfPublicPeers; i++)
    {
        if (*((int*)address) == *((int*)publicPeers[i].address))
        {
            return;
        }
    }

    ACQUIRE(publicPeersLock);

    if (numberOfPublicPeers < MAX_NUMBER_OF_PUBLIC_PEERS)
    {
        publicPeers[numberOfPublicPeers].isVerified = false;
        *((int*)publicPeers[numberOfPublicPeers++].address) = *((int*)address);
    }

    RELEASE(publicPeersLock);
}

static void enableAVX()
{
    __writecr4(__readcr4() | 0x40000);
    _xsetbv(_XCR_XFEATURE_ENABLED_MASK, _xgetbv(_XCR_XFEATURE_ENABLED_MASK) | (7
#if AVX512
        | 224
#endif
        ));
}

static void getUniverseDigest(__m256i* digest)
{
    unsigned int digestIndex;
    for (digestIndex = 0; digestIndex < ASSETS_CAPACITY; digestIndex++)
    {
        if (assetChangeFlags[digestIndex >> 6] & (1ULL << (digestIndex & 63)))
        {
            KangarooTwelve((unsigned char*)&assets[digestIndex], sizeof(Asset), (unsigned char*)&assetDigests[digestIndex], 32);
        }
    }
    unsigned int previousLevelBeginning = 0;
    unsigned int numberOfLeafs = ASSETS_CAPACITY;
    while (numberOfLeafs > 1)
    {
        for (unsigned int i = 0; i < numberOfLeafs; i += 2)
        {
            if (assetChangeFlags[i >> 6] & (3ULL << (i & 63)))
            {
                KangarooTwelve64To32((unsigned char*)&assetDigests[previousLevelBeginning + i], (unsigned char*)&assetDigests[digestIndex]);
                assetChangeFlags[i >> 6] &= ~(3ULL << (i & 63));
                assetChangeFlags[i >> 7] |= (1ULL << ((i >> 1) & 63));
            }
            digestIndex++;
        }
        previousLevelBeginning += numberOfLeafs;
        numberOfLeafs >>= 1;
    }
    assetChangeFlags[0] = 0;

    *digest = assetDigests[(ASSETS_CAPACITY * 2 - 1) - 1];
}

static void getComputerDigest(__m256i* digest)
{
    unsigned int digestIndex;
    for (digestIndex = 0; digestIndex < MAX_NUMBER_OF_CONTRACTS; digestIndex++)
    {
        if (contractStateChangeFlags[digestIndex >> 6] & (1ULL << (digestIndex & 63)))
        {
            const unsigned long long size = digestIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]) ? contractDescriptions[digestIndex].stateSize : 0;
            if (!size)
            {
                contractStateDigests[digestIndex] = _mm256_setzero_si256();
            }
            else
            {
                KangarooTwelve((unsigned char*)contractStates[digestIndex], size, (unsigned char*)&contractStateDigests[digestIndex], 32);
            }
        }
    }
    unsigned int previousLevelBeginning = 0;
    unsigned int numberOfLeafs = MAX_NUMBER_OF_CONTRACTS;
    while (numberOfLeafs > 1)
    {
        for (unsigned int i = 0; i < numberOfLeafs; i += 2)
        {
            if (contractStateChangeFlags[i >> 6] & (3ULL << (i & 63)))
            {
                KangarooTwelve64To32((unsigned char*)&contractStateDigests[previousLevelBeginning + i], (unsigned char*)&contractStateDigests[digestIndex]);
                contractStateChangeFlags[i >> 6] &= ~(3ULL << (i & 63));
                contractStateChangeFlags[i >> 7] |= (1ULL << ((i >> 1) & 63));
            }
            digestIndex++;
        }
        previousLevelBeginning += numberOfLeafs;
        numberOfLeafs >>= 1;
    }
    contractStateChangeFlags[0] = 0;

    *digest = contractStateDigests[(MAX_NUMBER_OF_CONTRACTS * 2 - 1) - 1];
}

static void closePeer(Peer* peer)
{
    if (((unsigned long long)peer->tcp4Protocol) > 1)
    {
        if (!peer->isClosing)
        {
            EFI_STATUS status;
            if (status = peer->tcp4Protocol->Configure(peer->tcp4Protocol, NULL))
            {
                logStatus(L"EFI_TCP4_PROTOCOL.Configure() fails", status, __LINE__);
            }

            peer->isClosing = TRUE;
        }

        if (!peer->isConnectingAccepting && !peer->isReceiving && !peer->isTransmitting)
        {
            bs->CloseProtocol(peer->connectAcceptToken.NewChildHandle, &tcp4ProtocolGuid, ih, NULL);
            EFI_STATUS status;
            if (status = tcp4ServiceBindingProtocol->DestroyChild(tcp4ServiceBindingProtocol, peer->connectAcceptToken.NewChildHandle))
            {
                logStatus(L"EFI_TCP4_SERVICE_BINDING_PROTOCOL.DestroyChild() fails", status, __LINE__);
            }

            peer->isConnectedAccepted = FALSE;
            peer->exchangedPublicPeers = FALSE;
            peer->isClosing = FALSE;
            peer->tcp4Protocol = NULL;
        }
    }
}

static void push(Peer* peer, RequestResponseHeader* requestResponseHeader)
{
    if (peer->tcp4Protocol && peer->isConnectedAccepted && !peer->isClosing)
    {
        if (peer->dataToTransmitSize + requestResponseHeader->size() > BUFFER_SIZE)
        {
            closePeer(peer);
        }
        else
        {
            bs->CopyMem(&peer->dataToTransmit[peer->dataToTransmitSize], requestResponseHeader, requestResponseHeader->size());
            peer->dataToTransmitSize += requestResponseHeader->size();

            _InterlockedIncrement64(&numberOfDisseminatedRequests);
        }
    }
}

static void pushToAny(RequestResponseHeader* requestResponseHeader)
{
    unsigned short suitablePeerIndices[NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS];
    unsigned short numberOfSuitablePeers = 0;
    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        if (peers[i].tcp4Protocol && peers[i].isConnectedAccepted && peers[i].exchangedPublicPeers && !peers[i].isClosing)
        {
            suitablePeerIndices[numberOfSuitablePeers++] = i;
        }
    }
    if (numberOfSuitablePeers)
    {
        push(&peers[suitablePeerIndices[random(numberOfSuitablePeers)]], requestResponseHeader);
    }
}

static void pushToSeveral(RequestResponseHeader* requestResponseHeader)
{
    unsigned short suitablePeerIndices[NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS];
    unsigned short numberOfSuitablePeers = 0;
    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        if (peers[i].tcp4Protocol && peers[i].isConnectedAccepted && peers[i].exchangedPublicPeers && !peers[i].isClosing)
        {
            suitablePeerIndices[numberOfSuitablePeers++] = i;
        }
    }
    unsigned short numberOfRemainingSuitablePeers = DISSEMINATION_MULTIPLIER;
    while (numberOfRemainingSuitablePeers-- && numberOfSuitablePeers)
    {
        const unsigned short index = random(numberOfSuitablePeers);
        push(&peers[suitablePeerIndices[index]], requestResponseHeader);
        suitablePeerIndices[index] = suitablePeerIndices[--numberOfSuitablePeers];
    }
}

static void enqueueResponse(Peer* peer, RequestResponseHeader* responseHeader)
{
    ACQUIRE(responseQueueHeadLock);

    if ((responseQueueBufferHead >= responseQueueBufferTail || responseQueueBufferHead + responseHeader->size() < responseQueueBufferTail)
        && (unsigned short)(responseQueueElementHead + 1) != responseQueueElementTail)
    {
        responseQueueElements[responseQueueElementHead].offset = responseQueueBufferHead;
        bs->CopyMem(&responseQueueBuffer[responseQueueBufferHead], responseHeader, responseHeader->size());
        responseQueueBufferHead += responseHeader->size();
        responseQueueElements[responseQueueElementHead].peer = peer;
        if (responseQueueBufferHead > RESPONSE_QUEUE_BUFFER_SIZE - BUFFER_SIZE)
        {
            responseQueueBufferHead = 0;
        }
        responseQueueElementHead++;
    }

    RELEASE(responseQueueHeadLock);
}

static void enqueueResponse(Peer* peer, unsigned int dataSize, unsigned char type, unsigned int dejavu, void* data)
{
    ACQUIRE(responseQueueHeadLock);

    if ((responseQueueBufferHead >= responseQueueBufferTail || responseQueueBufferHead + sizeof(RequestResponseHeader) + dataSize < responseQueueBufferTail)
        && (unsigned short)(responseQueueElementHead + 1) != responseQueueElementTail)
    {
        responseQueueElements[responseQueueElementHead].offset = responseQueueBufferHead;
        RequestResponseHeader* responseHeader = (RequestResponseHeader*)&responseQueueBuffer[responseQueueBufferHead];
        responseHeader->setSize(sizeof(RequestResponseHeader) + dataSize);
        responseHeader->setType(type);
        responseHeader->setDejavu(dejavu);
        if (data)
        {
            bs->CopyMem(&responseQueueBuffer[responseQueueBufferHead + sizeof(RequestResponseHeader)], data, dataSize);
        }
        responseQueueBufferHead += responseHeader->size();
        responseQueueElements[responseQueueElementHead].peer = peer;
        if (responseQueueBufferHead > RESPONSE_QUEUE_BUFFER_SIZE - BUFFER_SIZE)
        {
            responseQueueBufferHead = 0;
        }
        responseQueueElementHead++;
    }

    RELEASE(responseQueueHeadLock);
}

static unsigned int score(const unsigned long long processorNumber, unsigned char* publicKey, unsigned char* nonce)
{
    random(publicKey, nonce, (unsigned char*)&synapses[processorNumber], sizeof(synapses[0]));
    for (unsigned int inputNeuronIndex = 0; inputNeuronIndex < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; inputNeuronIndex++)
    {
        for (unsigned int anotherInputNeuronIndex = 0; anotherInputNeuronIndex < DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; anotherInputNeuronIndex++)
        {
            const unsigned int offset = inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + anotherInputNeuronIndex;
            synapses[processorNumber].input[offset] = (((unsigned char)synapses[processorNumber].input[offset]) % 3) - 1;
        }
    }
    for (unsigned int outputNeuronIndex = 0; outputNeuronIndex < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; outputNeuronIndex++)
    {
        for (unsigned int anotherOutputNeuronIndex = 0; anotherOutputNeuronIndex < INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; anotherOutputNeuronIndex++)
        {
            const unsigned int offset = outputNeuronIndex * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) + anotherOutputNeuronIndex;
            synapses[processorNumber].output[offset] = (((unsigned char)synapses[processorNumber].output[offset]) % 3) - 1;
        }
    }
    for (unsigned int inputNeuronIndex = 0; inputNeuronIndex < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; inputNeuronIndex++)
    {
        synapses[processorNumber].input[inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + (DATA_LENGTH + inputNeuronIndex)] = 0;
    }
    for (unsigned int outputNeuronIndex = 0; outputNeuronIndex < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; outputNeuronIndex++)
    {
        synapses[processorNumber].output[outputNeuronIndex * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) + (INFO_LENGTH + outputNeuronIndex)] = 0;
    }

    unsigned int lengthIndex = 0;

    bs->CopyMem(&neurons[processorNumber].input[0], &miningData, sizeof(miningData));
    bs->SetMem(&neurons[processorNumber].input[sizeof(miningData) / sizeof(neurons[0].input[0])], sizeof(neurons[0]) - sizeof(miningData), 0);

    for (unsigned int tick = 0; tick < MAX_INPUT_DURATION; tick++)
    {
        unsigned short neuronIndices[NUMBER_OF_INPUT_NEURONS + INFO_LENGTH];
        unsigned short numberOfRemainingNeurons = 0;
        for (numberOfRemainingNeurons = 0; numberOfRemainingNeurons < NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; numberOfRemainingNeurons++)
        {
            neuronIndices[numberOfRemainingNeurons] = numberOfRemainingNeurons;
        }
        while (numberOfRemainingNeurons)
        {
            const unsigned short neuronIndexIndex = synapses[processorNumber].lengths[lengthIndex++] % numberOfRemainingNeurons;
            const unsigned short inputNeuronIndex = neuronIndices[neuronIndexIndex];
            neuronIndices[neuronIndexIndex] = neuronIndices[--numberOfRemainingNeurons];
            for (unsigned short anotherInputNeuronIndex = 0; anotherInputNeuronIndex < DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH; anotherInputNeuronIndex++)
            {
                int value = neurons[processorNumber].input[anotherInputNeuronIndex] >= 0 ? 1 : -1;
                value *= synapses[processorNumber].input[inputNeuronIndex * (DATA_LENGTH + NUMBER_OF_INPUT_NEURONS + INFO_LENGTH) + anotherInputNeuronIndex];
                neurons[processorNumber].input[DATA_LENGTH + inputNeuronIndex] += value;
            }
        }
    }

    bs->CopyMem(&neurons[processorNumber].output[0], &neurons[processorNumber].input[DATA_LENGTH + NUMBER_OF_INPUT_NEURONS], INFO_LENGTH * sizeof(neurons[0].input[0]));

    for (unsigned int tick = 0; tick < MAX_OUTPUT_DURATION; tick++)
    {
        unsigned short neuronIndices[NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH];
        unsigned short numberOfRemainingNeurons = 0;
        for (numberOfRemainingNeurons = 0; numberOfRemainingNeurons < NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; numberOfRemainingNeurons++)
        {
            neuronIndices[numberOfRemainingNeurons] = numberOfRemainingNeurons;
        }
        while (numberOfRemainingNeurons)
        {
            const unsigned short neuronIndexIndex = synapses[processorNumber].lengths[lengthIndex++] % numberOfRemainingNeurons;
            const unsigned short outputNeuronIndex = neuronIndices[neuronIndexIndex];
            neuronIndices[neuronIndexIndex] = neuronIndices[--numberOfRemainingNeurons];
            for (unsigned int anotherOutputNeuronIndex = 0; anotherOutputNeuronIndex < INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH; anotherOutputNeuronIndex++)
            {
                int value = neurons[processorNumber].output[anotherOutputNeuronIndex] >= 0 ? 1 : -1;
                value *= synapses[processorNumber].output[outputNeuronIndex * (INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + DATA_LENGTH) + anotherOutputNeuronIndex];
                neurons[processorNumber].output[INFO_LENGTH + outputNeuronIndex] += value;
            }
        }
    }

    unsigned int score = 0;

    for (unsigned int i = 0; i < DATA_LENGTH; i++)
    {
        if ((miningData[i] >= 0) == (neurons[processorNumber].output[INFO_LENGTH + NUMBER_OF_OUTPUT_NEURONS + i] >= 0))
        {
            score++;
        }
    }

    return score;
}

static void exchangePublicPeers(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    if (!peer->exchangedPublicPeers)
    {
        peer->exchangedPublicPeers = TRUE; // A race condition is possible

        if (*((int*)peer->address))
        {
            for (unsigned int j = 0; j < numberOfPublicPeers; j++)
            {
                if (*((int*)peer->address) == *((int*)publicPeers[j].address))
                {
                    publicPeers[j].isVerified = true;

                    break;
                }
            }
        }
    }

    ExchangePublicPeers* request = (ExchangePublicPeers*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    for (unsigned int j = 0; j < NUMBER_OF_EXCHANGED_PEERS && numberOfPublicPeers < MAX_NUMBER_OF_PUBLIC_PEERS; j++)
    {
        if (!listOfPeersIsStatic)
        {
            addPublicPeer(request->peers[j]);
        }
    }
}

static void broadcastMessage(const unsigned long long processorNumber, Processor* processor, RequestResponseHeader* header)
{
    Message* request = (Message*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (header->size() <= sizeof(RequestResponseHeader) + sizeof(Message) + MAX_MESSAGE_PAYLOAD_SIZE + SIGNATURE_SIZE
        && header->size() >= sizeof(RequestResponseHeader) + sizeof(Message) + SIGNATURE_SIZE)
    {
        const unsigned int messageSize = header->size() - sizeof(RequestResponseHeader);

        bool ok;
        if (EQUAL(*((__m256i*)request->sourcePublicKey), _mm256_setzero_si256()))
        {
            ok = true;
        }
        else
        {
            unsigned char digest[32];
            KangarooTwelve((unsigned char*)request, messageSize - SIGNATURE_SIZE, digest, sizeof(digest));
            ok = verify(request->sourcePublicKey, digest, (((const unsigned char*)request) + (messageSize - SIGNATURE_SIZE)));
        }
        if (ok)
        {
            if (header->isDejavuZero())
            {
                const int spectrumIndex = ::spectrumIndex(request->sourcePublicKey);
                if (spectrumIndex >= 0 && energy(spectrumIndex) >= MESSAGE_DISSEMINATION_THRESHOLD)
                {
                    enqueueResponse(NULL, header);
                }
            }

            for (unsigned int i = 0; i < sizeof(computorSeeds) / sizeof(computorSeeds[0]); i++)
            {
                if (EQUAL(*((__m256i*)request->destinationPublicKey), *((__m256i*)computorPublicKeys[i])))
                {
                    const unsigned int messagePayloadSize = messageSize - sizeof(Message) - SIGNATURE_SIZE;
                    if (messagePayloadSize)
                    {
                        unsigned char sharedKeyAndGammingNonce[64];

                        if (EQUAL(*((__m256i*)request->sourcePublicKey), _mm256_setzero_si256()))
                        {
                            bs->SetMem(sharedKeyAndGammingNonce, 32, 0);
                        }
                        else
                        {
                            if (!getSharedKey(computorPrivateKeys[i], request->sourcePublicKey, sharedKeyAndGammingNonce))
                            {
                                ok = false;
                            }
                        }

                        if (ok)
                        {
                            bs->CopyMem(&sharedKeyAndGammingNonce[32], request->gammingNonce, 32);
                            unsigned char gammingKey[32];
                            KangarooTwelve64To32(sharedKeyAndGammingNonce, gammingKey);
                            bs->SetMem(sharedKeyAndGammingNonce, 32, 0); // Zero the shared key in case stack content could be leaked later
                            unsigned char gamma[MAX_MESSAGE_PAYLOAD_SIZE];
                            KangarooTwelve(gammingKey, sizeof(gammingKey), gamma, messagePayloadSize);
                            for (unsigned int j = 0; j < messagePayloadSize; j++)
                            {
                                ((unsigned char*)request)[sizeof(Message) + j] ^= gamma[j];
                            }

                            switch (gammingKey[0])
                            {
                            case MESSAGE_TYPE_SOLUTION:
                            {
                                if (messagePayloadSize >= 32)
                                {
                                    unsigned int k;
                                    for (k = 0; k < system.numberOfSolutions; k++)
                                    {
                                        if (EQUAL(*((__m256i*) & ((unsigned char*)request)[sizeof(Message)]), *((__m256i*)system.solutions[k].nonce))
                                            && EQUAL(*((__m256i*)request->destinationPublicKey), *((__m256i*)system.solutions[k].computorPublicKey)))
                                        {
                                            break;
                                        }
                                    }
                                    if (k == system.numberOfSolutions)
                                    {
                                        if (system.numberOfSolutions < MAX_NUMBER_OF_SOLUTIONS
                                            && score(processorNumber, request->destinationPublicKey, &((unsigned char*)request)[sizeof(Message)]) >= SOLUTION_THRESHOLD)
                                        {
                                            ACQUIRE(solutionsLock);

                                            for (k = 0; k < system.numberOfSolutions; k++)
                                            {
                                                if (EQUAL(*((__m256i*) & ((unsigned char*)request)[sizeof(Message)]), *((__m256i*)system.solutions[k].nonce))
                                                    && EQUAL(*((__m256i*)request->destinationPublicKey), *((__m256i*)system.solutions[k].computorPublicKey)))
                                                {
                                                    break;
                                                }
                                            }
                                            if (k == system.numberOfSolutions)
                                            {
                                                *((__m256i*)system.solutions[system.numberOfSolutions].computorPublicKey) = *((__m256i*)request->destinationPublicKey);
                                                *((__m256i*)system.solutions[system.numberOfSolutions++].nonce) = *((__m256i*) & ((unsigned char*)request)[sizeof(Message)]);
                                            }

                                            RELEASE(solutionsLock);
                                        }
                                    }
                                }
                            }
                            break;
                            }
                        }
                    }

                    break;
                }
            }
        }
    }
}

static void broadcastComputors(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    BroadcastComputors* request = (BroadcastComputors*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->computors.epoch > broadcastedComputors.broadcastComputors.computors.epoch)
    {
        unsigned char digest[32];
        KangarooTwelve((unsigned char*)request, sizeof(BroadcastComputors) - SIGNATURE_SIZE, digest, sizeof(digest));
        if (verify((unsigned char*)&arbitratorPublicKey, digest, request->computors.signature))
        {
            if (header->isDejavuZero())
            {
                enqueueResponse(NULL, header);
            }

            bs->CopyMem(&broadcastedComputors.broadcastComputors.computors, &request->computors, sizeof(Computors));

            if (request->computors.epoch == system.epoch)
            {
                numberOfOwnComputorIndices = 0;
                for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                {
                    *((__m256i*)minerPublicKeys[i]) = *((__m256i*)request->computors.publicKeys[i]);

                    for (unsigned int j = 0; j < sizeof(computorSeeds) / sizeof(computorSeeds[0]); j++)
                    {
                        if (EQUAL(*((__m256i*)request->computors.publicKeys[i]), *((__m256i*)computorPublicKeys[j])))
                        {
                            ownComputorIndices[numberOfOwnComputorIndices] = i;
                            ownComputorIndicesMapping[numberOfOwnComputorIndices++] = j;

                            break;
                        }
                    }
                }
            }
        }
    }
}

static void broadcastTick(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    BroadcastTick* request = (BroadcastTick*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->tick.computorIndex < NUMBER_OF_COMPUTORS
        && request->tick.epoch == system.epoch
        && request->tick.tick >= system.tick && request->tick.tick < system.initialTick + MAX_NUMBER_OF_TICKS_PER_EPOCH
        && request->tick.month >= 1 && request->tick.month <= 12
        && request->tick.day >= 1 && request->tick.day <= ((request->tick.month == 1 || request->tick.month == 3 || request->tick.month == 5 || request->tick.month == 7 || request->tick.month == 8 || request->tick.month == 10 || request->tick.month == 12) ? 31 : ((request->tick.month == 4 || request->tick.month == 6 || request->tick.month == 9 || request->tick.month == 11) ? 30 : ((request->tick.year & 3) ? 28 : 29)))
        && request->tick.hour <= 23
        && request->tick.minute <= 59
        && request->tick.second <= 59
        && request->tick.millisecond <= 999)
    {
        unsigned char digest[32];
        request->tick.computorIndex ^= BROADCAST_TICK;
        KangarooTwelve((unsigned char*)&request->tick, sizeof(Tick) - SIGNATURE_SIZE, digest, sizeof(digest));
        request->tick.computorIndex ^= BROADCAST_TICK;
        if (verify(broadcastedComputors.broadcastComputors.computors.publicKeys[request->tick.computorIndex], digest, request->tick.signature))
        {
            if (header->isDejavuZero())
            {
                enqueueResponse(NULL, header);
            }

            ACQUIRE(tickLocks[request->tick.computorIndex]);

            const unsigned int offset = ((request->tick.tick - system.initialTick) * NUMBER_OF_COMPUTORS) + request->tick.computorIndex;
            if (ticks[offset].epoch == system.epoch)
            {
                if (*((unsigned long long*) & request->tick.millisecond) != *((unsigned long long*) & ticks[offset].millisecond)
                    || !EQUAL(*((__m256i*)request->tick.prevSpectrumDigest), *((__m256i*)ticks[offset].prevSpectrumDigest))
                    || !EQUAL(*((__m256i*)request->tick.prevUniverseDigest), *((__m256i*)ticks[offset].prevUniverseDigest))
                    || !EQUAL(*((__m256i*)request->tick.prevComputerDigest), *((__m256i*)ticks[offset].prevComputerDigest))
                    || !EQUAL(*((__m256i*)request->tick.saltedSpectrumDigest), *((__m256i*)ticks[offset].saltedSpectrumDigest))
                    || !EQUAL(*((__m256i*)request->tick.saltedUniverseDigest), *((__m256i*)ticks[offset].saltedUniverseDigest))
                    || !EQUAL(*((__m256i*)request->tick.saltedComputerDigest), *((__m256i*)ticks[offset].saltedComputerDigest))
                    || !EQUAL(*((__m256i*)request->tick.transactionDigest), *((__m256i*)ticks[offset].transactionDigest))
                    || !EQUAL(*((__m256i*)request->tick.expectedNextTickTransactionDigest), *((__m256i*)ticks[offset].expectedNextTickTransactionDigest)))
                {
                    faultyComputorFlags[request->tick.computorIndex >> 6] |= (1ULL << (request->tick.computorIndex & 63));
                }
            }
            else
            {
                bs->CopyMem(&ticks[offset], &request->tick, sizeof(Tick));
            }

            RELEASE(tickLocks[request->tick.computorIndex]);
        }
    }
}

static void broadcastFutureTickData(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    BroadcastFutureTickData* request = (BroadcastFutureTickData*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->tickData.epoch == system.epoch
        && request->tickData.tick > system.tick && request->tickData.tick < system.initialTick + MAX_NUMBER_OF_TICKS_PER_EPOCH
        && request->tickData.tick % NUMBER_OF_COMPUTORS == request->tickData.computorIndex
        && request->tickData.month >= 1 && request->tickData.month <= 12
        && request->tickData.day >= 1 && request->tickData.day <= ((request->tickData.month == 1 || request->tickData.month == 3 || request->tickData.month == 5 || request->tickData.month == 7 || request->tickData.month == 8 || request->tickData.month == 10 || request->tickData.month == 12) ? 31 : ((request->tickData.month == 4 || request->tickData.month == 6 || request->tickData.month == 9 || request->tickData.month == 11) ? 30 : ((request->tickData.year & 3) ? 28 : 29)))
        && request->tickData.hour <= 23
        && request->tickData.minute <= 59
        && request->tickData.second <= 59
        && !request->tickData.millisecond
        && ms(request->tickData.year, request->tickData.month, request->tickData.day, request->tickData.hour, request->tickData.minute, request->tickData.second, request->tickData.millisecond) <= ms(time.Year - 2000, time.Month, time.Day, time.Hour, time.Minute, time.Second, time.Nanosecond / 1000000) + TIME_ACCURACY)
    {
        bool ok = true;
        for (unsigned int i = 0; i < NUMBER_OF_TRANSACTIONS_PER_TICK && ok; i++)
        {
            if (!EQUAL(*((__m256i*)request->tickData.transactionDigests[i]), _mm256_setzero_si256()))
            {
                for (unsigned int j = 0; j < i; j++)
                {
                    if (EQUAL(*((__m256i*)request->tickData.transactionDigests[i]), *((__m256i*)request->tickData.transactionDigests[j])))
                    {
                        ok = false;

                        break;
                    }
                }
            }
        }
        if (ok)
        {
            unsigned char digest[32];
            request->tickData.computorIndex ^= BROADCAST_FUTURE_TICK_DATA;
            KangarooTwelve((unsigned char*)&request->tickData, sizeof(TickData) - SIGNATURE_SIZE, digest, sizeof(digest));
            request->tickData.computorIndex ^= BROADCAST_FUTURE_TICK_DATA;
            if (verify(broadcastedComputors.broadcastComputors.computors.publicKeys[request->tickData.computorIndex], digest, request->tickData.signature))
            {
                if (header->isDejavuZero())
                {
                    enqueueResponse(NULL, header);
                }

                ACQUIRE(tickDataLock);
                if (request->tickData.tick == system.tick + 1 && targetNextTickDataDigestIsKnown)
                {
                    if (!EQUAL(targetNextTickDataDigest, _mm256_setzero_si256()))
                    {
                        unsigned char digest[32];
                        KangarooTwelve((unsigned char*)&request->tickData, sizeof(TickData), digest, 32);
                        if (EQUAL(*((__m256i*)digest), targetNextTickDataDigest))
                        {
                            bs->CopyMem(&tickData[request->tickData.tick - system.initialTick], &request->tickData, sizeof(TickData));
                        }
                    }
                }
                else
                {
                    if (tickData[request->tickData.tick - system.initialTick].epoch == system.epoch)
                    {
                        if (*((unsigned long long*) & request->tickData.millisecond) != *((unsigned long long*) & tickData[request->tickData.tick - system.initialTick].millisecond))
                        {
                            faultyComputorFlags[request->tickData.computorIndex >> 6] |= (1ULL << (request->tickData.computorIndex & 63));
                        }
                        else
                        {
                            for (unsigned int i = 0; i < NUMBER_OF_TRANSACTIONS_PER_TICK; i++)
                            {
                                if (!EQUAL(*((__m256i*)request->tickData.transactionDigests[i]), *((__m256i*)tickData[request->tickData.tick - system.initialTick].transactionDigests[i])))
                                {
                                    faultyComputorFlags[request->tickData.computorIndex >> 6] |= (1ULL << (request->tickData.computorIndex & 63));

                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        bs->CopyMem(&tickData[request->tickData.tick - system.initialTick], &request->tickData, sizeof(TickData));
                    }
                }
                RELEASE(tickDataLock);
            }
        }
    }
}

static void broadcastTransaction(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    Transaction* request = (Transaction*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->amount >= 0 && request->amount <= MAX_AMOUNT
        && request->inputSize <= MAX_INPUT_SIZE && request->inputSize == header->size() - sizeof(RequestResponseHeader) - sizeof(Transaction) - SIGNATURE_SIZE)
    {
        const unsigned int transactionSize = sizeof(Transaction) + request->inputSize + SIGNATURE_SIZE;
        unsigned char digest[32];
        KangarooTwelve((unsigned char*)request, transactionSize - SIGNATURE_SIZE, digest, sizeof(digest));
        if (verify(request->sourcePublicKey, digest, (((const unsigned char*)request) + sizeof(Transaction) + request->inputSize)))
        {
            if (header->isDejavuZero())
            {
                enqueueResponse(NULL, header);
            }

            const int spectrumIndex = ::spectrumIndex(request->sourcePublicKey);
            if (spectrumIndex >= 0)
            {
                ACQUIRE(entityPendingTransactionsLock);

                if (((Transaction*)&entityPendingTransactions[spectrumIndex * MAX_TRANSACTION_SIZE])->tick < request->tick)
                {
                    bs->CopyMem(&entityPendingTransactions[spectrumIndex * MAX_TRANSACTION_SIZE], request, transactionSize);
                    KangarooTwelve((unsigned char*)request, transactionSize, &entityPendingTransactionDigests[spectrumIndex * 32ULL], 32);
                }

                RELEASE(entityPendingTransactionsLock);
            }

            ACQUIRE(tickDataLock);
            if (request->tick == system.tick + 1
                && tickData[request->tick - system.initialTick].epoch == system.epoch)
            {
                KangarooTwelve((unsigned char*)request, transactionSize, digest, sizeof(digest));
                for (unsigned int i = 0; i < NUMBER_OF_TRANSACTIONS_PER_TICK; i++)
                {
                    if (EQUAL(*((__m256i*)digest), *((__m256i*)tickData[request->tick - system.initialTick].transactionDigests[i])))
                    {
                        ACQUIRE(tickTransactionsLock);
                        if (!tickTransactionOffsets[request->tick - system.initialTick][i])
                        {
                            if (nextTickTransactionOffset + transactionSize <= FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS))
                            {
                                tickTransactionOffsets[request->tick - system.initialTick][i] = nextTickTransactionOffset;
                                bs->CopyMem(&tickTransactions[nextTickTransactionOffset], request, transactionSize);
                                nextTickTransactionOffset += transactionSize;
                            }
                        }
                        RELEASE(tickTransactionsLock);

                        break;
                    }
                }
            }
            RELEASE(tickDataLock);
        }
    }
}

static void requestComputors(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    if (broadcastedComputors.broadcastComputors.computors.epoch)
    {
        enqueueResponse(peer, sizeof(broadcastedComputors.broadcastComputors), BROADCAST_COMPUTORS, header->dejavu(), &broadcastedComputors.broadcastComputors);
    }
    else
    {
        enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
    }
}

static void requestQuorumTick(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RequestQuorumTick* request = (RequestQuorumTick*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->quorumTick.tick >= system.initialTick && request->quorumTick.tick < system.initialTick + MAX_NUMBER_OF_TICKS_PER_EPOCH)
    {
        unsigned short computorIndices[NUMBER_OF_COMPUTORS];
        unsigned short numberOfComputorIndices;
        for (numberOfComputorIndices = 0; numberOfComputorIndices < NUMBER_OF_COMPUTORS; numberOfComputorIndices++)
        {
            computorIndices[numberOfComputorIndices] = numberOfComputorIndices;
        }
        while (numberOfComputorIndices)
        {
            const unsigned short index = random(numberOfComputorIndices);

            if (!(request->quorumTick.voteFlags[computorIndices[index] >> 3] & (1 << (computorIndices[index] & 7))))
            {
                const unsigned int offset = ((request->quorumTick.tick - system.initialTick) * NUMBER_OF_COMPUTORS) + computorIndices[index];
                if (ticks[offset].epoch == system.epoch)
                {
                    enqueueResponse(peer, sizeof(Tick), BROADCAST_TICK, header->dejavu(), &ticks[offset]);
                }
            }

            computorIndices[index] = computorIndices[--numberOfComputorIndices];
        }
    }
    enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
}

static void requestTickData(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RequestTickData* request = (RequestTickData*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->requestedTickData.tick > system.initialTick && request->requestedTickData.tick < system.initialTick + MAX_NUMBER_OF_TICKS_PER_EPOCH
        && tickData[request->requestedTickData.tick - system.initialTick].epoch == system.epoch)
    {
        enqueueResponse(peer, sizeof(TickData), BROADCAST_FUTURE_TICK_DATA, header->dejavu(), &tickData[request->requestedTickData.tick - system.initialTick]);
    }
    else
    {
        enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
    }
}

static void requestTickTransactions(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RequestedTickTransactions* request = (RequestedTickTransactions*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (request->tick >= system.initialTick && request->tick < system.initialTick + MAX_NUMBER_OF_TICKS_PER_EPOCH)
    {
        unsigned short tickTransactionIndices[NUMBER_OF_TRANSACTIONS_PER_TICK];
        unsigned short numberOfTickTransactions;
        for (numberOfTickTransactions = 0; numberOfTickTransactions < NUMBER_OF_TRANSACTIONS_PER_TICK; numberOfTickTransactions++)
        {
            tickTransactionIndices[numberOfTickTransactions] = numberOfTickTransactions;
        }
        while (numberOfTickTransactions)
        {
            const unsigned short index = random(numberOfTickTransactions);

            if (!(request->transactionFlags[tickTransactionIndices[index] >> 3] & (1 << (tickTransactionIndices[index] & 7)))
                && tickTransactionOffsets[request->tick - system.initialTick][tickTransactionIndices[index]])
            {
                const Transaction* transaction = (Transaction*)&tickTransactions[tickTransactionOffsets[request->tick - system.initialTick][tickTransactionIndices[index]]];
                enqueueResponse(peer, sizeof(Transaction) + transaction->inputSize + SIGNATURE_SIZE, BROADCAST_TRANSACTION, header->dejavu(), (void*)transaction);
            }

            tickTransactionIndices[index] = tickTransactionIndices[--numberOfTickTransactions];
        }
    }
    enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
}

static void requestCurrentTickInfo(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    CurrentTickInfo currentTickInfo;

    if (broadcastedComputors.broadcastComputors.computors.epoch)
    {
        unsigned long long tickDuration = (__rdtsc() - tickTicks[sizeof(tickTicks) / sizeof(tickTicks[0]) - 1]) / frequency;
        if (tickDuration > 0xFFFF)
        {
            tickDuration = 0xFFFF;
        }
        currentTickInfo.tickDuration = (unsigned short)tickDuration;

        currentTickInfo.epoch = system.epoch;
        currentTickInfo.tick = system.tick;
        currentTickInfo.numberOfAlignedVotes = tickNumberOfComputors;
        currentTickInfo.numberOfMisalignedVotes = (tickTotalNumberOfComputors - tickNumberOfComputors);
    }
    else
    {
        bs->SetMem(&currentTickInfo, sizeof(CurrentTickInfo), 0);
    }

    enqueueResponse(peer, sizeof(currentTickInfo), RESPOND_CURRENT_TICK_INFO, header->dejavu(), &currentTickInfo);
}

static void requestEntity(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RespondedEntity respondedEntity;

    RequestedEntity* request = (RequestedEntity*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    *((__m256i*)respondedEntity.entity.publicKey) = *((__m256i*)request->publicKey);
    respondedEntity.spectrumIndex = spectrumIndex(respondedEntity.entity.publicKey);
    respondedEntity.tick = system.tick;
    if (respondedEntity.spectrumIndex < 0)
    {
        respondedEntity.entity.incomingAmount = 0;
        respondedEntity.entity.outgoingAmount = 0;
        respondedEntity.entity.numberOfIncomingTransfers = 0;
        respondedEntity.entity.numberOfOutgoingTransfers = 0;
        respondedEntity.entity.latestIncomingTransferTick = 0;
        respondedEntity.entity.latestOutgoingTransferTick = 0;

        bs->SetMem(respondedEntity.siblings, sizeof(respondedEntity.siblings), 0);
    }
    else
    {
        bs->CopyMem(&respondedEntity.entity, &spectrum[respondedEntity.spectrumIndex], sizeof(::Entity));

        int sibling = respondedEntity.spectrumIndex;
        unsigned int spectrumDigestInputOffset = 0;
        for (unsigned int j = 0; j < SPECTRUM_DEPTH; j++)
        {
            *((__m256i*)respondedEntity.siblings[j]) = spectrumDigests[spectrumDigestInputOffset + (sibling ^ 1)];
            spectrumDigestInputOffset += (SPECTRUM_CAPACITY >> j);
            sibling >>= 1;
        }
    }

    enqueueResponse(peer, sizeof(respondedEntity), RESPOND_ENTITY, header->dejavu(), &respondedEntity);
}

static void requestContractIPO(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RespondContractIPO respondContractIPO;

    RequestContractIPO* request = (RequestContractIPO*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    respondContractIPO.contractIndex = request->contractIndex;
    respondContractIPO.tick = system.tick;
    if (request->contractIndex >= sizeof(contractDescriptions) / sizeof(contractDescriptions[0])
        || system.epoch >= contractDescriptions[request->contractIndex].constructionEpoch)
    {
        bs->SetMem(respondContractIPO.publicKeys, sizeof(respondContractIPO.publicKeys), 0);
        bs->SetMem(respondContractIPO.prices, sizeof(respondContractIPO.prices), 0);
    }
    else
    {
        IPO* ipo = (IPO*)contractStates[request->contractIndex];
        bs->CopyMem(respondContractIPO.publicKeys, ipo->publicKeys, sizeof(respondContractIPO.publicKeys));
        bs->CopyMem(respondContractIPO.prices, ipo->prices, sizeof(respondContractIPO.prices));
    }

    enqueueResponse(peer, sizeof(respondContractIPO), RESPOND_CONTRACT_IPO, header->dejavu(), &respondContractIPO);
}

static void requestIssuedAssets(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RespondIssuedAssets response;

    RequestIssuedAssets* request = (RequestIssuedAssets*)((char*)processor->buffer + sizeof(RequestResponseHeader));

    unsigned int universeIndex = (*((unsigned int*)request->publicKey)) & (ASSETS_CAPACITY - 1);

    ACQUIRE(universeLock);

iteration:
    if (universeIndex >= ASSETS_CAPACITY
        || assets[universeIndex].varStruct.issuance.type == EMPTY)
    {
        enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
    }
    else
    {
        if (assets[universeIndex].varStruct.issuance.type == ISSUANCE
            && EQUAL(*((__m256i*)assets[universeIndex].varStruct.issuance.publicKey), *((__m256i*)request->publicKey)))
        {
            bs->CopyMem(&response.asset, &assets[universeIndex], sizeof(Asset));
            response.tick = system.tick;

            enqueueResponse(peer, sizeof(response), RESPOND_ISSUED_ASSETS, header->dejavu(), &response);
        }

        universeIndex = (universeIndex + 1) & (ASSETS_CAPACITY - 1);

        goto iteration;
    }

    RELEASE(universeLock);
}

static void requestOwnedAssets(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RespondOwnedAssets response;

    RequestOwnedAssets* request = (RequestOwnedAssets*)((char*)processor->buffer + sizeof(RequestResponseHeader));

    unsigned int universeIndex = (*((unsigned int*)request->publicKey)) & (ASSETS_CAPACITY - 1);

    ACQUIRE(universeLock);

iteration:
    if (universeIndex >= ASSETS_CAPACITY
        || assets[universeIndex].varStruct.issuance.type == EMPTY)
    {
        enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
    }
    else
    {
        if (assets[universeIndex].varStruct.issuance.type == OWNERSHIP
            && EQUAL(*((__m256i*)assets[universeIndex].varStruct.issuance.publicKey), *((__m256i*)request->publicKey)))
        {
            bs->CopyMem(&response.asset, &assets[universeIndex], sizeof(Asset));
            bs->CopyMem(&response.issuanceAsset, &assets[assets[universeIndex].varStruct.ownership.issuanceIndex], sizeof(Asset));
            response.tick = system.tick;

            enqueueResponse(peer, sizeof(response), RESPOND_OWNED_ASSETS, header->dejavu(), &response);
        }

        universeIndex = (universeIndex + 1) & (ASSETS_CAPACITY - 1);

        goto iteration;
    }

    RELEASE(universeLock);
}

static void requestPossessedAssets(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    RespondPossessedAssets response;

    RequestPossessedAssets* request = (RequestPossessedAssets*)((char*)processor->buffer + sizeof(RequestResponseHeader));

    unsigned int universeIndex = (*((unsigned int*)request->publicKey)) & (ASSETS_CAPACITY - 1);

    ACQUIRE(universeLock);

iteration:
    if (universeIndex >= ASSETS_CAPACITY
        || assets[universeIndex].varStruct.issuance.type == EMPTY)
    {
        enqueueResponse(peer, 0, END_RESPONSE, header->dejavu(), NULL);
    }
    else
    {
        if (assets[universeIndex].varStruct.issuance.type == POSSESSION
            && EQUAL(*((__m256i*)assets[universeIndex].varStruct.issuance.publicKey), *((__m256i*)request->publicKey)))
        {
            bs->CopyMem(&response.asset, &assets[universeIndex], sizeof(Asset));
            bs->CopyMem(&response.ownershipAsset, &assets[assets[universeIndex].varStruct.possession.ownershipIndex], sizeof(Asset));
            bs->CopyMem(&response.issuanceAsset, &assets[assets[assets[universeIndex].varStruct.possession.ownershipIndex].varStruct.ownership.issuanceIndex], sizeof(Asset));
            response.tick = system.tick;

            enqueueResponse(peer, sizeof(response), RESPOND_POSSESSED_ASSETS, header->dejavu(), &response);
        }

        universeIndex = (universeIndex + 1) & (ASSETS_CAPACITY - 1);

        goto iteration;
    }

    RELEASE(universeLock);
}

static void processSpecialCommand(Peer* peer, Processor* processor, RequestResponseHeader* header)
{
    SpecialCommand* request = (SpecialCommand*)((char*)processor->buffer + sizeof(RequestResponseHeader));
    if (header->size() >= sizeof(RequestResponseHeader) + sizeof(SpecialCommand) + SIGNATURE_SIZE
        && (request->everIncreasingNonceAndCommandType & 0xFFFFFFFFFFFFFF) > system.latestOperatorNonce)
    {
        unsigned char digest[32];
        KangarooTwelve((unsigned char*)request, header->size() - sizeof(RequestResponseHeader) - SIGNATURE_SIZE, digest, sizeof(digest));
        if (verify(operatorPublicKey, digest, ((const unsigned char*)processor->buffer + (header->size() - SIGNATURE_SIZE))))
        {
            system.latestOperatorNonce = request->everIncreasingNonceAndCommandType & 0xFFFFFFFFFFFFFF;

            switch (request->everIncreasingNonceAndCommandType >> 56)
            {
            case SPECIAL_COMMAND_SHUT_DOWN:
            {
                state = 1;
            }
            break;

            case SPECIAL_COMMAND_GET_PROPOSAL_AND_BALLOT_REQUEST:
            {
                SpecialCommandGetProposalAndBallotRequest* request = (SpecialCommandGetProposalAndBallotRequest*)((char*)processor->buffer + sizeof(RequestResponseHeader));
                if (request->computorIndex < NUMBER_OF_COMPUTORS)
                {
                    SpecialCommandGetProposalAndBallotResponse response;

                    response.everIncreasingNonceAndCommandType = (request->everIncreasingNonceAndCommandType & 0xFFFFFFFFFFFFFF) | (SPECIAL_COMMAND_GET_PROPOSAL_AND_BALLOT_RESPONSE << 56);
                    response.computorIndex = request->computorIndex;
                    *((short*)response.padding) = 0;
                    bs->CopyMem(&response.proposal, &system.proposals[request->computorIndex], sizeof(ComputorProposal));
                    bs->CopyMem(&response.ballot, &system.ballots[request->computorIndex], sizeof(ComputorBallot));

                    enqueueResponse(peer, sizeof(response), SPECIAL_COMMAND_GET_PROPOSAL_AND_BALLOT_RESPONSE, header->dejavu(), &response);
                }
            }
            break;

            case SPECIAL_COMMAND_SET_PROPOSAL_AND_BALLOT_REQUEST:
            {
                SpecialCommandSetProposalAndBallotRequest* request = (SpecialCommandSetProposalAndBallotRequest*)((char*)processor->buffer + sizeof(RequestResponseHeader));
                if (request->computorIndex < NUMBER_OF_COMPUTORS)
                {
                    bs->CopyMem(&system.proposals[request->computorIndex], &request->proposal, sizeof(ComputorProposal));
                    bs->CopyMem(&system.ballots[request->computorIndex], &request->ballot, sizeof(ComputorBallot));

                    SpecialCommandSetProposalAndBallotResponse response;

                    response.everIncreasingNonceAndCommandType = (request->everIncreasingNonceAndCommandType & 0xFFFFFFFFFFFFFF) | (SPECIAL_COMMAND_SET_PROPOSAL_AND_BALLOT_RESPONSE << 56);
                    response.computorIndex = request->computorIndex;
                    *((short*)response.padding) = 0;

                    enqueueResponse(peer, sizeof(response), SPECIAL_COMMAND_SET_PROPOSAL_AND_BALLOT_RESPONSE, header->dejavu(), &response);
                }
            }
            break;
            }
        }
    }
}

static void requestProcessor(void* ProcedureArgument)
{
    enableAVX();

    unsigned long long processorNumber;
    mpServicesProtocol->WhoAmI(mpServicesProtocol, &processorNumber);

    Processor* processor = (Processor*)ProcedureArgument;
    RequestResponseHeader* header = (RequestResponseHeader*)processor->buffer;
    while (!state)
    {
        if (requestQueueElementTail == requestQueueElementHead)
        {
            _mm_pause();
        }
        else
        {
            ACQUIRE(requestQueueTailLock);

            if (requestQueueElementTail == requestQueueElementHead)
            {
                RELEASE(requestQueueTailLock);
            }
            else
            {
                const unsigned long long beginningTick = __rdtsc();

                {
                    RequestResponseHeader* requestHeader = (RequestResponseHeader*)&requestQueueBuffer[requestQueueElements[requestQueueElementTail].offset];
                    bs->CopyMem(header, requestHeader, requestHeader->size());
                    requestQueueBufferTail += requestHeader->size();
                }

                Peer* peer = requestQueueElements[requestQueueElementTail].peer;

                if (requestQueueBufferTail > REQUEST_QUEUE_BUFFER_SIZE - BUFFER_SIZE)
                {
                    requestQueueBufferTail = 0;
                }
                requestQueueElementTail++;

                RELEASE(requestQueueTailLock);

                switch (header->type())
                {
                case EXCHANGE_PUBLIC_PEERS:
                {
                    exchangePublicPeers(peer, processor, header);
                }
                break;

                case BROADCAST_MESSAGE:
                {
                    broadcastMessage(processorNumber, processor, header);
                }
                break;

                case BROADCAST_COMPUTORS:
                {
                    broadcastComputors(peer, processor, header);
                }
                break;

                case BROADCAST_TICK:
                {
                    broadcastTick(peer, processor, header);
                }
                break;

                case BROADCAST_FUTURE_TICK_DATA:
                {
                    broadcastFutureTickData(peer, processor, header);
                }
                break;

                case BROADCAST_TRANSACTION:
                {
                    broadcastTransaction(peer, processor, header);
                }
                break;

                case REQUEST_COMPUTORS:
                {
                    requestComputors(peer, processor, header);
                }
                break;

                case REQUEST_QUORUM_TICK:
                {
                    requestQuorumTick(peer, processor, header);
                }
                break;

                case REQUEST_TICK_DATA:
                {
                    requestTickData(peer, processor, header);
                }
                break;

                case REQUEST_TICK_TRANSACTIONS:
                {
                    requestTickTransactions(peer, processor, header);
                }
                break;

                case REQUEST_CURRENT_TICK_INFO:
                {
                    requestCurrentTickInfo(peer, processor, header);
                }
                break;

                case REQUEST_ENTITY:
                {
                    requestEntity(peer, processor, header);
                }
                break;

                case REQUEST_CONTRACT_IPO:
                {
                    requestContractIPO(peer, processor, header);
                }
                break;

                case REQUEST_ISSUED_ASSETS:
                {
                    requestIssuedAssets(peer, processor, header);
                }
                break;

                case REQUEST_OWNED_ASSETS:
                {
                    requestOwnedAssets(peer, processor, header);
                }
                break;

                case REQUEST_POSSESSED_ASSETS:
                {
                    requestPossessedAssets(peer, processor, header);
                }
                break;

                case PROCESS_SPECIAL_COMMAND:
                {
                    processSpecialCommand(peer, processor, header);
                }
                break;
                }

                queueProcessingNumerator += __rdtsc() - beginningTick;
                queueProcessingDenominator++;

                _InterlockedIncrement64(&numberOfProcessedRequests);
            }
        }
    }
}

static EFI_HANDLE getTcp4Protocol(const unsigned char* remoteAddress, const unsigned short port, EFI_TCP4_PROTOCOL** tcp4Protocol)
{
    EFI_STATUS status;
    EFI_HANDLE childHandle = NULL;
    if (status = tcp4ServiceBindingProtocol->CreateChild(tcp4ServiceBindingProtocol, &childHandle))
    {
        logStatus(L"EFI_TCP4_SERVICE_BINDING_PROTOCOL.CreateChild() fails", status, __LINE__);

        return NULL;
    }
    else
    {
        if (status = bs->OpenProtocol(childHandle, &tcp4ProtocolGuid, (void**)tcp4Protocol, ih, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL))
        {
            logStatus(L"EFI_BOOT_SERVICES.OpenProtocol() fails", status, __LINE__);

            return NULL;
        }
        else
        {
            EFI_TCP4_CONFIG_DATA configData;
            bs->SetMem(&configData, sizeof(configData), 0);
            configData.TimeToLive = 64;
            configData.AccessPoint.UseDefaultAddress = TRUE;
            if (!remoteAddress)
            {
                configData.AccessPoint.StationPort = port;
            }
            else
            {
                *((int*)configData.AccessPoint.RemoteAddress.Addr) = *((int*)remoteAddress);
                configData.AccessPoint.RemotePort = port;
                configData.AccessPoint.ActiveFlag = TRUE;
            }
            EFI_TCP4_OPTION option;
            bs->SetMem(&option, sizeof(option), 0);
            option.ReceiveBufferSize = BUFFER_SIZE;
            option.SendBufferSize = BUFFER_SIZE;
            option.KeepAliveProbes = 1;
            option.EnableWindowScaling = TRUE;
            configData.ControlOption = &option;

            if ((status = (*tcp4Protocol)->Configure(*tcp4Protocol, &configData))
                && status != EFI_NO_MAPPING)
            {
                logStatus(L"EFI_TCP4_PROTOCOL.Configure() fails", status, __LINE__);

                return NULL;
            }
            else
            {
                EFI_IP4_MODE_DATA modeData;

                if (status == EFI_NO_MAPPING)
                {
                    while (!(status = (*tcp4Protocol)->GetModeData(*tcp4Protocol, NULL, NULL, &modeData, NULL, NULL))
                        && !modeData.IsConfigured)
                    {
                        _mm_pause();
                    }
                    if (!status)
                    {
                        if (status = (*tcp4Protocol)->Configure(*tcp4Protocol, &configData))
                        {
                            logStatus(L"EFI_TCP4_PROTOCOL.Configure() fails", status, __LINE__);

                            return NULL;
                        }
                    }
                }

                if (status = (*tcp4Protocol)->GetModeData(*tcp4Protocol, NULL, &configData, &modeData, NULL, NULL))
                {
                    logStatus(L"EFI_TCP4_PROTOCOL.GetModeData() fails", status, __LINE__);

                    return NULL;
                }
                else
                {
                    if (!modeData.IsStarted || !modeData.IsConfigured)
                    {
                        log(L"EFI_TCP4_PROTOCOL is not configured!");

                        return NULL;
                    }
                    else
                    {
                        if (!remoteAddress)
                        {
                            setText(message, L"Local address = ");
                            appendIPv4Address(message, configData.AccessPoint.StationAddress);
                            appendText(message, L":");
                            appendNumber(message, configData.AccessPoint.StationPort, FALSE);
                            appendText(message, L".");
                            log(message);

                            log(L"Routes:");
                            for (unsigned int i = 0; i < modeData.RouteCount; i++)
                            {
                                setText(message, L"Address = ");
                                appendIPv4Address(message, modeData.RouteTable[i].SubnetAddress);
                                appendText(message, L" | mask = ");
                                appendIPv4Address(message, modeData.RouteTable[i].SubnetMask);
                                appendText(message, L" | gateway = ");
                                appendIPv4Address(message, modeData.RouteTable[i].GatewayAddress);
                                appendText(message, L".");
                                log(message);
                            }
                        }

                        return childHandle;
                    }
                }
            }
        }
    }
}

static void __beginFunctionOrProcedure(const unsigned int functionOrProcedureId)
{
    if (functionFlags[functionOrProcedureId >> 6] & (1ULL << (functionOrProcedureId & 63)))
    {
        // TODO
    }
    else
    {
        functionFlags[functionOrProcedureId >> 6] |= (1ULL << (functionOrProcedureId & 63));
    }
}

static void __endFunctionOrProcedure(const unsigned int functionOrProcedureId)
{
    functionFlags[functionOrProcedureId >> 6] &= ~(1ULL << (functionOrProcedureId & 63));

    contractStateChangeFlags[functionOrProcedureId >> (22 + 6)] |= (1ULL << ((functionOrProcedureId >> 22) & 63));
}

static void __registerUserFunction(unsigned short inputType, unsigned short inputSize)
{
    // TODO
}

static void __registerUserProcedure(USER_PROCEDURE userProcedure, unsigned short inputType, unsigned short inputSize)
{
    contractUserProcedures[executedContractIndex][inputType] = userProcedure;
    contractUserProcedureInputSizes[executedContractIndex][inputType] = inputSize;
}

static __m256i __arbitrator()
{
    return arbitratorPublicKey;
}

static __m256i __computor(unsigned short computorIndex)
{
    return *((__m256i*)broadcastedComputors.broadcastComputors.computors.publicKeys[computorIndex % NUMBER_OF_COMPUTORS]);
}

static unsigned char __day()
{
    return etalonTick.day;
}

static unsigned char __dayOfWeek(unsigned char year, unsigned char month, unsigned char day)
{
    return dayIndex(year, month, day) % 7;
}

static unsigned short __epoch()
{
    return system.epoch;
}

static bool __getEntity(__m256i id, ::Entity& entity)
{
    int index = spectrumIndex((unsigned char*)&id);
    if (index < 0)
    {
        *((__m256i*)&entity.publicKey) = id;
        entity.incomingAmount = 0;
        entity.outgoingAmount = 0;
        entity.numberOfIncomingTransfers = 0;
        entity.numberOfOutgoingTransfers = 0;
        entity.latestIncomingTransferTick = 0;
        entity.latestOutgoingTransferTick = 0;

        return false;
    }
    else
    {
        *((__m256i*)&entity.publicKey) = *((__m256i*)&spectrum[index].publicKey);
        entity.incomingAmount = spectrum[index].incomingAmount;
        entity.outgoingAmount = spectrum[index].outgoingAmount;
        entity.numberOfIncomingTransfers = spectrum[index].numberOfIncomingTransfers;
        entity.numberOfOutgoingTransfers = spectrum[index].numberOfOutgoingTransfers;
        entity.latestIncomingTransferTick = spectrum[index].latestIncomingTransferTick;
        entity.latestOutgoingTransferTick = spectrum[index].latestOutgoingTransferTick;

        return true;
    }
}

static unsigned char __hour()
{
    return etalonTick.hour;
}

static unsigned short __millisecond()
{
    return etalonTick.millisecond;
}

static unsigned char __minute()
{
    return etalonTick.minute;
}

static unsigned char __month()
{
    return etalonTick.month;
}

static __m256i __nextId(__m256i currentId)
{
    int index = spectrumIndex((unsigned char*)&currentId);
    while (++index < SPECTRUM_CAPACITY)
    {
        const __m256i nextId = *((__m256i*)spectrum[index].publicKey);
        if (!EQUAL(nextId, _mm256_setzero_si256()))
        {
            return nextId;
        }
    }

    return _mm256_setzero_si256();
}

static unsigned char __second()
{
    return etalonTick.second;
}

static unsigned int __tick()
{
    return system.tick;
}

static long long __transfer(__m256i destination, long long amount)
{
    if (((unsigned long long)amount) > MAX_AMOUNT)
    {
        return -((long long)(MAX_AMOUNT + 1));
    }

    const int index = spectrumIndex((unsigned char*)&currentContract);

    if (index < 0)
    {
        return -amount;
    }

    const long long remainingAmount = energy(index) - amount;

    if (remainingAmount < 0)
    {
        return remainingAmount;
    }

    if (decreaseEnergy(index, amount))
    {
        increaseEnergy((unsigned char*)&destination, amount);
    }

    return remainingAmount;
}

static unsigned char __year()
{
    return etalonTick.year;
}

static void contractProcessor(void*)
{
    enableAVX();

    switch (contractProcessorPhase)
    {
    case INITIALIZE:
    {
        for (executedContractIndex = 1; executedContractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); executedContractIndex++)
        {
            if (system.epoch == contractDescriptions[executedContractIndex].constructionEpoch
                && system.epoch < contractDescriptions[executedContractIndex].destructionEpoch)
            {
                currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                contractSystemProcedures[executedContractIndex][INITIALIZE](contractStates[executedContractIndex]);
            }
        }
    }
    break;

    case BEGIN_EPOCH:
    {
        for (executedContractIndex = 1; executedContractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); executedContractIndex++)
        {
            if (system.epoch >= contractDescriptions[executedContractIndex].constructionEpoch
                && system.epoch < contractDescriptions[executedContractIndex].destructionEpoch)
            {
                currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                contractSystemProcedures[executedContractIndex][BEGIN_EPOCH](contractStates[executedContractIndex]);
            }
        }
    }
    break;

    case BEGIN_TICK:
    {
        for (executedContractIndex = 1; executedContractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); executedContractIndex++)
        {
            if (system.epoch >= contractDescriptions[executedContractIndex].constructionEpoch
                && system.epoch < contractDescriptions[executedContractIndex].destructionEpoch)
            {
                currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                contractSystemProcedures[executedContractIndex][BEGIN_TICK](contractStates[executedContractIndex]);
            }
        }
    }
    break;

    case END_TICK:
    {
        for (executedContractIndex = sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); executedContractIndex-- > 1; )
        {
            if (system.epoch >= contractDescriptions[executedContractIndex].constructionEpoch
                && system.epoch < contractDescriptions[executedContractIndex].destructionEpoch)
            {
                currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                contractSystemProcedures[executedContractIndex][END_TICK](contractStates[executedContractIndex]);
            }
        }
    }
    break;

    case END_EPOCH:
    {
        for (executedContractIndex = sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); executedContractIndex-- > 1; )
        {
            if (system.epoch >= contractDescriptions[executedContractIndex].constructionEpoch
                && system.epoch < contractDescriptions[executedContractIndex].destructionEpoch)
            {
                currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                contractSystemProcedures[executedContractIndex][END_EPOCH](contractStates[executedContractIndex]);
            }
        }
    }
    break;
    }
}

static void processTick(unsigned long long processorNumber)
{
    if (tickPhase < 1)
    {
        tickPhase = 1;
    }

#if !IGNORE_RESOURCE_TESTING
    etalonTick.prevResourceTestingDigest = resourceTestingDigest;
#endif
    *((__m256i*)etalonTick.prevSpectrumDigest) = spectrumDigests[(SPECTRUM_CAPACITY * 2 - 1) - 1];
    getUniverseDigest((__m256i*)etalonTick.prevUniverseDigest);
    getComputerDigest((__m256i*)etalonTick.prevComputerDigest);

    if (system.tick == system.initialTick)
    {
        contractProcessorPhase = INITIALIZE;
        contractProcessorState = 1;
        while (contractProcessorState)
        {
            _mm_pause();
        }

        contractProcessorPhase = BEGIN_EPOCH;
        contractProcessorState = 1;
        while (contractProcessorState)
        {
            _mm_pause();
        }
    }

    contractProcessorPhase = BEGIN_TICK;
    contractProcessorState = 1;
    while (contractProcessorState)
    {
        _mm_pause();
    }

    ACQUIRE(tickDataLock);
    bs->CopyMem(&nextTickData, &tickData[system.tick - system.initialTick], sizeof(TickData));
    RELEASE(tickDataLock);
    if (nextTickData.epoch == system.epoch)
    {
        bs->SetMem(entityPendingTransactionIndices, sizeof(entityPendingTransactionIndices), 0);
        for (unsigned int transactionIndex = 0; transactionIndex < NUMBER_OF_TRANSACTIONS_PER_TICK; transactionIndex++)
        {
            if (!EQUAL(*((__m256i*)nextTickData.transactionDigests[transactionIndex]), _mm256_setzero_si256()))
            {
                if (tickTransactionOffsets[system.tick - system.initialTick][transactionIndex])
                {
                    Transaction* transaction = (Transaction*)&tickTransactions[tickTransactionOffsets[system.tick - system.initialTick][transactionIndex]];
                    const int spectrumIndex = ::spectrumIndex(transaction->sourcePublicKey);
                    if (spectrumIndex >= 0
                        && !entityPendingTransactionIndices[spectrumIndex])
                    {
                        entityPendingTransactionIndices[spectrumIndex] = 1;

                        numberOfTransactions++;
                        if (decreaseEnergy(spectrumIndex, transaction->amount))
                        {
                            increaseEnergy(transaction->destinationPublicKey, transaction->amount);
                        }

                        if (EQUAL(*((__m256i*)transaction->destinationPublicKey), _mm256_setzero_si256()))
                        {
                            // Nothing to do
                        }
                        else
                        {
                            unsigned char maskedDestinationPublicKey[32];
                            *((__m256i*)maskedDestinationPublicKey) = *((__m256i*)transaction->destinationPublicKey);
                            *((unsigned int*)maskedDestinationPublicKey) &= ~(MAX_NUMBER_OF_CONTRACTS - 1);
                            executedContractIndex = *((unsigned int*)transaction->destinationPublicKey);
                            if (EQUAL(*((__m256i*)maskedDestinationPublicKey), _mm256_setzero_si256())
                                && executedContractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]))
                            {
                                if (system.epoch < contractDescriptions[executedContractIndex].constructionEpoch)
                                {
                                    if (!transaction->amount
                                        && transaction->inputSize == sizeof(ContractIPOBid))
                                    {
                                        ContractIPOBid* contractIPOBid = (ContractIPOBid*)(((unsigned char*)transaction) + sizeof(Transaction));
                                        if (contractIPOBid->price > 0 && contractIPOBid->price <= MAX_AMOUNT / NUMBER_OF_COMPUTORS
                                            && contractIPOBid->quantity > 0 && contractIPOBid->quantity <= NUMBER_OF_COMPUTORS)
                                        {
                                            const long long amount = contractIPOBid->price * contractIPOBid->quantity;
                                            if (decreaseEnergy(spectrumIndex, amount))
                                            {
                                                numberOfReleasedEntities = 0;
                                                IPO* ipo = (IPO*)contractStates[executedContractIndex];
                                                for (unsigned int i = 0; i < contractIPOBid->quantity; i++)
                                                {
                                                    if (contractIPOBid->price <= ipo->prices[NUMBER_OF_COMPUTORS - 1])
                                                    {
                                                        unsigned int j;
                                                        for (j = 0; j < numberOfReleasedEntities; j++)
                                                        {
                                                            if (EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)releasedPublicKeys[j])))
                                                            {
                                                                break;
                                                            }
                                                        }
                                                        if (j == numberOfReleasedEntities)
                                                        {
                                                            *((__m256i*)releasedPublicKeys[numberOfReleasedEntities]) = *((__m256i*)transaction->sourcePublicKey);
                                                            releasedAmounts[numberOfReleasedEntities++] = contractIPOBid->price;
                                                        }
                                                        else
                                                        {
                                                            releasedAmounts[j] += contractIPOBid->price;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        unsigned int j;
                                                        for (j = 0; j < numberOfReleasedEntities; j++)
                                                        {
                                                            if (EQUAL(*((__m256i*)ipo->publicKeys[NUMBER_OF_COMPUTORS - 1]), *((__m256i*)releasedPublicKeys[j])))
                                                            {
                                                                break;
                                                            }
                                                        }
                                                        if (j == numberOfReleasedEntities)
                                                        {
                                                            *((__m256i*)releasedPublicKeys[numberOfReleasedEntities]) = *((__m256i*)ipo->publicKeys[NUMBER_OF_COMPUTORS - 1]);
                                                            releasedAmounts[numberOfReleasedEntities++] = ipo->prices[NUMBER_OF_COMPUTORS - 1];
                                                        }
                                                        else
                                                        {
                                                            releasedAmounts[j] += ipo->prices[NUMBER_OF_COMPUTORS - 1];
                                                        }

                                                        *((__m256i*)ipo->publicKeys[NUMBER_OF_COMPUTORS - 1]) = *((__m256i*)transaction->sourcePublicKey);
                                                        ipo->prices[NUMBER_OF_COMPUTORS - 1] = contractIPOBid->price;
                                                        j = NUMBER_OF_COMPUTORS - 1;
                                                        while (j
                                                            && ipo->prices[j - 1] < ipo->prices[j])
                                                        {
                                                            const __m256i tmpPublicKey = *((__m256i*)ipo->publicKeys[j - 1]);
                                                            const long long tmpPrice = ipo->prices[j - 1];
                                                            *((__m256i*)ipo->publicKeys[j - 1]) = *((__m256i*)ipo->publicKeys[j]);
                                                            ipo->prices[j - 1] = ipo->prices[j];
                                                            *((__m256i*)ipo->publicKeys[j]) = tmpPublicKey;
                                                            ipo->prices[j--] = tmpPrice;
                                                        }

                                                        contractStateChangeFlags[executedContractIndex >> 6] |= (1ULL << (executedContractIndex & 63));
                                                    }
                                                }
                                                for (unsigned int i = 0; i < numberOfReleasedEntities; i++)
                                                {
                                                    increaseEnergy(releasedPublicKeys[i], releasedAmounts[i]);
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    if (contractUserProcedures[executedContractIndex][transaction->inputType])
                                    {
                                        currentContract = _mm256_set_epi64x(0, 0, 0, executedContractIndex);

                                        bs->SetMem(&executedContractInput, sizeof(executedContractInput), 0);
                                        bs->CopyMem(&executedContractInput, (((unsigned char*)transaction) + sizeof(Transaction)), transaction->inputSize);
                                        contractUserProcedures[executedContractIndex][transaction->inputType](contractStates[executedContractIndex], &executedContractInput, NULL);
                                    }
                                }
                            }
                            else
                            {
                                if (EQUAL(*((__m256i*)transaction->destinationPublicKey), arbitratorPublicKey))
                                {
                                    if (!transaction->amount
                                        && transaction->inputSize == 32
                                        && !transaction->inputType)
                                    {
#if !IGNORE_RESOURCE_TESTING
                                        unsigned char data[32 + 32];
                                        *((__m256i*)&data[0]) = *((__m256i*)transaction->sourcePublicKey);
                                        *((__m256i*)&data[32]) = *((__m256i*)(((unsigned char*)transaction) + sizeof(Transaction)));
                                        unsigned int flagIndex;
                                        KangarooTwelve(data, sizeof(data), (unsigned char*)&flagIndex, sizeof(flagIndex));
                                        if (!(minerSolutionFlags[flagIndex >> 6] & (1ULL << (flagIndex & 63))))
                                        {
                                            minerSolutionFlags[flagIndex >> 6] |= (1ULL << (flagIndex & 63));

                                            unsigned long long score = ::score(processorNumber, transaction->sourcePublicKey, ((unsigned char*)transaction) + sizeof(Transaction));

                                            resourceTestingDigest ^= score;
                                            KangarooTwelve((unsigned char*)&resourceTestingDigest, sizeof(resourceTestingDigest), (unsigned char*)&resourceTestingDigest, sizeof(resourceTestingDigest));

                                            if (score >= SOLUTION_THRESHOLD)
                                            {
                                                for (unsigned int i = 0; i < sizeof(computorSeeds) / sizeof(computorSeeds[0]); i++)
                                                {
                                                    if (EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)computorPublicKeys[i])))
                                                    {
                                                        ACQUIRE(solutionsLock);

                                                        unsigned int j;
                                                        for (j = 0; j < system.numberOfSolutions; j++)
                                                        {
                                                            if (EQUAL(*((__m256i*)(((unsigned char*)transaction) + sizeof(Transaction))), *((__m256i*)system.solutions[j].nonce))
                                                                && EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)system.solutions[j].computorPublicKey)))
                                                            {
                                                                solutionPublicationTicks[j] = -1;

                                                                break;
                                                            }
                                                        }
                                                        if (j == system.numberOfSolutions
                                                            && system.numberOfSolutions < MAX_NUMBER_OF_SOLUTIONS)
                                                        {
                                                            *((__m256i*)system.solutions[system.numberOfSolutions].computorPublicKey) = *((__m256i*)transaction->sourcePublicKey);
                                                            *((__m256i*)system.solutions[system.numberOfSolutions].nonce) = *((__m256i*)(((unsigned char*)transaction) + sizeof(Transaction)));
                                                            solutionPublicationTicks[system.numberOfSolutions++] = -1;
                                                        }

                                                        RELEASE(solutionsLock);

                                                        break;
                                                    }
                                                }

                                                unsigned int minerIndex;
                                                for (minerIndex = 0; minerIndex < numberOfMiners; minerIndex++)
                                                {
                                                    if (EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)minerPublicKeys[minerIndex])))
                                                    {
                                                        minerScores[minerIndex]++;

                                                        break;
                                                    }
                                                }
                                                if (minerIndex == numberOfMiners
                                                    && numberOfMiners < MAX_NUMBER_OF_MINERS)
                                                {
                                                    *((__m256i*)minerPublicKeys[numberOfMiners]) = *((__m256i*)transaction->sourcePublicKey);
                                                    minerScores[numberOfMiners++] = 1;
                                                }

                                                const __m256i tmpPublicKey = *((__m256i*)minerPublicKeys[minerIndex]);
                                                const unsigned int tmpScore = minerScores[minerIndex];
                                                while (minerIndex > (unsigned int)(minerIndex < NUMBER_OF_COMPUTORS ? 0 : NUMBER_OF_COMPUTORS)
                                                    && minerScores[minerIndex - 1] < minerScores[minerIndex])
                                                {
                                                    *((__m256i*)minerPublicKeys[minerIndex]) = *((__m256i*)minerPublicKeys[minerIndex - 1]);
                                                    minerScores[minerIndex] = minerScores[minerIndex - 1];
                                                    *((__m256i*)minerPublicKeys[--minerIndex]) = tmpPublicKey;
                                                    minerScores[minerIndex] = tmpScore;
                                                }

                                                for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS - QUORUM; i++)
                                                {
                                                    competitorPublicKeys[i] = *((__m256i*)minerPublicKeys[QUORUM + i]);
                                                    competitorScores[i] = minerScores[QUORUM + i];
                                                    competitorComputorStatuses[QUORUM + i] = true;

                                                    if (NUMBER_OF_COMPUTORS + i < numberOfMiners)
                                                    {
                                                        competitorPublicKeys[i + (NUMBER_OF_COMPUTORS - QUORUM)] = *((__m256i*)minerPublicKeys[NUMBER_OF_COMPUTORS + i]);
                                                        competitorScores[i + (NUMBER_OF_COMPUTORS - QUORUM)] = minerScores[NUMBER_OF_COMPUTORS + i];
                                                    }
                                                    else
                                                    {
                                                        competitorScores[i + (NUMBER_OF_COMPUTORS - QUORUM)] = 0;
                                                    }
                                                    competitorComputorStatuses[i + (NUMBER_OF_COMPUTORS - QUORUM)] = false;
                                                }
                                                for (unsigned int i = NUMBER_OF_COMPUTORS - QUORUM; i < (NUMBER_OF_COMPUTORS - QUORUM) * 2; i++)
                                                {
                                                    int j = i;
                                                    const __m256i tmpPublicKey = competitorPublicKeys[j];
                                                    const unsigned int tmpScore = competitorScores[j];
                                                    const bool tmpComputorStatus = false;
                                                    while (j
                                                        && competitorScores[j - 1] < competitorScores[j])
                                                    {
                                                        competitorPublicKeys[j] = competitorPublicKeys[j - 1];
                                                        competitorScores[j] = competitorScores[j - 1];
                                                        competitorPublicKeys[--j] = tmpPublicKey;
                                                        competitorScores[j] = tmpScore;
                                                    }
                                                }

                                                minimumComputorScore = competitorScores[NUMBER_OF_COMPUTORS - QUORUM - 1];

                                                unsigned char candidateCounter = 0;
                                                for (unsigned int i = 0; i < (NUMBER_OF_COMPUTORS - QUORUM) * 2; i++)
                                                {
                                                    if (!competitorComputorStatuses[i])
                                                    {
                                                        minimumCandidateScore = competitorScores[i];
                                                        candidateCounter++;
                                                    }
                                                }
                                                if (candidateCounter < NUMBER_OF_COMPUTORS - QUORUM)
                                                {
                                                    minimumCandidateScore = minimumComputorScore;
                                                }

                                                for (unsigned int i = 0; i < QUORUM; i++)
                                                {
                                                    system.futureComputors[i] = *((__m256i*)minerPublicKeys[i]);
                                                }
                                                for (unsigned int i = QUORUM; i < NUMBER_OF_COMPUTORS; i++)
                                                {
                                                    system.futureComputors[i] = competitorPublicKeys[i - QUORUM];
                                                }
                                            }
                                        }
                                        else
#endif
                                        {
                                            for (unsigned int i = 0; i < sizeof(computorSeeds) / sizeof(computorSeeds[0]); i++)
                                            {
                                                if (EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)computorPublicKeys[i])))
                                                {
                                                    ACQUIRE(solutionsLock);

                                                    unsigned int j;
                                                    for (j = 0; j < system.numberOfSolutions; j++)
                                                    {
                                                        if (EQUAL(*((__m256i*)(((unsigned char*)transaction) + sizeof(Transaction))), *((__m256i*)system.solutions[j].nonce))
                                                            && EQUAL(*((__m256i*)transaction->sourcePublicKey), *((__m256i*)system.solutions[j].computorPublicKey)))
                                                        {
                                                            solutionPublicationTicks[j] = -1;

                                                            break;
                                                        }
                                                    }
                                                    if (j == system.numberOfSolutions
                                                        && system.numberOfSolutions < MAX_NUMBER_OF_SOLUTIONS)
                                                    {
                                                        *((__m256i*)system.solutions[system.numberOfSolutions].computorPublicKey) = *((__m256i*)transaction->sourcePublicKey);
                                                        *((__m256i*)system.solutions[system.numberOfSolutions].nonce) = *((__m256i*)(((unsigned char*)transaction) + sizeof(Transaction)));
                                                        solutionPublicationTicks[system.numberOfSolutions++] = -1;
                                                    }

                                                    RELEASE(solutionsLock);

                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    while (true)
                    {
                        criticalSituation = 1;
                    }
                }
            }
        }
    }

    contractProcessorPhase = END_TICK;
    contractProcessorState = 1;
    while (contractProcessorState)
    {
        _mm_pause();
    }

    unsigned int digestIndex;
    for (digestIndex = 0; digestIndex < SPECTRUM_CAPACITY; digestIndex++)
    {
        if (spectrum[digestIndex].latestIncomingTransferTick == system.tick || spectrum[digestIndex].latestOutgoingTransferTick == system.tick)
        {
            KangarooTwelve64To32((unsigned char*)&spectrum[digestIndex], (unsigned char*)&spectrumDigests[digestIndex]);
            spectrumChangeFlags[digestIndex >> 6] |= (1ULL << (digestIndex & 63));
        }
    }
    unsigned int previousLevelBeginning = 0;
    unsigned int numberOfLeafs = SPECTRUM_CAPACITY;
    while (numberOfLeafs > 1)
    {
        for (unsigned int i = 0; i < numberOfLeafs; i += 2)
        {
            if (spectrumChangeFlags[i >> 6] & (3ULL << (i & 63)))
            {
                KangarooTwelve64To32((unsigned char*)&spectrumDigests[previousLevelBeginning + i], (unsigned char*)&spectrumDigests[digestIndex]);
                spectrumChangeFlags[i >> 6] &= ~(3ULL << (i & 63));
                spectrumChangeFlags[i >> 7] |= (1ULL << ((i >> 1) & 63));
            }
            digestIndex++;
        }
        previousLevelBeginning += numberOfLeafs;
        numberOfLeafs >>= 1;
    }
    spectrumChangeFlags[0] = 0;

    *((__m256i*)etalonTick.saltedSpectrumDigest) = spectrumDigests[(SPECTRUM_CAPACITY * 2 - 1) - 1];
    getUniverseDigest((__m256i*)etalonTick.saltedUniverseDigest);
    getComputerDigest((__m256i*)etalonTick.saltedComputerDigest);

    for (unsigned int i = 0; i < numberOfOwnComputorIndices; i++)
    {
        if ((system.tick + TICK_TRANSACTIONS_PUBLICATION_OFFSET) % NUMBER_OF_COMPUTORS == ownComputorIndices[i])
        {
            if (system.tick > system.latestLedTick)
            {
                if (isMain)
                {
                    broadcastedFutureTickData.tickData.computorIndex = ownComputorIndices[i] ^ BROADCAST_FUTURE_TICK_DATA;
                    broadcastedFutureTickData.tickData.epoch = system.epoch;
                    broadcastedFutureTickData.tickData.tick = system.tick + TICK_TRANSACTIONS_PUBLICATION_OFFSET;

                    broadcastedFutureTickData.tickData.millisecond = 0;
                    broadcastedFutureTickData.tickData.second = time.Second;
                    broadcastedFutureTickData.tickData.minute = time.Minute;
                    broadcastedFutureTickData.tickData.hour = time.Hour;
                    broadcastedFutureTickData.tickData.day = time.Day;
                    broadcastedFutureTickData.tickData.month = time.Month;
                    broadcastedFutureTickData.tickData.year = time.Year - 2000;

                    if (system.proposals[ownComputorIndices[i]].uriSize)
                    {
                        bs->CopyMem(&broadcastedFutureTickData.tickData.varStruct.proposal, &system.proposals[ownComputorIndices[i]], sizeof(ComputorProposal));
                    }
                    else
                    {
                        bs->CopyMem(&broadcastedFutureTickData.tickData.varStruct.ballot, &system.ballots[ownComputorIndices[i]], sizeof(ComputorBallot));
                    }

                    unsigned char timelockPreimage[32 + 32 + 32];
                    *((__m256i*) & timelockPreimage[0]) = *((__m256i*)etalonTick.saltedSpectrumDigest);
                    *((__m256i*) & timelockPreimage[32]) = *((__m256i*)etalonTick.saltedUniverseDigest);
                    *((__m256i*) & timelockPreimage[64]) = *((__m256i*)etalonTick.saltedComputerDigest);
                    KangarooTwelve(timelockPreimage, sizeof(timelockPreimage), broadcastedFutureTickData.tickData.timelock, sizeof(broadcastedFutureTickData.tickData.timelock));

                    unsigned int numberOfEntityPendingTransactionIndices;
                    for (numberOfEntityPendingTransactionIndices = 0; numberOfEntityPendingTransactionIndices < SPECTRUM_CAPACITY; numberOfEntityPendingTransactionIndices++)
                    {
                        entityPendingTransactionIndices[numberOfEntityPendingTransactionIndices] = numberOfEntityPendingTransactionIndices;
                    }
                    unsigned int j = 0;
                    while (j < NUMBER_OF_TRANSACTIONS_PER_TICK && numberOfEntityPendingTransactionIndices)
                    {
                        const unsigned int index = random(numberOfEntityPendingTransactionIndices);

                        const Transaction* pendingTransaction = ((Transaction*)&entityPendingTransactions[entityPendingTransactionIndices[index] * MAX_TRANSACTION_SIZE]);
                        if (pendingTransaction->tick == system.tick + TICK_TRANSACTIONS_PUBLICATION_OFFSET)
                        {
                            const unsigned int transactionSize = sizeof(Transaction) + pendingTransaction->inputSize + SIGNATURE_SIZE;
                            if (nextTickTransactionOffset + transactionSize <= FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS))
                            {
                                ACQUIRE(tickTransactionsLock);
                                if (nextTickTransactionOffset + transactionSize <= FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS))
                                {
                                    tickTransactionOffsets[pendingTransaction->tick - system.initialTick][j] = nextTickTransactionOffset;
                                    bs->CopyMem(&tickTransactions[nextTickTransactionOffset], (void*)pendingTransaction, transactionSize);
                                    *((__m256i*)broadcastedFutureTickData.tickData.transactionDigests[j]) = *((__m256i*) & entityPendingTransactionDigests[entityPendingTransactionIndices[index] * 32ULL]);
                                    j++;
                                    nextTickTransactionOffset += transactionSize;
                                }
                                RELEASE(tickTransactionsLock);
                            }
                        }

                        entityPendingTransactionIndices[index] = entityPendingTransactionIndices[--numberOfEntityPendingTransactionIndices];
                    }
                    for (; j < NUMBER_OF_TRANSACTIONS_PER_TICK; j++)
                    {
                        *((__m256i*)broadcastedFutureTickData.tickData.transactionDigests[j]) = _mm256_setzero_si256();
                    }

                    bs->SetMem(broadcastedFutureTickData.tickData.contractFees, sizeof(broadcastedFutureTickData.tickData.contractFees), 0);

                    unsigned char digest[32];
                    KangarooTwelve((unsigned char*)&broadcastedFutureTickData.tickData, sizeof(TickData) - SIGNATURE_SIZE, digest, sizeof(digest));
                    broadcastedFutureTickData.tickData.computorIndex ^= BROADCAST_FUTURE_TICK_DATA;
                    sign(computorSubseeds[ownComputorIndicesMapping[i]], computorPublicKeys[ownComputorIndicesMapping[i]], digest, broadcastedFutureTickData.tickData.signature);

                    enqueueResponse(NULL, sizeof(broadcastedFutureTickData), BROADCAST_FUTURE_TICK_DATA, 0, &broadcastedFutureTickData);
                }

                system.latestLedTick = system.tick;
            }

            break;
        }
    }

    if (isMain)
    {
        for (unsigned int i = 0; i < sizeof(computorSeeds) / sizeof(computorSeeds[0]); i++)
        {
            int solutionIndexToPublish = -1;

            unsigned int j;
            for (j = 0; j < system.numberOfSolutions; j++)
            {
                if (solutionPublicationTicks[j] > 0
                    && EQUAL(*((__m256i*)system.solutions[j].computorPublicKey), *((__m256i*)computorPublicKeys[i])))
                {
                    if (solutionPublicationTicks[j] <= (int)system.tick)
                    {
                        solutionIndexToPublish = j;
                    }

                    break;
                }
            }
            if (j == system.numberOfSolutions)
            {
                for (j = 0; j < system.numberOfSolutions; j++)
                {
                    if (!solutionPublicationTicks[j]
                        && EQUAL(*((__m256i*)system.solutions[j].computorPublicKey), *((__m256i*)computorPublicKeys[i])))
                    {
                        solutionIndexToPublish = j;

                        break;
                    }
                }
            }

            if (solutionIndexToPublish >= 0)
            {
                struct
                {
                    Transaction transaction;
                    unsigned char nonce[32];
                    unsigned char signature[SIGNATURE_SIZE];
                } payload;
                *((__m256i*)payload.transaction.sourcePublicKey) = *((__m256i*)computorPublicKeys[i]);
                *((__m256i*)payload.transaction.destinationPublicKey) = arbitratorPublicKey;
                payload.transaction.amount = 0;
                unsigned int random;
                _rdrand32_step(&random);
                solutionPublicationTicks[solutionIndexToPublish] = payload.transaction.tick = system.tick + MIN_MINING_SOLUTIONS_PUBLICATION_OFFSET + random % MIN_MINING_SOLUTIONS_PUBLICATION_OFFSET;
                payload.transaction.inputType = 0;
                payload.transaction.inputSize = sizeof(payload.nonce);
                *((__m256i*)payload.nonce) = *((__m256i*)system.solutions[solutionIndexToPublish].nonce);

                unsigned char digest[32];
                KangarooTwelve((unsigned char*)&payload.transaction, sizeof(payload.transaction) + sizeof(payload.nonce), digest, sizeof(digest));
                sign(computorSubseeds[i], computorPublicKeys[i], digest, payload.signature);

                enqueueResponse(NULL, sizeof(payload), BROADCAST_TRANSACTION, 0, &payload);
            }
        }
    }
}

static void endEpoch()
{
    contractProcessorPhase = END_EPOCH;
    contractProcessorState = 1;
    while (contractProcessorState)
    {
        _mm_pause();
    }

    Contract0State* contract0State = (Contract0State*)contractStates[0];
    for (unsigned int contractIndex = 1; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
    {
        if (system.epoch < contractDescriptions[contractIndex].constructionEpoch)
        {
            IPO* ipo = (IPO*)contractStates[contractIndex];
            const long long finalPrice = ipo->prices[NUMBER_OF_COMPUTORS - 1];
            int issuanceIndex, ownershipIndex, possessionIndex;
            if (finalPrice)
            {
                __m256i zero = _mm256_setzero_si256();
                issueAsset((unsigned char*)&zero, (char*)contractDescriptions[contractIndex].assetName, 0, CONTRACT_ASSET_UNIT_OF_MEASUREMENT, NUMBER_OF_COMPUTORS, &issuanceIndex, &ownershipIndex, &possessionIndex);
            }
            numberOfReleasedEntities = 0;
            for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
            {
                if (ipo->prices[i] > finalPrice)
                {
                    unsigned int j;
                    for (j = 0; j < numberOfReleasedEntities; j++)
                    {
                        if (EQUAL(*((__m256i*)ipo->publicKeys[i]), *((__m256i*)releasedPublicKeys[j])))
                        {
                            break;
                        }
                    }
                    if (j == numberOfReleasedEntities)
                    {
                        *((__m256i*)releasedPublicKeys[numberOfReleasedEntities]) = *((__m256i*)ipo->publicKeys[i]);
                        releasedAmounts[numberOfReleasedEntities++] = ipo->prices[i] - finalPrice;
                    }
                    else
                    {
                        releasedAmounts[j] += (ipo->prices[i] - finalPrice);
                    }
                }
                if (finalPrice)
                {
                    int destinationOwnershipIndex, destinationPossessionIndex;
                    transferAssetOwnershipAndPossession(ownershipIndex, possessionIndex, ipo->publicKeys[i], 1, &destinationOwnershipIndex, &destinationPossessionIndex);
                }
            }
            for (unsigned int i = 0; i < numberOfReleasedEntities; i++)
            {
                increaseEnergy(releasedPublicKeys[i], releasedAmounts[i]);
            }

            contract0State->contractFeeReserves[contractIndex] = finalPrice * NUMBER_OF_COMPUTORS;
        }
    }

    system.initialMillisecond = etalonTick.millisecond;
    system.initialSecond = etalonTick.second;
    system.initialMinute = etalonTick.minute;
    system.initialHour = etalonTick.hour;
    system.initialDay = etalonTick.day;
    system.initialMonth = etalonTick.month;
    system.initialYear = etalonTick.year;

    long long arbitratorRevenue = ISSUANCE_RATE;

    unsigned long long transactionCounters[NUMBER_OF_COMPUTORS];
    bs->SetMem(transactionCounters, sizeof(transactionCounters), 0);
    for (unsigned int tick = system.initialTick; tick <= system.tick; tick++)
    {
        ACQUIRE(tickDataLock);
        if (tickData[tick - system.initialTick].epoch == system.epoch)
        {
            unsigned int numberOfTransactions = 0;
            for (unsigned int transactionIndex = 0; transactionIndex < NUMBER_OF_TRANSACTIONS_PER_TICK; transactionIndex++)
            {
                if (!EQUAL(*((__m256i*)tickData[tick - system.initialTick].transactionDigests[transactionIndex]), _mm256_setzero_si256()))
                {
                    numberOfTransactions++;
                }
            }
            transactionCounters[tick % NUMBER_OF_COMPUTORS] += revenuePoints[numberOfTransactions];
        }
        RELEASE(tickDataLock);
    }
    unsigned long long sortedTransactionCounters[QUORUM + 1];
    bs->SetMem(sortedTransactionCounters, sizeof(sortedTransactionCounters), 0);
    for (unsigned short computorIndex = 0; computorIndex < NUMBER_OF_COMPUTORS; computorIndex++)
    {
        sortedTransactionCounters[QUORUM] = transactionCounters[computorIndex];
        unsigned int i = QUORUM;
        while (i
            && sortedTransactionCounters[i - 1] < sortedTransactionCounters[i])
        {
            const unsigned long long tmp = sortedTransactionCounters[i - 1];
            sortedTransactionCounters[i - 1] = sortedTransactionCounters[i];
            sortedTransactionCounters[i--] = tmp;
        }
    }
    if (!sortedTransactionCounters[QUORUM - 1])
    {
        sortedTransactionCounters[QUORUM - 1] = 1;
    }
    for (unsigned int computorIndex = 0; computorIndex < NUMBER_OF_COMPUTORS; computorIndex++)
    {
        const unsigned int revenue = (transactionCounters[computorIndex] >= sortedTransactionCounters[QUORUM - 1]) ? (ISSUANCE_RATE / NUMBER_OF_COMPUTORS) : (((ISSUANCE_RATE / NUMBER_OF_COMPUTORS) * ((unsigned long long)transactionCounters[computorIndex])) / sortedTransactionCounters[QUORUM - 1]);
        increaseEnergy(broadcastedComputors.broadcastComputors.computors.publicKeys[computorIndex], revenue);
        arbitratorRevenue -= revenue;
    }

    increaseEnergy((unsigned char*)&arbitratorPublicKey, arbitratorRevenue);

    {
        ACQUIRE(spectrumLock);

        ::Entity* reorgSpectrum = (::Entity*)reorgBuffer;
        bs->SetMem(reorgSpectrum, SPECTRUM_CAPACITY * sizeof(::Entity), 0);
        for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
        {
            if (spectrum[i].incomingAmount - spectrum[i].outgoingAmount)
            {
                unsigned int index = (*((unsigned int*)spectrum[i].publicKey)) & (SPECTRUM_CAPACITY - 1);

            iteration:
                if (EQUAL(*((__m256i*)reorgSpectrum[index].publicKey), _mm256_setzero_si256()))
                {
                    bs->CopyMem(&reorgSpectrum[index], &spectrum[i], sizeof(::Entity));
                }
                else
                {
                    index = (index + 1) & (SPECTRUM_CAPACITY - 1);

                    goto iteration;
                }
            }
        }
        bs->CopyMem(spectrum, reorgSpectrum, SPECTRUM_CAPACITY * sizeof(::Entity));

        unsigned int digestIndex;
        for (digestIndex = 0; digestIndex < SPECTRUM_CAPACITY; digestIndex++)
        {
            KangarooTwelve64To32((unsigned char*)&spectrum[digestIndex], (unsigned char*)&spectrumDigests[digestIndex]);
        }
        unsigned int previousLevelBeginning = 0;
        unsigned int numberOfLeafs = SPECTRUM_CAPACITY;
        while (numberOfLeafs > 1)
        {
            for (unsigned int i = 0; i < numberOfLeafs; i += 2)
            {
                KangarooTwelve64To32((unsigned char*)&spectrumDigests[previousLevelBeginning + i], (unsigned char*)&spectrumDigests[digestIndex++]);
            }

            previousLevelBeginning += numberOfLeafs;
            numberOfLeafs >>= 1;
        }

        numberOfEntities = 0;
        for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
        {
            if (spectrum[i].incomingAmount - spectrum[i].outgoingAmount)
            {
                numberOfEntities++;
            }
        }

        RELEASE(spectrumLock);
    }

    {
        ACQUIRE(universeLock);

        Asset* reorgAssets = (Asset*)reorgBuffer;
        bs->SetMem(reorgAssets, ASSETS_CAPACITY * sizeof(Asset), 0);
        for (unsigned int i = 0; i < ASSETS_CAPACITY; i++)
        {
            if (assets[i].varStruct.possession.type == POSSESSION
                && assets[i].varStruct.possession.numberOfUnits > 0)
            {
                const unsigned int oldOwnershipIndex = assets[i].varStruct.possession.ownershipIndex;
                const unsigned int oldIssuanceIndex = assets[oldOwnershipIndex].varStruct.ownership.issuanceIndex;
                unsigned char* issuerPublicKey = assets[oldIssuanceIndex].varStruct.issuance.publicKey;
                char* name = assets[oldIssuanceIndex].varStruct.issuance.name;
                int issuanceIndex = (*((unsigned int*)issuerPublicKey)) & (ASSETS_CAPACITY - 1);
            iteration2:
                if (reorgAssets[issuanceIndex].varStruct.issuance.type == EMPTY
                    || (reorgAssets[issuanceIndex].varStruct.issuance.type == ISSUANCE
                        && ((*((unsigned long long*)reorgAssets[issuanceIndex].varStruct.issuance.name)) & 0xFFFFFFFFFFFFFF) == ((*((unsigned long long*)name)) & 0xFFFFFFFFFFFFFF)
                        && EQUAL(*((__m256i*)reorgAssets[issuanceIndex].varStruct.issuance.publicKey), *((__m256i*)issuerPublicKey))))
                {
                    if (reorgAssets[issuanceIndex].varStruct.issuance.type == EMPTY)
                    {
                        bs->CopyMem(&reorgAssets[issuanceIndex], &assets[oldIssuanceIndex], sizeof(Asset));
                    }

                    unsigned char* ownerPublicKey = assets[oldOwnershipIndex].varStruct.ownership.publicKey;
                    int ownershipIndex = (*((unsigned int*)ownerPublicKey)) & (ASSETS_CAPACITY - 1);
                iteration3:
                    if (reorgAssets[ownershipIndex].varStruct.ownership.type == EMPTY
                        || (reorgAssets[ownershipIndex].varStruct.ownership.type == OWNERSHIP
                            && reorgAssets[ownershipIndex].varStruct.ownership.managingContractIndex == assets[oldOwnershipIndex].varStruct.ownership.managingContractIndex
                            && reorgAssets[ownershipIndex].varStruct.ownership.issuanceIndex == issuanceIndex
                            && EQUAL(*((__m256i*)reorgAssets[ownershipIndex].varStruct.ownership.publicKey), *((__m256i*)ownerPublicKey))))
                    {
                        if (reorgAssets[ownershipIndex].varStruct.ownership.type == EMPTY)
                        {
                            *((__m256i*)reorgAssets[ownershipIndex].varStruct.ownership.publicKey) = *((__m256i*)ownerPublicKey);
                            reorgAssets[ownershipIndex].varStruct.ownership.type = OWNERSHIP;
                            reorgAssets[ownershipIndex].varStruct.ownership.managingContractIndex = assets[oldOwnershipIndex].varStruct.ownership.managingContractIndex;
                            reorgAssets[ownershipIndex].varStruct.ownership.issuanceIndex = issuanceIndex;
                        }
                        reorgAssets[ownershipIndex].varStruct.ownership.numberOfUnits += assets[i].varStruct.possession.numberOfUnits;

                        int possessionIndex = (*((unsigned int*)assets[i].varStruct.possession.publicKey)) & (ASSETS_CAPACITY - 1);
                    iteration4:
                        if (reorgAssets[possessionIndex].varStruct.possession.type == EMPTY
                            || (reorgAssets[possessionIndex].varStruct.possession.type == POSSESSION
                                && reorgAssets[possessionIndex].varStruct.possession.managingContractIndex == assets[i].varStruct.possession.managingContractIndex
                                && reorgAssets[possessionIndex].varStruct.possession.ownershipIndex == ownershipIndex
                                && EQUAL(*((__m256i*)reorgAssets[possessionIndex].varStruct.possession.publicKey), *((__m256i*)assets[i].varStruct.possession.publicKey))))
                        {
                            if (reorgAssets[possessionIndex].varStruct.possession.type == EMPTY)
                            {
                                *((__m256i*)reorgAssets[possessionIndex].varStruct.possession.publicKey) = *((__m256i*)assets[i].varStruct.possession.publicKey);
                                reorgAssets[possessionIndex].varStruct.possession.type = POSSESSION;
                                reorgAssets[possessionIndex].varStruct.possession.managingContractIndex = assets[i].varStruct.possession.managingContractIndex;
                                reorgAssets[possessionIndex].varStruct.possession.ownershipIndex = ownershipIndex;
                            }
                            reorgAssets[possessionIndex].varStruct.possession.numberOfUnits += assets[i].varStruct.possession.numberOfUnits;
                        }
                        else
                        {
                            possessionIndex = (possessionIndex + 1) & (ASSETS_CAPACITY - 1);

                            goto iteration4;
                        }
                    }
                    else
                    {
                        ownershipIndex = (ownershipIndex + 1) & (ASSETS_CAPACITY - 1);

                        goto iteration3;
                    }
                }
                else
                {
                    issuanceIndex = (issuanceIndex + 1) & (ASSETS_CAPACITY - 1);

                    goto iteration2;
                }
            }
        }
        bs->CopyMem(assets, reorgAssets, ASSETS_CAPACITY * sizeof(Asset));

        bs->SetMem(assetChangeFlags, ASSETS_CAPACITY / 8, 0xFF);

        RELEASE(universeLock);
    }

    system.epoch++;
    system.initialTick = system.tick;
    systemMustBeSaved = true;

    SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
    SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
    SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
    spectrumMustBeSaved = true;

    UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
    UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
    UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
    universeMustBeSaved = true;

    CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
    CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
    CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
    computerMustBeSaved = true;

    broadcastedComputors.broadcastComputors.computors.epoch = 0;
    for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
    {
        _rdrand64_step((unsigned long long*) & broadcastedComputors.broadcastComputors.computors.publicKeys[i][0]);
        _rdrand64_step((unsigned long long*) & broadcastedComputors.broadcastComputors.computors.publicKeys[i][8]);
        _rdrand64_step((unsigned long long*) & broadcastedComputors.broadcastComputors.computors.publicKeys[i][16]);
        _rdrand64_step((unsigned long long*) & broadcastedComputors.broadcastComputors.computors.publicKeys[i][24]);
    }
    bs->SetMem(&broadcastedComputors.broadcastComputors.computors.signature, sizeof(broadcastedComputors.broadcastComputors.computors.signature), 0);

    numberOfOwnComputorIndices = 0;
}

static void tickProcessor(void*)
{
    enableAVX();

    unsigned long long processorNumber;
    mpServicesProtocol->WhoAmI(mpServicesProtocol, &processorNumber);

    unsigned int latestProcessedTick = 0;
    while (!state)
    {
        const unsigned long long curTimeTick = __rdtsc();

        if (broadcastedComputors.broadcastComputors.computors.epoch == system.epoch)
        {
            {
                const unsigned int baseOffset = (system.tick + 1 - system.initialTick) * NUMBER_OF_COMPUTORS;
                unsigned int futureTickTotalNumberOfComputors = 0;
                for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                {
                    if (ticks[baseOffset + i].epoch == system.epoch)
                    {
                        futureTickTotalNumberOfComputors++;
                    }
                }
                ::futureTickTotalNumberOfComputors = futureTickTotalNumberOfComputors;
            }

            if (system.tick - system.initialTick < MAX_NUMBER_OF_TICKS_PER_EPOCH - 1)
            {
                if (system.tick > latestProcessedTick)
                {
                    processTick(processorNumber);

                    latestProcessedTick = system.tick;
                }

                if (futureTickTotalNumberOfComputors > NUMBER_OF_COMPUTORS - QUORUM)
                {
                    const unsigned int baseOffset = (system.tick + 1 - system.initialTick) * NUMBER_OF_COMPUTORS;
                    unsigned int numberOfEmptyNextTickTransactionDigest = 0;
                    unsigned int numberOfUniqueNextTickTransactionDigests = 0;
                    for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                    {
                        if (ticks[baseOffset + i].epoch == system.epoch)
                        {
                            unsigned int j;
                            for (j = 0; j < numberOfUniqueNextTickTransactionDigests; j++)
                            {
                                if (EQUAL(*((__m256i*)ticks[baseOffset + i].transactionDigest), uniqueNextTickTransactionDigests[j]))
                                {
                                    break;
                                }
                            }
                            if (j == numberOfUniqueNextTickTransactionDigests)
                            {
                                uniqueNextTickTransactionDigests[numberOfUniqueNextTickTransactionDigests] = *((__m256i*)ticks[baseOffset + i].transactionDigest);
                                uniqueNextTickTransactionDigestCounters[numberOfUniqueNextTickTransactionDigests++] = 1;
                            }
                            else
                            {
                                uniqueNextTickTransactionDigestCounters[j]++;
                            }

                            if (EQUAL(*((__m256i*)ticks[baseOffset + i].transactionDigest), _mm256_setzero_si256()))
                            {
                                numberOfEmptyNextTickTransactionDigest++;
                            }
                        }
                    }
                    unsigned int mostPopularUniqueNextTickTransactionDigestIndex = 0, totalUniqueNextTickTransactionDigestCounter = uniqueNextTickTransactionDigestCounters[0];
                    for (unsigned int i = 1; i < numberOfUniqueNextTickTransactionDigests; i++)
                    {
                        if (uniqueNextTickTransactionDigestCounters[i] > uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex])
                        {
                            mostPopularUniqueNextTickTransactionDigestIndex = i;
                        }
                        totalUniqueNextTickTransactionDigestCounter += uniqueNextTickTransactionDigestCounters[i];
                    }
                    if (uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex] >= QUORUM)
                    {
                        targetNextTickDataDigest = uniqueNextTickTransactionDigests[mostPopularUniqueNextTickTransactionDigestIndex];
                        targetNextTickDataDigestIsKnown = true;
                        testFlags |= 1024;
                    }
                    else
                    {
                        if (numberOfEmptyNextTickTransactionDigest > NUMBER_OF_COMPUTORS - QUORUM
                            || uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex] + (NUMBER_OF_COMPUTORS - totalUniqueNextTickTransactionDigestCounter) < QUORUM)
                        {
                            targetNextTickDataDigest = _mm256_setzero_si256();
                            targetNextTickDataDigestIsKnown = true;
                            testFlags |= 2048;
                        }
                    }
                }

                if (!targetNextTickDataDigestIsKnown)
                {
                    const unsigned int baseOffset = (system.tick - system.initialTick) * NUMBER_OF_COMPUTORS;
                    unsigned int numberOfEmptyNextTickTransactionDigest = 0;
                    unsigned int numberOfUniqueNextTickTransactionDigests = 0;
                    for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                    {
                        if (ticks[baseOffset + i].epoch == system.epoch)
                        {
                            unsigned int j;
                            for (j = 0; j < numberOfUniqueNextTickTransactionDigests; j++)
                            {
                                if (EQUAL(*((__m256i*)ticks[baseOffset + i].expectedNextTickTransactionDigest), uniqueNextTickTransactionDigests[j]))
                                {
                                    break;
                                }
                            }
                            if (j == numberOfUniqueNextTickTransactionDigests)
                            {
                                uniqueNextTickTransactionDigests[numberOfUniqueNextTickTransactionDigests] = *((__m256i*)ticks[baseOffset + i].expectedNextTickTransactionDigest);
                                uniqueNextTickTransactionDigestCounters[numberOfUniqueNextTickTransactionDigests++] = 1;
                            }
                            else
                            {
                                uniqueNextTickTransactionDigestCounters[j]++;
                            }

                            if (EQUAL(*((__m256i*)ticks[baseOffset + i].expectedNextTickTransactionDigest), _mm256_setzero_si256()))
                            {
                                numberOfEmptyNextTickTransactionDigest++;
                            }
                        }
                    }
                    if (numberOfUniqueNextTickTransactionDigests)
                    {
                        unsigned int mostPopularUniqueNextTickTransactionDigestIndex = 0, totalUniqueNextTickTransactionDigestCounter = uniqueNextTickTransactionDigestCounters[0];
                        for (unsigned int i = 1; i < numberOfUniqueNextTickTransactionDigests; i++)
                        {
                            if (uniqueNextTickTransactionDigestCounters[i] > uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex])
                            {
                                mostPopularUniqueNextTickTransactionDigestIndex = i;
                            }
                            totalUniqueNextTickTransactionDigestCounter += uniqueNextTickTransactionDigestCounters[i];
                        }
                        if (uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex] >= QUORUM)
                        {
                            targetNextTickDataDigest = uniqueNextTickTransactionDigests[mostPopularUniqueNextTickTransactionDigestIndex];
                            targetNextTickDataDigestIsKnown = true;
                            testFlags |= 4096;
                        }
                        else
                        {
                            if (numberOfEmptyNextTickTransactionDigest > NUMBER_OF_COMPUTORS - QUORUM
                                || uniqueNextTickTransactionDigestCounters[mostPopularUniqueNextTickTransactionDigestIndex] + (NUMBER_OF_COMPUTORS - totalUniqueNextTickTransactionDigestCounter) < QUORUM)
                            {
                                targetNextTickDataDigest = _mm256_setzero_si256();
                                targetNextTickDataDigestIsKnown = true;
                                testFlags |= 8192;
                            }
                        }
                    }
                }

                ACQUIRE(tickDataLock);
                bs->CopyMem(&nextTickData, &tickData[system.tick + 1 - system.initialTick], sizeof(TickData));
                RELEASE(tickDataLock);
                if (nextTickData.epoch == system.epoch)
                {
                    unsigned char timelockPreimage[32 + 32 + 32];
                    *((__m256i*)&timelockPreimage[0]) = *((__m256i*)etalonTick.prevSpectrumDigest);
                    *((__m256i*)&timelockPreimage[32]) = *((__m256i*)etalonTick.prevUniverseDigest);
                    *((__m256i*)&timelockPreimage[64]) = *((__m256i*)etalonTick.prevComputerDigest);
                    __m256i timelock;
                    KangarooTwelve(timelockPreimage, sizeof(timelockPreimage), (unsigned char*)&timelock, sizeof(timelock));
                    if (!EQUAL(*((__m256i*)nextTickData.timelock), timelock))
                    {
                        ACQUIRE(tickDataLock);
                        tickData[system.tick + 1 - system.initialTick].epoch = 0;
                        RELEASE(tickDataLock);
                        nextTickData.epoch = 0;
                    }
                }

                bool tickDataSuits;
                if (!targetNextTickDataDigestIsKnown)
                {
                    if (nextTickData.epoch != system.epoch
                        && futureTickTotalNumberOfComputors <= NUMBER_OF_COMPUTORS - QUORUM
                        && __rdtsc() - tickTicks[sizeof(tickTicks) / sizeof(tickTicks[0]) - 1] < TARGET_TICK_DURATION * frequency / 1000)
                    {
                        tickDataSuits = false;
                    }
                    else
                    {
                        tickDataSuits = true;
                    }
                }
                else
                {
                    if (EQUAL(targetNextTickDataDigest, _mm256_setzero_si256()))
                    {
                        ACQUIRE(tickDataLock);
                        tickData[system.tick + 1 - system.initialTick].epoch = 0;
                        RELEASE(tickDataLock);
                        nextTickData.epoch = 0;
                        tickDataSuits = true;
                    }
                    else
                    {
                        if (nextTickData.epoch != system.epoch)
                        {
                            tickDataSuits = false;
                        }
                        else
                        {
                            testFlags |= 1048576;
                            KangarooTwelve((unsigned char*)&nextTickData, sizeof(TickData), etalonTick.expectedNextTickTransactionDigest, 32);
                            tickDataSuits = EQUAL(*((__m256i*)etalonTick.expectedNextTickTransactionDigest), targetNextTickDataDigest);
                            if (!tickDataSuits)
                            {
                                testFlags |= 1;
                            }
                        }
                    }
                }
                if (!tickDataSuits)
                {
                    unsigned int tickTotalNumberOfComputors = 0;
                    const unsigned int baseOffset = (system.tick - system.initialTick) * NUMBER_OF_COMPUTORS;
                    for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                    {
                        ACQUIRE(tickLocks[i]);

                        if (ticks[baseOffset + i].epoch == system.epoch)
                        {
                            tickTotalNumberOfComputors++;
                        }

                        RELEASE(tickLocks[i]);
                    }
                    ::tickNumberOfComputors = 0;
                    ::tickTotalNumberOfComputors = tickTotalNumberOfComputors;
                    if (testFlags & 1) testFlags |= 512;
                }
                else
                {
                    if (tickPhase < 2)
                    {
                        tickPhase = 2;
                    }

                    numberOfNextTickTransactions = 0;
                    numberOfKnownNextTickTransactions = 0;

                    if (nextTickData.epoch == system.epoch)
                    {
                        nextTickTransactionsSemaphore = 1;
                        bs->SetMem(requestedTickTransactions.requestedTickTransactions.transactionFlags, sizeof(requestedTickTransactions.requestedTickTransactions.transactionFlags), 0);
                        unsigned long long unknownTransactions[NUMBER_OF_TRANSACTIONS_PER_TICK / 64];
                        bs->SetMem(unknownTransactions, sizeof(unknownTransactions), 0);
                        for (unsigned int i = 0; i < NUMBER_OF_TRANSACTIONS_PER_TICK; i++)
                        {
                            if (!EQUAL(*((__m256i*)nextTickData.transactionDigests[i]), _mm256_setzero_si256()))
                            {
                                numberOfNextTickTransactions++;

                                ACQUIRE(tickTransactionsLock);
                                if (tickTransactionOffsets[system.tick + 1 - system.initialTick][i])
                                {
                                    const Transaction* transaction = (Transaction*)&tickTransactions[tickTransactionOffsets[system.tick + 1 - system.initialTick][i]];
                                    unsigned char digest[32];
                                    KangarooTwelve((unsigned char*)transaction, sizeof(Transaction) + transaction->inputSize + SIGNATURE_SIZE, digest, sizeof(digest));
                                    if (EQUAL(*((__m256i*)digest), *((__m256i*)nextTickData.transactionDigests[i])))
                                    {
                                        numberOfKnownNextTickTransactions++;
                                    }
                                    else
                                    {
                                        unknownTransactions[i >> 6] |= (1ULL << (i & 63));
                                    }
                                }
                                RELEASE(tickTransactionsLock);
                            }
                        }
                        if (numberOfKnownNextTickTransactions != numberOfNextTickTransactions)
                        {
                            const unsigned int nextTick = system.tick + 1;
                            for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
                            {
                                Transaction* pendingTransaction = (Transaction*)&entityPendingTransactions[i * MAX_TRANSACTION_SIZE];
                                if (pendingTransaction->tick == nextTick)
                                {
                                    ACQUIRE(entityPendingTransactionsLock);

                                    for (unsigned int j = 0; j < NUMBER_OF_TRANSACTIONS_PER_TICK; j++)
                                    {
                                        if (unknownTransactions[j >> 6] & (1ULL << (j & 63)))
                                        {
                                            if (EQUAL(*((__m256i*) & entityPendingTransactionDigests[i * 32ULL]), *((__m256i*)nextTickData.transactionDigests[j])))
                                            {
                                                unsigned char transactionBuffer[MAX_TRANSACTION_SIZE];
                                                const unsigned int transactionSize = sizeof(Transaction) + pendingTransaction->inputSize + SIGNATURE_SIZE;
                                                bs->CopyMem(transactionBuffer, (void*)pendingTransaction, transactionSize);

                                                pendingTransaction = (Transaction*)transactionBuffer;
                                                ACQUIRE(tickTransactionsLock);
                                                if (!tickTransactionOffsets[pendingTransaction->tick - system.initialTick][j])
                                                {
                                                    if (nextTickTransactionOffset + transactionSize <= FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS))
                                                    {
                                                        tickTransactionOffsets[pendingTransaction->tick - system.initialTick][j] = nextTickTransactionOffset;
                                                        bs->CopyMem(&tickTransactions[nextTickTransactionOffset], pendingTransaction, transactionSize);
                                                        nextTickTransactionOffset += transactionSize;
                                                    }
                                                }
                                                RELEASE(tickTransactionsLock);

                                                numberOfKnownNextTickTransactions++;
                                                unknownTransactions[j >> 6] &= ~(1ULL << (j & 63));
                                            }
                                        }
                                    }

                                    RELEASE(entityPendingTransactionsLock);
                                }
                            }

                            for (unsigned int i = 0; i < NUMBER_OF_TRANSACTIONS_PER_TICK; i++)
                            {
                                if (!(unknownTransactions[i >> 6] & (1ULL << (i & 63))))
                                {
                                    requestedTickTransactions.requestedTickTransactions.transactionFlags[i >> 3] |= (1 << (i & 7));
                                }
                            }
                        }
                        nextTickTransactionsSemaphore = 0;
                    }

                    if (numberOfKnownNextTickTransactions != numberOfNextTickTransactions)
                    {
                        if (!targetNextTickDataDigestIsKnown
                            && __rdtsc() - tickTicks[sizeof(tickTicks) / sizeof(tickTicks[0]) - 1] > TARGET_TICK_DURATION * 5 * frequency / 1000)
                        {
                            ACQUIRE(tickDataLock);
                            tickData[system.tick + 1 - system.initialTick].epoch = 0;
                            RELEASE(tickDataLock);
                            nextTickData.epoch = 0;

                            numberOfNextTickTransactions = 0;
                            numberOfKnownNextTickTransactions = 0;
                        }
                    }

                    if (numberOfKnownNextTickTransactions != numberOfNextTickTransactions)
                    {
                        requestedTickTransactions.requestedTickTransactions.tick = system.tick + 1;
                    }
                    else
                    {
                        requestedTickTransactions.requestedTickTransactions.tick = 0;

                        if (tickData[system.tick - system.initialTick].epoch == system.epoch)
                        {
                            KangarooTwelve((unsigned char*)&tickData[system.tick - system.initialTick], sizeof(TickData), etalonTick.transactionDigest, 32);
                        }
                        else
                        {
                            *((__m256i*)etalonTick.transactionDigest) = _mm256_setzero_si256();
                        }

                        if (nextTickData.epoch == system.epoch)
                        {
                            if (!targetNextTickDataDigestIsKnown)
                            {
                                testFlags |= 1048576*2;
                                KangarooTwelve((unsigned char*)&nextTickData, sizeof(TickData), etalonTick.expectedNextTickTransactionDigest, 32);
                            }
                        }
                        else
                        {
                            testFlags |= 1048576*4;
                            *((__m256i*)etalonTick.expectedNextTickTransactionDigest) = _mm256_setzero_si256();
                        }

                        if (system.tick > system.latestCreatedTick || system.tick == system.initialTick)
                        {
#if !IGNORE_RESOURCE_TESTING
                            if (isMain)
                            {
                                BroadcastTick broadcastTick;
                                bs->CopyMem(&broadcastTick.tick, &etalonTick, sizeof(Tick));
                                for (unsigned int i = 0; i < numberOfOwnComputorIndices; i++)
                                {
                                    broadcastTick.tick.computorIndex = ownComputorIndices[i] ^ BROADCAST_TICK;
                                    unsigned char saltedData[32 + 32];
                                    *((__m256i*) & saltedData[0]) = *((__m256i*)computorPublicKeys[ownComputorIndicesMapping[i]]);
                                    *((unsigned long long*)&saltedData[32]) = resourceTestingDigest;
                                    KangarooTwelve(saltedData, 32 + sizeof(resourceTestingDigest), (unsigned char*)&broadcastTick.tick.saltedResourceTestingDigest, sizeof(broadcastTick.tick.saltedResourceTestingDigest));
                                    *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedSpectrumDigest);
                                    KangarooTwelve64To32(saltedData, broadcastTick.tick.saltedSpectrumDigest);
                                    *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedUniverseDigest);
                                    KangarooTwelve64To32(saltedData, broadcastTick.tick.saltedUniverseDigest);
                                    *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedComputerDigest);
                                    KangarooTwelve64To32(saltedData, broadcastTick.tick.saltedComputerDigest);

                                    unsigned char digest[32];
                                    KangarooTwelve((unsigned char*)&broadcastTick.tick, sizeof(Tick) - SIGNATURE_SIZE, digest, sizeof(digest));
                                    broadcastTick.tick.computorIndex ^= BROADCAST_TICK;
                                    sign(computorSubseeds[ownComputorIndicesMapping[i]], computorPublicKeys[ownComputorIndicesMapping[i]], digest, broadcastTick.tick.signature);

                                    enqueueResponse(NULL, sizeof(broadcastTick), BROADCAST_TICK, 0, &broadcastTick);
                                }
                            }
#endif

                            if (system.tick != system.initialTick)
                            {
                                system.latestCreatedTick = system.tick;
                            }
                        }

                        TickEssence tickEssence;
                        __m256i etalonTickEssenceDigest;

                        *((unsigned long long*) & tickEssence.millisecond) = *((unsigned long long*) & etalonTick.millisecond);
                        *((__m256i*)tickEssence.prevSpectrumDigest) = *((__m256i*)etalonTick.prevSpectrumDigest);
                        *((__m256i*)tickEssence.prevUniverseDigest) = *((__m256i*)etalonTick.prevUniverseDigest);
                        *((__m256i*)tickEssence.prevComputerDigest) = *((__m256i*)etalonTick.prevComputerDigest);
                        *((__m256i*)tickEssence.transactionDigest) = *((__m256i*)etalonTick.transactionDigest);
                        KangarooTwelve((unsigned char*)&tickEssence, sizeof(TickEssence), (unsigned char*)&etalonTickEssenceDigest, 32);

                        const unsigned int baseOffset = (system.tick - system.initialTick) * NUMBER_OF_COMPUTORS;

                        unsigned int tickNumberOfComputors = 0, tickTotalNumberOfComputors = 0;
                        for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                        {
                            ACQUIRE(tickLocks[i]);

                            const Tick* tick = &ticks[baseOffset + i];
                            if (tick->epoch == system.epoch)
                            {
                                tickTotalNumberOfComputors++;

                                unsigned char saltedData[32 + 32];
                                unsigned char saltedDigest[32];
                                *((__m256i*) & saltedData[0]) = *((__m256i*)broadcastedComputors.broadcastComputors.computors.publicKeys[tick->computorIndex]);
#if !IGNORE_RESOURCE_TESTING
                                *((unsigned long long*)&saltedData[32]) = resourceTestingDigest;
                                KangarooTwelve(saltedData, 32 + sizeof(resourceTestingDigest), (unsigned char*)&saltedDigest, sizeof(resourceTestingDigest));
                                if (tick->saltedResourceTestingDigest == *((unsigned long long*)&saltedDigest))
#endif
                                {
                                    *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedSpectrumDigest);
                                    KangarooTwelve64To32(saltedData, saltedDigest);
                                    if (EQUAL(*((__m256i*)tick->saltedSpectrumDigest), *((__m256i*)saltedDigest)))
                                    {
                                        *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedUniverseDigest);
                                        KangarooTwelve64To32(saltedData, saltedDigest);
                                        if (EQUAL(*((__m256i*)tick->saltedUniverseDigest), *((__m256i*)saltedDigest)))
                                        {
                                            *((__m256i*) & saltedData[32]) = *((__m256i*)etalonTick.saltedComputerDigest);
                                            KangarooTwelve64To32(saltedData, saltedDigest);
                                            if (EQUAL(*((__m256i*)tick->saltedComputerDigest), *((__m256i*)saltedDigest)))
                                            {
                                                *((unsigned long long*) & tickEssence.millisecond) = *((unsigned long long*) & tick->millisecond);
                                                *((__m256i*)tickEssence.prevSpectrumDigest) = *((__m256i*)tick->prevSpectrumDigest);
                                                *((__m256i*)tickEssence.prevUniverseDigest) = *((__m256i*)tick->prevUniverseDigest);
                                                *((__m256i*)tickEssence.prevComputerDigest) = *((__m256i*)tick->prevComputerDigest);
                                                *((__m256i*)tickEssence.transactionDigest) = *((__m256i*)tick->transactionDigest);
                                                __m256i tickEssenceDigest;
                                                KangarooTwelve((unsigned char*)&tickEssence, sizeof(TickEssence), (unsigned char*)&tickEssenceDigest, 32);
                                                if (EQUAL(tickEssenceDigest, etalonTickEssenceDigest))
                                                {
                                                    tickNumberOfComputors++;
                                                }
                                                else
                                                {
                                                    if (*((unsigned long long*) & tick->millisecond) != *((unsigned long long*) & etalonTick.millisecond)) testFlags |= 16;
                                                    if (!EQUAL(*((__m256i*)tick->prevSpectrumDigest), *((__m256i*)etalonTick.prevSpectrumDigest))) testFlags |= 32;
                                                    if (!EQUAL(*((__m256i*)tick->prevUniverseDigest), *((__m256i*)etalonTick.prevUniverseDigest))) testFlags |= 64;
                                                    if (!EQUAL(*((__m256i*)tick->prevComputerDigest), *((__m256i*)etalonTick.prevComputerDigest))) testFlags |= 128;
                                                    if (!EQUAL(*((__m256i*)tick->transactionDigest), *((__m256i*)etalonTick.transactionDigest))) testFlags |= 256;
                                                }
                                            }
                                            else
                                            {
                                                testFlags |= 8;
                                            }
                                        }
                                        else
                                        {
                                            testFlags |= 4;
                                        }
                                    }
                                    else
                                    {
                                        testFlags |= 2;
                                    }
                                }
                            }

                            RELEASE(tickLocks[i]);
                        }
                        ::tickNumberOfComputors = tickNumberOfComputors;
                        ::tickTotalNumberOfComputors = tickTotalNumberOfComputors;

                        if (tickPhase < 3)
                        {
                            tickPhase = 3;
                        }

                        if (tickNumberOfComputors >= QUORUM)
                        {
                            if (!targetNextTickDataDigestIsKnown)
                            {
                                if (forceNextTick)
                                {
                                    targetNextTickDataDigest = _mm256_setzero_si256();
                                    targetNextTickDataDigestIsKnown = true;
                                    testFlags |= 16384;
                                }
                            }
                            forceNextTick = false;

                            if (targetNextTickDataDigestIsKnown)
                            {
                                if (tickPhase < 4)
                                {
                                    tickPhase = 4;
                                }

                                tickDataSuits = false;
                                if (EQUAL(targetNextTickDataDigest, _mm256_setzero_si256()))
                                {
                                    ACQUIRE(tickDataLock);
                                    tickData[system.tick + 1 - system.initialTick].epoch = 0;
                                    RELEASE(tickDataLock);
                                    nextTickData.epoch = 0;
                                    tickDataSuits = true;
                                }
                                else
                                {
                                    if (nextTickData.epoch != system.epoch)
                                    {
                                        tickDataSuits = false;
                                    }
                                    else
                                    {
                                        testFlags |= 1048576*8;
                                        KangarooTwelve((unsigned char*)&nextTickData, sizeof(TickData), etalonTick.expectedNextTickTransactionDigest, 32);
                                        tickDataSuits = EQUAL(*((__m256i*)etalonTick.expectedNextTickTransactionDigest), targetNextTickDataDigest);
                                    }
                                }
                                if (tickDataSuits)
                                {
                                    const int dayIndex = ::dayIndex(etalonTick.year, etalonTick.month, etalonTick.day);
                                    if ((dayIndex == 738570 + system.epoch * 7 && etalonTick.hour >= 12)
                                        || dayIndex > 738570 + system.epoch * 7)
                                    {
                                        endEpoch();
                                    }
                                    else
                                    {
                                        etalonTick.tick++;
                                        ACQUIRE(tickDataLock);
                                        if (tickData[system.tick - system.initialTick].epoch == system.epoch
                                            && (tickData[system.tick - system.initialTick].year > etalonTick.year
                                                || (tickData[system.tick - system.initialTick].year == etalonTick.year && (tickData[system.tick - system.initialTick].month > etalonTick.month
                                                    || (tickData[system.tick - system.initialTick].month == etalonTick.month && (tickData[system.tick - system.initialTick].day > etalonTick.day
                                                        || (tickData[system.tick - system.initialTick].day == etalonTick.day && (tickData[system.tick - system.initialTick].hour > etalonTick.hour
                                                            || (tickData[system.tick - system.initialTick].hour == etalonTick.hour && (tickData[system.tick - system.initialTick].minute > etalonTick.minute
                                                                || (tickData[system.tick - system.initialTick].minute == etalonTick.minute && (tickData[system.tick - system.initialTick].second > etalonTick.second
                                                                    || (tickData[system.tick - system.initialTick].second == etalonTick.second && tickData[system.tick - system.initialTick].millisecond > etalonTick.millisecond)))))))))))))
                                        {
                                            etalonTick.millisecond = tickData[system.tick - system.initialTick].millisecond;
                                            etalonTick.second = tickData[system.tick - system.initialTick].second;
                                            etalonTick.minute = tickData[system.tick - system.initialTick].minute;
                                            etalonTick.hour = tickData[system.tick - system.initialTick].hour;
                                            etalonTick.day = tickData[system.tick - system.initialTick].day;
                                            etalonTick.month = tickData[system.tick - system.initialTick].month;
                                            etalonTick.year = tickData[system.tick - system.initialTick].year;
                                        }
                                        else
                                        {
                                            if (++etalonTick.millisecond > 999)
                                            {
                                                etalonTick.millisecond = 0;

                                                if (++etalonTick.second > 59)
                                                {
                                                    etalonTick.second = 0;

                                                    if (++etalonTick.minute > 59)
                                                    {
                                                        etalonTick.minute = 0;

                                                        if (++etalonTick.hour > 23)
                                                        {
                                                            etalonTick.hour = 0;

                                                            if (++etalonTick.day > ((etalonTick.month == 1 || etalonTick.month == 3 || etalonTick.month == 5 || etalonTick.month == 7 || etalonTick.month == 8 || etalonTick.month == 10 || etalonTick.month == 12) ? 31 : ((etalonTick.month == 4 || etalonTick.month == 6 || etalonTick.month == 9 || etalonTick.month == 11) ? 30 : ((etalonTick.year & 3) ? 28 : 29))))
                                                            {
                                                                etalonTick.day = 1;

                                                                if (++etalonTick.month > 12)
                                                                {
                                                                    etalonTick.month = 1;

                                                                    ++etalonTick.year;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        RELEASE(tickDataLock);
                                    }

                                    system.tick++;

                                    testFlags = 0;

                                    tickPhase = 0;

                                    ::tickNumberOfComputors = 0;
                                    ::tickTotalNumberOfComputors = 0;
                                    targetNextTickDataDigestIsKnown = false;
                                    numberOfNextTickTransactions = 0;
                                    numberOfKnownNextTickTransactions = 0;

                                    for (unsigned int i = 0; i < sizeof(tickTicks) / sizeof(tickTicks[0]) - 1; i++)
                                    {
                                        tickTicks[i] = tickTicks[i + 1];
                                    }
                                    tickTicks[sizeof(tickTicks) / sizeof(tickTicks[0]) - 1] = __rdtsc();
                                }
                            }
                        }
                    }
                }
            }
        }

        tickerLoopNumerator += __rdtsc() - curTimeTick;
        tickerLoopDenominator++;
    }
}

static void emptyCallback(EFI_EVENT Event, void* Context)
{
}

static void shutdownCallback(EFI_EVENT Event, void* Context)
{
    bs->CloseEvent(Event);
}

static void contractProcessorShutdownCallback(EFI_EVENT Event, void* Context)
{
    bs->CloseEvent(Event);

    contractProcessorState = 0;
}

static long long load(CHAR16* fileName, unsigned long long totalSize, unsigned char* buffer)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL* file;
    if (status = root->Open(root, (void**)&file, fileName, EFI_FILE_MODE_READ, 0))
    {
        logStatus(L"EFI_FILE_PROTOCOL.Open() fails", status, __LINE__);

        return -1;
    }
    else
    {
        long long readSize = 0;
        while (readSize < totalSize)
        {
            unsigned long long size = (READING_CHUNK_SIZE <= (totalSize - readSize) ? READING_CHUNK_SIZE : (totalSize - readSize));
            status = file->Read(file, &size, &buffer[readSize]);
            if (status
                || size != (READING_CHUNK_SIZE <= (totalSize - readSize) ? READING_CHUNK_SIZE : (totalSize - readSize)))
            {
                logStatus(L"EFI_FILE_PROTOCOL.Read() fails", status, __LINE__);

                file->Close(file);

                return -1;
            }
            readSize += size;
        }
        file->Close(file);

        return readSize;
    }
}

static long long save(CHAR16* fileName, unsigned long long totalSize, unsigned char* buffer)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL* file;
    if (status = root->Open(root, (void**)&file, fileName, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0))
    {
        logStatus(L"EFI_FILE_PROTOCOL.Open() fails", status, __LINE__);

        return -1;
    }
    else
    {
        long long writtenSize = 0;
        while (writtenSize < totalSize)
        {
            unsigned long long size = (WRITING_CHUNK_SIZE <= (totalSize - writtenSize) ? WRITING_CHUNK_SIZE : (totalSize - writtenSize));
            status = file->Write(file, &size, &buffer[writtenSize]);
            if (status
                || size != (WRITING_CHUNK_SIZE <= (totalSize - writtenSize) ? WRITING_CHUNK_SIZE : (totalSize - writtenSize)))
            {
                logStatus(L"EFI_FILE_PROTOCOL.Write() fails", status, __LINE__);

                file->Close(file);

                return -1;
            }
            writtenSize += size;
        }
        file->Close(file);

        return writtenSize;
    }
}

static void saveSpectrum()
{
    const unsigned long long beginningTick = __rdtsc();

    ACQUIRE(spectrumLock);
    long long savedSize = save(SPECTRUM_FILE_NAME, SPECTRUM_CAPACITY * sizeof(::Entity), (unsigned char*)spectrum);
    RELEASE(spectrumLock);

    if (savedSize == SPECTRUM_CAPACITY * sizeof(::Entity))
    {
        setNumber(message, savedSize, TRUE);
        appendText(message, L" bytes of the spectrum data are saved (");
        appendNumber(message, (__rdtsc() - beginningTick) * 1000000 / frequency, TRUE);
        appendText(message, L" microseconds).");
        log(message);
    }
}

static void saveUniverse()
{
    const unsigned long long beginningTick = __rdtsc();

    ACQUIRE(universeLock);
    long long savedSize = save(UNIVERSE_FILE_NAME, ASSETS_CAPACITY * sizeof(Asset), (unsigned char*)assets);
    RELEASE(universeLock);

    if (savedSize == ASSETS_CAPACITY * sizeof(Asset))
    {
        setNumber(message, savedSize, TRUE);
        appendText(message, L" bytes of the universe data are saved (");
        appendNumber(message, (__rdtsc() - beginningTick) * 1000000 / frequency, TRUE);
        appendText(message, L" microseconds).");
        log(message);
    }
}

static void saveComputer()
{
    const unsigned long long beginningTick = __rdtsc();

    bool ok = true;
    unsigned long long totalSize = 0;

    ACQUIRE(computerLock);

    for (unsigned int contractIndex = 0; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
    {
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 9] = contractIndex / 1000 + L'0';
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 8] = (contractIndex % 1000) / 100 + L'0';
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 7] = (contractIndex % 100) / 10 + L'0';
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 6] = contractIndex % 10 + L'0';
        long long savedSize = save(CONTRACT_FILE_NAME, contractDescriptions[contractIndex].stateSize, contractStates[contractIndex]);
        totalSize += savedSize;
        if (savedSize != contractDescriptions[contractIndex].stateSize)
        {
            ok = false;

            break;
        }
    }

    RELEASE(computerLock);

    if (ok)
    {
        setNumber(message, totalSize, TRUE);
        appendText(message, L" bytes of the computer data are saved (");
        appendNumber(message, (__rdtsc() - beginningTick) * 1000000 / frequency, TRUE);
        appendText(message, L" microseconds).");
        log(message);
    }
}

static void saveSystem()
{
    const unsigned long long beginningTick = __rdtsc();
    long long savedSize = save(SYSTEM_FILE_NAME, sizeof(system), (unsigned char*)&system);
    if (savedSize == sizeof(system))
    {
        setNumber(message, savedSize, TRUE);
        appendText(message, L" bytes of the system data are saved (");
        appendNumber(message, (__rdtsc() - beginningTick) * 1000000 / frequency, TRUE);
        appendText(message, L" microseconds).");
        log(message);
    }
}

static bool initialize()
{
    enableAVX();

#if AVX512
    zero = _mm512_maskz_set1_epi64(0, 0);
    moveThetaPrev = _mm512_setr_epi64(4, 0, 1, 2, 3, 5, 6, 7);
    moveThetaNext = _mm512_setr_epi64(1, 2, 3, 4, 0, 5, 6, 7);
    rhoB = _mm512_setr_epi64(0, 1, 62, 28, 27, 0, 0, 0);
    rhoG = _mm512_setr_epi64(36, 44, 6, 55, 20, 0, 0, 0);
    rhoK = _mm512_setr_epi64(3, 10, 43, 25, 39, 0, 0, 0);
    rhoM = _mm512_setr_epi64(41, 45, 15, 21, 8, 0, 0, 0);
    rhoS = _mm512_setr_epi64(18, 2, 61, 56, 14, 0, 0, 0);
    pi1B = _mm512_setr_epi64(0, 3, 1, 4, 2, 5, 6, 7);
    pi1G = _mm512_setr_epi64(1, 4, 2, 0, 3, 5, 6, 7);
    pi1K = _mm512_setr_epi64(2, 0, 3, 1, 4, 5, 6, 7);
    pi1M = _mm512_setr_epi64(3, 1, 4, 2, 0, 5, 6, 7);
    pi1S = _mm512_setr_epi64(4, 2, 0, 3, 1, 5, 6, 7);
    pi2S1 = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 8, 10);
    pi2S2 = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 9, 11);
    pi2BG = _mm512_setr_epi64(0, 1, 8, 9, 6, 5, 6, 7);
    pi2KM = _mm512_setr_epi64(2, 3, 10, 11, 7, 5, 6, 7);
    pi2S3 = _mm512_setr_epi64(4, 5, 12, 13, 4, 5, 6, 7);
    padding = _mm512_maskz_set1_epi64(1, 0x8000000000000000);

    K12RoundConst0 = _mm512_maskz_set1_epi64(1, 0x000000008000808bULL);
    K12RoundConst1 = _mm512_maskz_set1_epi64(1, 0x800000000000008bULL);
    K12RoundConst2 = _mm512_maskz_set1_epi64(1, 0x8000000000008089ULL);
    K12RoundConst3 = _mm512_maskz_set1_epi64(1, 0x8000000000008003ULL);
    K12RoundConst4 = _mm512_maskz_set1_epi64(1, 0x8000000000008002ULL);
    K12RoundConst5 = _mm512_maskz_set1_epi64(1, 0x8000000000000080ULL);
    K12RoundConst6 = _mm512_maskz_set1_epi64(1, 0x000000000000800aULL);
    K12RoundConst7 = _mm512_maskz_set1_epi64(1, 0x800000008000000aULL);
    K12RoundConst8 = _mm512_maskz_set1_epi64(1, 0x8000000080008081ULL);
    K12RoundConst9 = _mm512_maskz_set1_epi64(1, 0x8000000000008080ULL);
    K12RoundConst10 = _mm512_maskz_set1_epi64(1, 0x0000000080000001ULL);
    K12RoundConst11 = _mm512_maskz_set1_epi64(1, 0x8000000080008008ULL);

    B1 = _mm256_set_epi64x(B14, B13, B12, B11);
    B2 = _mm256_set_epi64x(B24, B23, B22, B21);
    B3 = _mm256_set_epi64x(B34, B33, B32, B31);
    B4 = _mm256_set_epi64x(B44, B43, B42, B41);
    C = _mm256_set_epi64x(C4, C3, C2, C1);
#endif

    for (unsigned int contractIndex = 0; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
    {
        contractStates[contractIndex] = NULL;
    }
    bs->SetMem(contractSystemProcedures, sizeof(contractSystemProcedures), 0);
    bs->SetMem(contractUserProcedures, sizeof(contractUserProcedures), 0);

    getPublicKeyFromIdentity((const unsigned char*)OPERATOR, operatorPublicKey);
    if (EQUAL(*((__m256i*)operatorPublicKey), _mm256_setzero_si256()))
    {
        _rdrand64_step((unsigned long long*)&operatorPublicKey[0]);
        _rdrand64_step((unsigned long long*)&operatorPublicKey[8]);
        _rdrand64_step((unsigned long long*)&operatorPublicKey[16]);
        _rdrand64_step((unsigned long long*)&operatorPublicKey[24]);
    }

    for (unsigned int i = 0; i < sizeof(computorSeeds) / sizeof(computorSeeds[0]); i++)
    {
        if (!getSubseed(computorSeeds[i], computorSubseeds[i]))
        {
            return false;
        }
        getPrivateKey(computorSubseeds[i], computorPrivateKeys[i]);
        getPublicKey(computorPrivateKeys[i], computorPublicKeys[i]);
    }

    getPublicKeyFromIdentity((const unsigned char*)ARBITRATOR, (unsigned char*)&arbitratorPublicKey);

    int cpuInfo[4];
    __cpuid(cpuInfo, 0x15);
    if (cpuInfo[2] == 0 || cpuInfo[1] == 0 || cpuInfo[0] == 0)
    {
        log(L"Theoretical TSC frequency = n/a.");
    }
    else
    {
        setText(message, L"Theoretical TSC frequency = ");
        appendNumber(message, ((unsigned long long)cpuInfo[1]) * cpuInfo[2] / cpuInfo[0], TRUE);
        appendText(message, L" Hz.");
        log(message);
    }

    frequency = __rdtsc();
    bs->Stall(1000000);
    frequency = __rdtsc() - frequency;
    setText(message, L"Practical TSC frequency = ");
    appendNumber(message, frequency, TRUE);
    appendText(message, L" Hz.");
    log(message);

    bs->SetMem((void*)tickLocks, sizeof(tickLocks), 0);
    bs->SetMem(&tickTicks, sizeof(tickTicks), 0);

    bs->SetMem(processors, sizeof(processors), 0);
    bs->SetMem(peers, sizeof(peers), 0);
    bs->SetMem(publicPeers, sizeof(publicPeers), 0);

    broadcastedComputors.header.setSize(sizeof(broadcastedComputors.header) + sizeof(broadcastedComputors.broadcastComputors));
    broadcastedComputors.header.setType(BROADCAST_COMPUTORS);
    broadcastedComputors.broadcastComputors.computors.epoch = 0;
    for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
    {
        _rdrand64_step((unsigned long long*)&broadcastedComputors.broadcastComputors.computors.publicKeys[i][0]);
        _rdrand64_step((unsigned long long*)&broadcastedComputors.broadcastComputors.computors.publicKeys[i][8]);
        _rdrand64_step((unsigned long long*)&broadcastedComputors.broadcastComputors.computors.publicKeys[i][16]);
        _rdrand64_step((unsigned long long*)&broadcastedComputors.broadcastComputors.computors.publicKeys[i][24]);
    }
    bs->SetMem(&broadcastedComputors.broadcastComputors.computors.signature, sizeof(broadcastedComputors.broadcastComputors.computors.signature), 0);

    requestedComputors.header.setSize(sizeof(requestedComputors));
    requestedComputors.header.setType(REQUEST_COMPUTORS);
    requestedQuorumTick.header.setSize(sizeof(requestedQuorumTick));
    requestedQuorumTick.header.setType(REQUEST_QUORUM_TICK);
    requestedTickData.header.setSize(sizeof(requestedTickData));
    requestedTickData.header.setType(REQUEST_TICK_DATA);
    requestedTickTransactions.header.setSize(sizeof(requestedTickTransactions));
    requestedTickTransactions.header.setType(REQUEST_TICK_TRANSACTIONS);
    requestedTickTransactions.requestedTickTransactions.tick = 0;

    EFI_STATUS status;

    EFI_GUID simpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    unsigned long long numberOfHandles;
    EFI_HANDLE* handles;
    if (status = bs->LocateHandleBuffer(ByProtocol, &simpleFileSystemProtocolGuid, NULL, &numberOfHandles, &handles))
    {
        logStatus(L"EFI_BOOT_SERVICES.LocateHandleBuffer() fails", status, __LINE__);

        return false;
    }
    else
    {
        for (unsigned int i = 0; i < numberOfHandles; i++)
        {
            if (status = bs->OpenProtocol(handles[i], &simpleFileSystemProtocolGuid, (void**)&simpleFileSystemProtocol, ih, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL))
            {
                logStatus(L"EFI_BOOT_SERVICES.OpenProtocol() fails", status, __LINE__);

                bs->FreePool(handles);

                return false;
            }
            else
            {
                if (status = simpleFileSystemProtocol->OpenVolume(simpleFileSystemProtocol, (void**)&root))
                {
                    logStatus(L"EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.OpenVolume() fails", status, __LINE__);

                    bs->CloseProtocol(handles[i], &simpleFileSystemProtocolGuid, ih, NULL);
                    bs->FreePool(handles);

                    return false;
                }
                else
                {
                    EFI_GUID fileSystemInfoId = EFI_FILE_SYSTEM_INFO_ID;
                    EFI_FILE_SYSTEM_INFO info;
                    unsigned long long size = sizeof(info);
                    if (status = root->GetInfo(root, &fileSystemInfoId, &size, &info))
                    {
                        logStatus(L"EFI_FILE_PROTOCOL.GetInfo() fails", status, __LINE__);

                        bs->CloseProtocol(handles[i], &simpleFileSystemProtocolGuid, ih, NULL);
                        bs->FreePool(handles);

                        return false;
                    }
                    else
                    {
                        setText(message, L"Volume #");
                        appendNumber(message, i, FALSE);
                        appendText(message, L" (");
                        appendText(message, info.VolumeLabel);
                        appendText(message, L"): ");
                        appendNumber(message, info.FreeSpace, TRUE);
                        appendText(message, L" / ");
                        appendNumber(message, info.VolumeSize, TRUE);
                        appendText(message, L" free bytes | Read-");
                        appendText(message, info.ReadOnly ? L"only." : L"Write.");
                        log(message);

                        bool matches = true;
                        for (unsigned int j = 0; j < sizeof(info.VolumeLabel) / sizeof(info.VolumeLabel[0]); j++)
                        {
                            if (info.VolumeLabel[j] != VOLUME_LABEL[j] && info.VolumeLabel[j] != (VOLUME_LABEL[j] ^ 0x20))
                            {
                                matches = false;

                                break;
                            }
                            if (!VOLUME_LABEL[j])
                            {
                                break;
                            }
                        }
                        if (matches)
                        {
                            break;
                        }
                        else
                        {
                            bs->CloseProtocol(handles[i], &simpleFileSystemProtocolGuid, ih, NULL);
                            simpleFileSystemProtocol = NULL;
                        }
                    }
                }
            }
        }

        bs->FreePool(handles);
    }

    if (!simpleFileSystemProtocol)
    {
        bs->LocateProtocol(&simpleFileSystemProtocolGuid, NULL, (void**)&simpleFileSystemProtocol);
    }
    if (status = simpleFileSystemProtocol->OpenVolume(simpleFileSystemProtocol, (void**)&root))
    {
        logStatus(L"EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.OpenVolume() fails", status, __LINE__);

        return false;
    }
    else
    {
        if (status = bs->AllocatePool(EfiRuntimeServicesData, ((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_COMPUTORS * sizeof(Tick), (void**)&ticks))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(ticks, ((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_COMPUTORS * sizeof(Tick), 0);
        if ((status = bs->AllocatePool(EfiRuntimeServicesData, ((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * sizeof(TickData), (void**)&tickData))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS), (void**)&tickTransactions))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, SPECTRUM_CAPACITY * MAX_TRANSACTION_SIZE, (void**)&entityPendingTransactions))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, SPECTRUM_CAPACITY * 32ULL, (void**)&entityPendingTransactionDigests)))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(tickData, ((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * sizeof(TickData), 0);
        bs->SetMem(tickTransactions, FIRST_TICK_TRANSACTION_OFFSET + (((unsigned long long)MAX_NUMBER_OF_TICKS_PER_EPOCH) * NUMBER_OF_TRANSACTIONS_PER_TICK * MAX_TRANSACTION_SIZE / TRANSACTION_SPARSENESS), 0);
        bs->SetMem(tickTransactionOffsets, sizeof(tickTransactionOffsets), 0);
        for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
        {
            ((Transaction*)&entityPendingTransactions[i * MAX_TRANSACTION_SIZE])->tick = 0;
        }

        if (status = bs->AllocatePool(EfiRuntimeServicesData, SPECTRUM_CAPACITY * sizeof(::Entity) >= ASSETS_CAPACITY * sizeof(Asset) ? SPECTRUM_CAPACITY * sizeof(::Entity) : ASSETS_CAPACITY * sizeof(Asset), (void**)&reorgBuffer))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }

        if ((status = bs->AllocatePool(EfiRuntimeServicesData, SPECTRUM_CAPACITY * sizeof(::Entity), (void**)&spectrum))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, (SPECTRUM_CAPACITY * 2 - 1) * 32ULL, (void**)&spectrumDigests)))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(spectrumChangeFlags, sizeof(spectrumChangeFlags), 0);

        if ((status = bs->AllocatePool(EfiRuntimeServicesData, ASSETS_CAPACITY * sizeof(Asset), (void**)&assets))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, (ASSETS_CAPACITY * 2 - 1) * 32ULL, (void**)&assetDigests))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, ASSETS_CAPACITY / 8, (void**)&assetChangeFlags)))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(assetChangeFlags, ASSETS_CAPACITY / 8, 0xFF);

        for (unsigned int contractIndex = 0; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
        {
            unsigned long long size = contractDescriptions[contractIndex].stateSize;
            if (status = bs->AllocatePool(EfiRuntimeServicesData, size, (void**)&contractStates[contractIndex]))
            {
                logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

                return false;
            }
        }
        if ((status = bs->AllocatePool(EfiRuntimeServicesData, MAX_NUMBER_OF_CONTRACTS / 8, (void**)&contractStateChangeFlags))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, 536870912, (void**)&functionFlags)))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(contractStateChangeFlags, MAX_NUMBER_OF_CONTRACTS / 8, 0xFF);
        bs->SetMem(functionFlags, 536870912, 0);

        bs->SetMem(&system, sizeof(system), 0);
        load(SYSTEM_FILE_NAME, sizeof(system), (unsigned char*)&system);
        system.version = VERSION_B;
        system.epoch = EPOCH;
        system.initialHour = 12;
        system.initialDay = 13;
        system.initialMonth = 4;
        system.initialYear = 22;
        if (system.epoch == EPOCH)
        {
            system.initialTick = TICK;
        }
        system.tick = system.initialTick;

        etalonTick.epoch = system.epoch;
        etalonTick.tick = system.initialTick;
        etalonTick.millisecond = system.initialMillisecond;
        etalonTick.second = system.initialSecond;
        etalonTick.minute = system.initialMinute;
        etalonTick.hour = system.initialHour;
        etalonTick.day = system.initialDay;
        etalonTick.month = system.initialMonth;
        etalonTick.year = system.initialYear;

        bs->SetMem(solutionPublicationTicks, sizeof(solutionPublicationTicks), 0);

        bs->SetMem(faultyComputorFlags, sizeof(faultyComputorFlags), 0);

        SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
        SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
        SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
        long long loadedSize = load(SPECTRUM_FILE_NAME, SPECTRUM_CAPACITY * sizeof(::Entity), (unsigned char*)spectrum);
        if (loadedSize != SPECTRUM_CAPACITY * sizeof(::Entity))
        {
            logStatus(L"EFI_FILE_PROTOCOL.Read() reads invalid number of bytes", loadedSize, __LINE__);

            return false;
        }
        {
            const unsigned long long beginningTick = __rdtsc();

            unsigned int digestIndex;
            for (digestIndex = 0; digestIndex < SPECTRUM_CAPACITY; digestIndex++)
            {
                KangarooTwelve64To32((unsigned char*)&spectrum[digestIndex], (unsigned char*)&spectrumDigests[digestIndex]);
            }
            unsigned int previousLevelBeginning = 0;
            unsigned int numberOfLeafs = SPECTRUM_CAPACITY;
            while (numberOfLeafs > 1)
            {
                for (unsigned int i = 0; i < numberOfLeafs; i += 2)
                {
                    KangarooTwelve64To32((unsigned char*)&spectrumDigests[previousLevelBeginning + i], (unsigned char*)&spectrumDigests[digestIndex++]);
                }

                previousLevelBeginning += numberOfLeafs;
                numberOfLeafs >>= 1;
            }

            setNumber(message, SPECTRUM_CAPACITY * sizeof(::Entity), TRUE);
            appendText(message, L" bytes of the spectrum data are hashed (");
            appendNumber(message, (__rdtsc() - beginningTick) * 1000000 / frequency, TRUE);
            appendText(message, L" microseconds).");
            log(message);

            CHAR16 digestChars[60 + 1];
            unsigned long long totalAmount = 0;

            getIdentity((unsigned char*)&spectrumDigests[(SPECTRUM_CAPACITY * 2 - 1) - 1], digestChars, true);

            for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
            {
                if (spectrum[i].incomingAmount - spectrum[i].outgoingAmount)
                {
                    numberOfEntities++;
                    totalAmount += spectrum[i].incomingAmount - spectrum[i].outgoingAmount;
                }
            }

            setNumber(message, totalAmount, TRUE);
            appendText(message, L" qus in ");
            appendNumber(message, numberOfEntities, TRUE);
            appendText(message, L" entities (digest = ");
            appendText(message, digestChars);
            appendText(message, L").");
            log(message);
        }

        UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
        UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
        UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
        loadedSize = load(UNIVERSE_FILE_NAME, ASSETS_CAPACITY * sizeof(Asset), (unsigned char*)assets);
        if (loadedSize != ASSETS_CAPACITY * sizeof(Asset))
        {
            logStatus(L"EFI_FILE_PROTOCOL.Read() reads invalid number of bytes", loadedSize, __LINE__);

            return false;
        }
        {
            setText(message, L"Universe digest = ");
            __m256i digest;
            getUniverseDigest(&digest);
            CHAR16 digestChars[60 + 1];
            getIdentity((unsigned char*)&digest, digestChars, true);
            appendText(message, digestChars);
            appendText(message, L".");
            log(message);
        }

        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 4] = system.epoch / 100 + L'0';
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 3] = (system.epoch % 100) / 10 + L'0';
        CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 2] = system.epoch % 10 + L'0';
        for (unsigned int contractIndex = 0; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
        {
            if (contractDescriptions[contractIndex].constructionEpoch == system.epoch)
            {
                bs->SetMem(contractStates[contractIndex], contractDescriptions[contractIndex].stateSize, 0);
            }
            else
            {
                CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 9] = contractIndex / 1000 + L'0';
                CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 8] = (contractIndex % 1000) / 100 + L'0';
                CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 7] = (contractIndex % 100) / 10 + L'0';
                CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 6] = contractIndex % 10 + L'0';
                loadedSize = load(CONTRACT_FILE_NAME, contractDescriptions[contractIndex].stateSize, contractStates[contractIndex]);
                if (loadedSize != contractDescriptions[contractIndex].stateSize)
                {
                    logStatus(L"EFI_FILE_PROTOCOL.Read() reads invalid number of bytes", loadedSize, __LINE__);

                    return false;
                }
            }

            initializeContract(contractIndex, contractStates[contractIndex]);
        }
        {
            setText(message, L"Computer digest = ");
            __m256i digest;
            getComputerDigest(&digest);
            CHAR16 digestChars[60 + 1];
            getIdentity((unsigned char*)&digest, digestChars, true);
            appendText(message, digestChars);
            appendText(message, L".");
            log(message);
        }

        unsigned char randomSeed[32];
        bs->SetMem(randomSeed, 32, 0);
        randomSeed[0] = 17;
        randomSeed[1] = 58;
        randomSeed[2] = 136;
        randomSeed[3] = 99;
        randomSeed[4] = 44;
        randomSeed[5] = 120;
        randomSeed[6] = 251;
        randomSeed[7] = 9;
        random(randomSeed, randomSeed, (unsigned char*)miningData, sizeof(miningData));

        if (status = bs->AllocatePool(EfiRuntimeServicesData, NUMBER_OF_MINER_SOLUTION_FLAGS / 8, (void**)&minerSolutionFlags))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        bs->SetMem(minerSolutionFlags, NUMBER_OF_MINER_SOLUTION_FLAGS / 8, 0);

        bs->SetMem((void*)minerScores, sizeof(minerScores[0]) * NUMBER_OF_COMPUTORS, 0);
    }

    if ((status = bs->AllocatePool(EfiRuntimeServicesData, 536870912, (void**)&dejavu0))
        || (status = bs->AllocatePool(EfiRuntimeServicesData, 536870912, (void**)&dejavu1)))
    {
        logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

        return false;
    }
    bs->SetMem((void*)dejavu0, 536870912, 0);
    bs->SetMem((void*)dejavu1, 536870912, 0);

    if ((status = bs->AllocatePool(EfiRuntimeServicesData, REQUEST_QUEUE_BUFFER_SIZE, (void**)&requestQueueBuffer))
        || (status = bs->AllocatePool(EfiRuntimeServicesData, RESPONSE_QUEUE_BUFFER_SIZE, (void**)&responseQueueBuffer)))
    {
        logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

        return false;
    }

    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        peers[i].receiveData.FragmentCount = 1;
        peers[i].transmitData.FragmentCount = 1;
        if ((status = bs->AllocatePool(EfiRuntimeServicesData, BUFFER_SIZE, &peers[i].receiveBuffer))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, BUFFER_SIZE, &peers[i].transmitData.FragmentTable[0].FragmentBuffer))
            || (status = bs->AllocatePool(EfiRuntimeServicesData, BUFFER_SIZE, (void**)&peers[i].dataToTransmit)))
        {
            logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

            return false;
        }
        if ((status = bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, emptyCallback, NULL, &peers[i].connectAcceptToken.CompletionToken.Event))
            || (status = bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, emptyCallback, NULL, &peers[i].receiveToken.CompletionToken.Event))
            || (status = bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, emptyCallback, NULL, &peers[i].transmitToken.CompletionToken.Event)))
        {
            logStatus(L"EFI_BOOT_SERVICES.CreateEvent() fails", status, __LINE__);

            return false;
        }
        peers[i].connectAcceptToken.CompletionToken.Status = -1;
        peers[i].receiveToken.CompletionToken.Status = -1;
        peers[i].receiveToken.Packet.RxData = &peers[i].receiveData;
        peers[i].transmitToken.CompletionToken.Status = -1;
        peers[i].transmitToken.Packet.TxData = &peers[i].transmitData;
    }

    for (unsigned int i = 0; i < sizeof(knownPublicPeers) / sizeof(knownPublicPeers[0]) && numberOfPublicPeers < MAX_NUMBER_OF_PUBLIC_PEERS; i++)
    {
        addPublicPeer((unsigned char*)knownPublicPeers[i]);
    }

    return true;
}

static void deinitialize()
{
    bs->SetMem(computorSeeds, sizeof(computorSeeds), 0);
    bs->SetMem(computorSubseeds, sizeof(computorSubseeds), 0);
    bs->SetMem(computorPrivateKeys, sizeof(computorPrivateKeys), 0);
    bs->SetMem(computorPublicKeys, sizeof(computorPublicKeys), 0);

    if (root)
    {
        root->Close(root);
    }

    if (spectrumDigests)
    {
        bs->FreePool(spectrumDigests);
    }
    if (spectrum)
    {
        bs->FreePool(spectrum);
    }

    if (assetChangeFlags)
    {
        bs->FreePool(assetChangeFlags);
    }
    if (assetDigests)
    {
        bs->FreePool(assetDigests);
    }
    if (assets)
    {
        bs->FreePool(assets);
    }

    if (reorgBuffer)
    {
        bs->FreePool(reorgBuffer);
    }

    if (functionFlags)
    {
        bs->FreePool(functionFlags);
    }
    if (contractStateChangeFlags)
    {
        bs->FreePool(contractStateChangeFlags);
    }
    for (unsigned int contractIndex = 0; contractIndex < sizeof(contractDescriptions) / sizeof(contractDescriptions[0]); contractIndex++)
    {
        if (contractStates[contractIndex])
        {
            bs->FreePool(contractStates[contractIndex]);
        }
    }

    if (entityPendingTransactionDigests)
    {
        bs->FreePool(entityPendingTransactionDigests);
    }
    if (entityPendingTransactions)
    {
        bs->FreePool(entityPendingTransactions);
    }
    if (tickTransactions)
    {
        bs->FreePool(tickTransactions);
    }
    if (tickData)
    {
        bs->FreePool(tickData);
    }
    if (ticks)
    {
        bs->FreePool(ticks);
    }

    if (minerSolutionFlags)
    {
        bs->FreePool(minerSolutionFlags);
    }

    if (dejavu0)
    {
        bs->FreePool((void*)dejavu0);
    }
    if (dejavu1)
    {
        bs->FreePool((void*)dejavu1);
    }

    if (requestQueueBuffer)
    {
        bs->FreePool(requestQueueBuffer);
    }
    if (responseQueueBuffer)
    {
        bs->FreePool(responseQueueBuffer);
    }

    for (unsigned int processorIndex = 0; processorIndex < MAX_NUMBER_OF_PROCESSORS; processorIndex++)
    {
        if (processors[processorIndex].buffer)
        {
            bs->FreePool(processors[processorIndex].buffer);
        }
    }

    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        if (peers[i].receiveBuffer)
        {
            bs->FreePool(peers[i].receiveBuffer);
        }
        if (peers[i].transmitData.FragmentTable[0].FragmentBuffer)
        {
            bs->FreePool(peers[i].transmitData.FragmentTable[0].FragmentBuffer);
        }
        if (peers[i].dataToTransmit)
        {
            bs->FreePool(peers[i].dataToTransmit);

            bs->CloseEvent(peers[i].connectAcceptToken.CompletionToken.Event);
            bs->CloseEvent(peers[i].receiveToken.CompletionToken.Event);
            bs->CloseEvent(peers[i].transmitToken.CompletionToken.Event);
        }
    }
}

static void logInfo()
{
    unsigned long long numberOfWaitingBytes = 0;

    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        if (peers[i].tcp4Protocol)
        {
            numberOfWaitingBytes += peers[i].dataToTransmitSize;
        }
    }

    unsigned int numberOfVerifiedPublicPeers = 0;

    for (unsigned int i = 0; i < numberOfPublicPeers; i++)
    {
        if (publicPeers[i].isVerified)
        {
            numberOfVerifiedPublicPeers++;
        }
    }

    setText(message, L"[+");
    appendNumber(message, numberOfProcessedRequests - prevNumberOfProcessedRequests, TRUE);
    appendText(message, L" -");
    appendNumber(message, numberOfDiscardedRequests - prevNumberOfDiscardedRequests, TRUE);
    appendText(message, L" *");
    appendNumber(message, numberOfDuplicateRequests - prevNumberOfDuplicateRequests, TRUE);
    appendText(message, L" /");
    appendNumber(message, numberOfDisseminatedRequests - prevNumberOfDisseminatedRequests, TRUE);
    appendText(message, L"] ");

    unsigned int numberOfConnectingSlots = 0, numberOfConnectedSlots = 0;
    for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
    {
        if (peers[i].tcp4Protocol)
        {
            if (!peers[i].isConnectedAccepted)
            {
                numberOfConnectingSlots++;
            }
            else
            {
                numberOfConnectedSlots++;
            }
        }
    }
    appendNumber(message, numberOfConnectingSlots, FALSE);
    appendText(message, L"|");
    appendNumber(message, numberOfConnectedSlots, FALSE);

    appendText(message, L" ");
    appendNumber(message, numberOfVerifiedPublicPeers, TRUE);
    appendText(message, L"/");
    appendNumber(message, numberOfPublicPeers, TRUE);
    appendText(message, listOfPeersIsStatic ? L" Static" : L" Dynamic");
    appendText(message, L" (+");
    appendNumber(message, numberOfReceivedBytes - prevNumberOfReceivedBytes, TRUE);
    appendText(message, L" -");
    appendNumber(message, numberOfTransmittedBytes - prevNumberOfTransmittedBytes, TRUE);
    appendText(message, L" ..."); appendNumber(message, numberOfWaitingBytes, TRUE);
    appendText(message, L").");
    log(message);
    prevNumberOfProcessedRequests = numberOfProcessedRequests;
    prevNumberOfDiscardedRequests = numberOfDiscardedRequests;
    prevNumberOfDuplicateRequests = numberOfDuplicateRequests;
    prevNumberOfDisseminatedRequests = numberOfDisseminatedRequests;
    prevNumberOfReceivedBytes = numberOfReceivedBytes;
    prevNumberOfTransmittedBytes = numberOfTransmittedBytes;

    setNumber(message, numberOfProcessors - 2, TRUE);

    appendText(message, L" | Tick = ");
    unsigned long long tickDuration = (tickTicks[sizeof(tickTicks) / sizeof(tickTicks[0]) - 1] - tickTicks[0]) / (sizeof(tickTicks) / sizeof(tickTicks[0]) - 1);
    appendNumber(message, tickDuration / frequency, FALSE);
    appendText(message, L".");
    appendNumber(message, (tickDuration % frequency) * 10 / frequency, FALSE);
    appendText(message, L" s | Indices = ");
    if (!numberOfOwnComputorIndices)
    {
        appendText(message, L"?.");
    }
    else
    {
        const CHAR16 alphabet[26][2] = { L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J", L"K", L"L", L"M", L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U", L"V", L"W", L"X", L"Y", L"Z" };
        for (unsigned int i = 0; i < numberOfOwnComputorIndices; i++)
        {
            appendText(message, alphabet[ownComputorIndices[i] / 26]);
            appendText(message, alphabet[ownComputorIndices[i] % 26]);
            appendText(message, i ? L"[" : L"[in ");
            appendNumber(message, ((ownComputorIndices[i] + NUMBER_OF_COMPUTORS) - system.tick % NUMBER_OF_COMPUTORS) % NUMBER_OF_COMPUTORS, FALSE);
            if (!i)
            {
                appendText(message, L" ticks");
            }
            if (i < (unsigned int)(numberOfOwnComputorIndices - 1))
            {
                appendText(message, L"]+");
            }
            else
            {
                appendText(message, L"].");
            }
        }
    }
    log(message);

    unsigned int numberOfPendingTransactions = 0;
    for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
    {
        if (((Transaction*)&entityPendingTransactions[i * MAX_TRANSACTION_SIZE])->tick > system.tick)
        {
            numberOfPendingTransactions++;
        }
    }
    if (nextTickTransactionsSemaphore)
    {
        setText(message, L"?");
    }
    else
    {
        setNumber(message, numberOfKnownNextTickTransactions, TRUE);
    }
    appendText(message, L"/");
    if (nextTickTransactionsSemaphore)
    {
        appendText(message, L"?");
    }
    else
    {
        appendNumber(message, numberOfNextTickTransactions, TRUE);
    }
    appendText(message, L" next tick transactions are known. ");
    if (tickData[system.tick + 1 - system.initialTick].epoch == system.epoch)
    {
        appendText(message, L"(");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].year / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].year % 10, FALSE);
        appendText(message, L".");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].month / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].month % 10, FALSE);
        appendText(message, L".");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].day / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].day % 10, FALSE);
        appendText(message, L" ");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].hour / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].hour % 10, FALSE);
        appendText(message, L":");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].minute / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].minute % 10, FALSE);
        appendText(message, L":");
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].second / 10, FALSE);
        appendNumber(message, tickData[system.tick + 1 - system.initialTick].second % 10, FALSE);
        appendText(message, L".) ");
    }
    appendNumber(message, numberOfPendingTransactions, TRUE);
    appendText(message, L" pending transactions.");
    log(message);

    unsigned int filledRequestQueueBufferSize = (requestQueueBufferHead >= requestQueueBufferTail) ? (requestQueueBufferHead - requestQueueBufferTail) : (REQUEST_QUEUE_BUFFER_SIZE - (requestQueueBufferTail - requestQueueBufferHead));
    unsigned int filledResponseQueueBufferSize = (responseQueueBufferHead >= responseQueueBufferTail) ? (responseQueueBufferHead - responseQueueBufferTail) : (RESPONSE_QUEUE_BUFFER_SIZE - (responseQueueBufferTail - responseQueueBufferHead));
    unsigned int filledRequestQueueLength = (requestQueueElementHead >= requestQueueElementTail) ? (requestQueueElementHead - requestQueueElementTail) : (REQUEST_QUEUE_LENGTH - (requestQueueElementTail - requestQueueElementHead));
    unsigned int filledResponseQueueLength = (responseQueueElementHead >= responseQueueElementTail) ? (responseQueueElementHead - responseQueueElementTail) : (RESPONSE_QUEUE_LENGTH - (responseQueueElementTail - responseQueueElementHead));
    setNumber(message, filledRequestQueueBufferSize, TRUE);
    appendText(message, L" (");
    appendNumber(message, filledRequestQueueLength, TRUE);
    appendText(message, L") :: ");
    appendNumber(message, filledResponseQueueBufferSize, TRUE);
    appendText(message, L" (");
    appendNumber(message, filledResponseQueueLength, TRUE);
    appendText(message, L") | Average processing time = ");
    if (queueProcessingDenominator)
    {
        appendNumber(message, (queueProcessingNumerator / queueProcessingDenominator) * 1000000 / frequency, TRUE);
    }
    else
    {
        appendText(message, L"?");
    }
    appendText(message, L" mcs.");
    log(message);
}

static void processKeyPresses()
{
    EFI_INPUT_KEY key;
    if (!st->ConIn->ReadKeyStroke(st->ConIn, &key))
    {
        switch (key.ScanCode)
        {
        /*
        *
        * F2 Key
        * By pressing the F2 Key the node will display the current status.
        * The status includes:
        * Version, faulty Computors, Last Tick Date,
        * Digest of spectrum, univers and computer, number of transactions and solutions processed
        */
        case 0x0C: // 
        {
            setText(message, L"Qubic ");
            appendQubicVersion(message);
            appendText(message, L".");
            log(message);

            unsigned int numberOfFaultyComputors = 0;
            for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
            {
                if (faultyComputorFlags[i >> 6] & (1ULL << (i & 63)))
                {
                    getIdentity(broadcastedComputors.broadcastComputors.computors.publicKeys[i], message, false);
                    appendText(message, L" = ");
                    long long amount = 0;
                    const int spectrumIndex = ::spectrumIndex(broadcastedComputors.broadcastComputors.computors.publicKeys[i]);
                    if (spectrumIndex >= 0)
                    {
                        amount = energy(spectrumIndex);
                    }
                    appendNumber(message, amount, TRUE);
                    appendText(message, L" qus");
                    log(message);

                    numberOfFaultyComputors++;
                }
            }
            setNumber(message, numberOfFaultyComputors, TRUE);
            appendText(message, L" faulty computors.");
            log(message);

            setText(message, L"Tick time was set to ");
            appendNumber(message, etalonTick.year / 10, FALSE);
            appendNumber(message, etalonTick.year % 10, FALSE);
            appendText(message, L".");
            appendNumber(message, etalonTick.month / 10, FALSE);
            appendNumber(message, etalonTick.month % 10, FALSE);
            appendText(message, L".");
            appendNumber(message, etalonTick.day / 10, FALSE);
            appendNumber(message, etalonTick.day % 10, FALSE);
            appendText(message, L" ");
            appendNumber(message, etalonTick.hour / 10, FALSE);
            appendNumber(message, etalonTick.hour % 10, FALSE);
            appendText(message, L":");
            appendNumber(message, etalonTick.minute / 10, FALSE);
            appendNumber(message, etalonTick.minute % 10, FALSE);
            appendText(message, L":");
            appendNumber(message, etalonTick.second / 10, FALSE);
            appendNumber(message, etalonTick.second % 10, FALSE);
            appendText(message, L".");
            appendNumber(message, etalonTick.millisecond / 100, FALSE);
            appendNumber(message, etalonTick.millisecond % 100 / 10, FALSE);
            appendNumber(message, etalonTick.millisecond % 10, FALSE);
            appendText(message, L".");
            log(message);

            CHAR16 digestChars[60 + 1];

            getIdentity((unsigned char*)&spectrumDigests[(SPECTRUM_CAPACITY * 2 - 1) - 1], digestChars, true);
            unsigned int numberOfEntities = 0;
            unsigned long long totalAmount = 0;
            for (unsigned int i = 0; i < SPECTRUM_CAPACITY; i++)
            {
                if (energy(i))
                {
                    numberOfEntities++;
                    totalAmount += energy(i);
                }
            }
            setNumber(message, totalAmount, TRUE);
            appendText(message, L" qus in ");
            appendNumber(message, numberOfEntities, TRUE);
            appendText(message, L" entities (digest = ");
            appendText(message, digestChars);
            appendText(message, L"); ");
            appendNumber(message, numberOfTransactions, TRUE);
            appendText(message, L" transactions.");
            log(message);

            __m256i digest;

            setText(message, L"Universe digest = ");
            getUniverseDigest(&digest);
            getIdentity((unsigned char*)&digest, digestChars, true);
            appendText(message, digestChars);
            appendText(message, L".");
            log(message);

            setText(message, L"Computer digest = ");
            getComputerDigest(&digest);
            getIdentity((unsigned char*)&digest, digestChars, true);
            appendText(message, digestChars);
            appendText(message, L".");
            log(message);

            unsigned int numberOfPublishedSolutions = 0, numberOfRecordedSolutions = 0;
            for (unsigned int i = 0; i < system.numberOfSolutions; i++)
            {
                if (solutionPublicationTicks[i])
                {
                    numberOfPublishedSolutions++;

                    if (solutionPublicationTicks[i] < 0)
                    {
                        numberOfRecordedSolutions++;
                    }
                }
            }
            setNumber(message, numberOfRecordedSolutions, TRUE);
            appendText(message, L"/");
            appendNumber(message, numberOfPublishedSolutions, TRUE);
            appendText(message, L"/");
            appendNumber(message, system.numberOfSolutions, TRUE);
            appendText(message, L" solutions.");
            log(message);

            log(isMain ? L"MAIN   *   MAIN   *   MAIN   *   MAIN   *   MAIN" : L"aux   *   aux   *   aux   *   aux   *   aux");
        }
        break;

        /*
        *
        * F3 Key
        * By Pressing the F3 Key the node will display the current state of the mining race
        * You can see which of your ID's is at which position.
        *
        case 0x0D:
        {
            unsigned int numberOfSolutions = 0;
            for (unsigned int i = 0; i < numberOfMiners; i++)
            {
                numberOfSolutions += minerScores[i];
            }
            setNumber(message, numberOfMiners, TRUE);
            appendText(message, L" miners with ");
            appendNumber(message, numberOfSolutions, TRUE);
            appendText(message, L" solutions (min computor score = ");
            appendNumber(message, minimumComputorScore, TRUE);
            appendText(message, L", min candidate score = ");
            appendNumber(message, minimumCandidateScore, TRUE);
            appendText(message, L").");
            log(message);
        }
        break;*/

        /*
        * F4 Key
        * By Pressing the F4 Key the node will dop all currently active connections.
        * This forces the node to reconnect to known peers and can help to recover stuck situations.
        */
        case 0x0E:
        {
            for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
            {
                closePeer(&peers[i]);
            }
        }
        break;

        /*
        * F5 Key
        * By Pressing the F5 Key the node will issue new votes for it's COMPUTORS.
        * By issuing new "empty" votes a tick can by bypassed if there is no consensus. (to few computors which voted)
        */
        case 0x0F:
        {
            forceNextTick = true;
        }
        break;

        /*
        * F6 Key
        * By Pressing the F6 Key the current state of Qubic is saved to the disk.
        * The Fles generated will be appended by .000
        */
        case 0x10:
        {
            SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 4] = L'0';
            SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 3] = L'0';
            SPECTRUM_FILE_NAME[sizeof(SPECTRUM_FILE_NAME) / sizeof(SPECTRUM_FILE_NAME[0]) - 2] = L'0';
            saveSpectrum();

            UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 4] = L'0';
            UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 3] = L'0';
            UNIVERSE_FILE_NAME[sizeof(UNIVERSE_FILE_NAME) / sizeof(UNIVERSE_FILE_NAME[0]) - 2] = L'0';
            saveUniverse();

            CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 4] = L'0';
            CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 3] = L'0';
            CONTRACT_FILE_NAME[sizeof(CONTRACT_FILE_NAME) / sizeof(CONTRACT_FILE_NAME[0]) - 2] = L'0';
            saveComputer();
        }
        break;

        /*
        * F9 Key
        * By Pressing the F9 Key the latestCreatedTick got's decreased by one.
        * By decreasing this by one, the Node will resend the issued votes for its Computors.
        */
        case 0x13:
        {
            system.latestCreatedTick--;
        }
        break;

        /*
        * F10 Key
        * By Pressing the F10 Key the testFlags will be resetted.
        * The Testflags are used to display debugging information to the log output.
        */
        case 0x14:
        {
            testFlags = 0;
        }
        break;

        /*
        * F11 Key
        * By Pressing the F11 Key the node can swtich between static and dynamic network mode
        * static: incomming connections are blocked and peerlist will not be altered
        * dynamic: all connections are open, peers are added and removed dynamically
        */
        case 0x15:
        {
            listOfPeersIsStatic = !listOfPeersIsStatic;
        }
        break;

        /*
        * F12 Key
        * By Pressing the F12 Key the node can wtich between MAIN and aux mode.
        * MAIN: the node is issuing ticks and participate as "COMPUTOR" in the network
        * aux: the node is running without participating active as "COMPUTOR" in the network
        * !! IMPORTANT !! only one MAIN instance per COMPUTOR is allowed.
        */
        case 0x16:
        {
            isMain = !isMain;
            log(isMain ? L"MAIN   *   MAIN   *   MAIN   *   MAIN   *   MAIN" : L"aux   *   aux   *   aux   *   aux   *   aux");
        }
        break;

        /*
        * ESC Key
        * By Pressing the ESC Key the node will stop
        */
        case 0x17:
        {
            state = 1;
        }
        break;

        /*
        * PAUSE Key
        * By Pressing the PAUSE Key you can toggle the log output
        */
        case 0x48:
        {
            disableLogging = !disableLogging;
        }
        break;
        }
    }
}

EFI_STATUS efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable)
{
    ih = imageHandle;
    st = systemTable;
    rs = st->RuntimeServices;
    bs = st->BootServices;

    bs->SetWatchdogTimer(0, 0, 0, NULL);

    initTime();

    st->ConOut->ClearScreen(st->ConOut);
    setText(message, L"Qubic ");
    appendQubicVersion(message);
    appendText(message, L" is launched.");
    log(message);

    if (initialize())
    {
        EFI_STATUS status;

        unsigned int computingProcessorNumber;
        EFI_GUID mpServiceProtocolGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
        bs->LocateProtocol(&mpServiceProtocolGuid, NULL, (void**)&mpServicesProtocol);
        unsigned long long numberOfAllProcessors, numberOfEnabledProcessors;
        mpServicesProtocol->GetNumberOfProcessors(mpServicesProtocol, &numberOfAllProcessors, &numberOfEnabledProcessors);
        for (unsigned int i = 0; i < numberOfAllProcessors && numberOfProcessors < MAX_NUMBER_OF_PROCESSORS; i++)
        {
            EFI_PROCESSOR_INFORMATION processorInformation;
            mpServicesProtocol->GetProcessorInfo(mpServicesProtocol, i, &processorInformation);
            if (processorInformation.StatusFlag == (PROCESSOR_ENABLED_BIT | PROCESSOR_HEALTH_STATUS_BIT))
            {
                if (status = bs->AllocatePool(EfiRuntimeServicesData, BUFFER_SIZE, &processors[numberOfProcessors].buffer))
                {
                    logStatus(L"EFI_BOOT_SERVICES.AllocatePool() fails", status, __LINE__);

                    numberOfProcessors = 0;

                    break;
                }

                if (numberOfProcessors == 2)
                {
                    computingProcessorNumber = i;
                }
                else
                {
                    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, shutdownCallback, NULL, &processors[numberOfProcessors].event);
                    mpServicesProtocol->StartupThisAP(mpServicesProtocol, numberOfProcessors == 1 ? tickProcessor : requestProcessor, i, processors[numberOfProcessors].event, 0, &processors[numberOfProcessors], NULL);
                }
                numberOfProcessors++;
            }
        }
        if (numberOfProcessors < 3)
        {
            log(L"At least 4 healthy enabled processors are required!");
        }
        else
        {
            setNumber(message, 1 + numberOfProcessors, TRUE);
            appendText(message, L"/");
            appendNumber(message, numberOfAllProcessors, TRUE);
            appendText(message, L" processors are being used.");
            log(message);

            if (status = bs->LocateProtocol(&tcp4ServiceBindingProtocolGuid, NULL, (void**)&tcp4ServiceBindingProtocol))
            {
                logStatus(L"EFI_TCP4_SERVICE_BINDING_PROTOCOL is not located", status, __LINE__);
            }
            else
            {
                const EFI_HANDLE peerChildHandle = getTcp4Protocol(NULL, PORT, &peerTcp4Protocol);
                if (peerChildHandle)
                {
                    unsigned int salt;
                    _rdrand32_step(&salt);

                    unsigned long long clockTick = 0, systemDataSavingTick = 0, loggingTick = 0, peerRefreshingTick = 0, tickRequestingTick = 0;
                    unsigned int tickRequestingIndicator = 0, futureTickRequestingIndicator = 0;
                    while (!state)
                    {
                        if (criticalSituation == 1)
                        {
                            log(L"CRITICAL SITUATION #1!!!");
                        }

                        const unsigned long long curTimeTick = __rdtsc();

                        if (curTimeTick - clockTick >= (frequency >> 1))
                        {
                            clockTick = curTimeTick;
                                
                            updateTime();
                        }

                        if (contractProcessorState == 1)
                        {
                            contractProcessorState = 2;
                            bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, contractProcessorShutdownCallback, NULL, &contractProcessorEvent);
                            mpServicesProtocol->StartupThisAP(mpServicesProtocol, contractProcessor, computingProcessorNumber, contractProcessorEvent, MAX_CONTRACT_ITERATION_DURATION * 1000, NULL, NULL);
                        }
                        /*if (!computationProcessorState && (computation || __computation))
                        {
                            numberOfAllSCs++;
                            computationProcessorState = 1;
                            bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, shutdownCallback, NULL, &computationProcessorEvent);
                            if (status = mpServicesProtocol->StartupThisAP(mpServicesProtocol, computationProcessor, computingProcessorNumber, computationProcessorEvent, MAX_CONTRACT_ITERATION_DURATION * 1000, NULL, NULL))
                            {
                                numberOfNonLaunchedSCs++;
                                logStatus(L"EFI_MP_SERVICES_PROTOCOL.StartupThisAP() fails", status, __LINE__);
                            }
                        }*/

                        peerTcp4Protocol->Poll(peerTcp4Protocol);

                        for (unsigned int i = 0; i < NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS; i++)
                        {
                            if (((unsigned long long)peers[i].tcp4Protocol)
                                && peers[i].connectAcceptToken.CompletionToken.Status != -1)
                            {
                                peers[i].isConnectingAccepting = FALSE;

                                if (i < NUMBER_OF_OUTGOING_CONNECTIONS)
                                {
                                    if (peers[i].connectAcceptToken.CompletionToken.Status)
                                    {
                                        peers[i].connectAcceptToken.CompletionToken.Status = -1;
                                        forget(*((int*)peers[i].address));
                                        closePeer(&peers[i]);
                                    }
                                    else
                                    {
                                        peers[i].connectAcceptToken.CompletionToken.Status = -1;
                                        if (peers[i].isClosing)
                                        {
                                            closePeer(&peers[i]);
                                        }
                                        else
                                        {
                                            peers[i].isConnectedAccepted = TRUE;
                                        }
                                    }
                                }
                                else
                                {
                                    if (peers[i].connectAcceptToken.CompletionToken.Status)
                                    {
                                        peers[i].connectAcceptToken.CompletionToken.Status = -1;
                                        peers[i].tcp4Protocol = NULL;
                                    }
                                    else
                                    {
                                        peers[i].connectAcceptToken.CompletionToken.Status = -1;
                                        if (peers[i].isClosing)
                                        {
                                            closePeer(&peers[i]);
                                        }
                                        else
                                        {
                                            if (status = bs->OpenProtocol(peers[i].connectAcceptToken.NewChildHandle, &tcp4ProtocolGuid, (void**)&peers[i].tcp4Protocol, ih, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL))
                                            {
                                                logStatus(L"EFI_BOOT_SERVICES.OpenProtocol() fails", status, __LINE__);

                                                tcp4ServiceBindingProtocol->DestroyChild(tcp4ServiceBindingProtocol, peers[i].connectAcceptToken.NewChildHandle);
                                                peers[i].tcp4Protocol = NULL;
                                            }
                                            else
                                            {
                                                peers[i].isConnectedAccepted = TRUE;
                                            }
                                        }
                                    }
                                }

                                if (peers[i].isConnectedAccepted)
                                {
                                    ExchangePublicPeers* request = (ExchangePublicPeers*)&peers[i].dataToTransmit[sizeof(RequestResponseHeader)];
                                    bool noVerifiedPublicPeers = true;
                                    for (unsigned int k = 0; k < numberOfPublicPeers; k++)
                                    {
                                        if (publicPeers[k].isVerified)
                                        {
                                            noVerifiedPublicPeers = false;

                                            break;
                                        }
                                    }
                                    for (unsigned int j = 0; j < NUMBER_OF_EXCHANGED_PEERS; j++)
                                    {
                                        const unsigned int publicPeerIndex = random(numberOfPublicPeers);
                                        if (publicPeers[publicPeerIndex].isVerified || noVerifiedPublicPeers)
                                        {
                                            *((int*)request->peers[j]) = *((int*)publicPeers[publicPeerIndex].address);
                                        }
                                        else
                                        {
                                            j--;
                                        }
                                    }

                                    RequestResponseHeader* requestHeader = (RequestResponseHeader*)peers[i].dataToTransmit;
                                    requestHeader->setSize(sizeof(RequestResponseHeader) + sizeof(ExchangePublicPeers));
                                    requestHeader->randomizeDejavu();
                                    requestHeader->setType(EXCHANGE_PUBLIC_PEERS);
                                    peers[i].dataToTransmitSize = requestHeader->size();
                                    _InterlockedIncrement64(&numberOfDisseminatedRequests);

                                    if (!broadcastedComputors.broadcastComputors.computors.epoch
                                        || broadcastedComputors.broadcastComputors.computors.epoch != system.epoch)
                                    {
                                        requestedComputors.header.randomizeDejavu();
                                        bs->CopyMem(&peers[i].dataToTransmit[peers[i].dataToTransmitSize], &requestedComputors, requestedComputors.header.size());
                                        peers[i].dataToTransmitSize += requestedComputors.header.size();
                                        _InterlockedIncrement64(&numberOfDisseminatedRequests);
                                    }
                                }
                            }

                            if (((unsigned long long)peers[i].tcp4Protocol) > 1)
                            {
                                peers[i].tcp4Protocol->Poll(peers[i].tcp4Protocol);
                            }

                            if (((unsigned long long)peers[i].tcp4Protocol) > 1)
                            {
                                if (peers[i].receiveToken.CompletionToken.Status != -1)
                                {
                                    peers[i].isReceiving = FALSE;
                                    if (peers[i].receiveToken.CompletionToken.Status)
                                    {
                                        peers[i].receiveToken.CompletionToken.Status = -1;
                                        closePeer(&peers[i]);
                                    }
                                    else
                                    {
                                        peers[i].receiveToken.CompletionToken.Status = -1;
                                        if (peers[i].isClosing)
                                        {
                                            closePeer(&peers[i]);
                                        }
                                        else
                                        {
                                            numberOfReceivedBytes += peers[i].receiveData.DataLength;
                                            *((unsigned long long*)&peers[i].receiveData.FragmentTable[0].FragmentBuffer) += peers[i].receiveData.DataLength;

                                        iteration:
                                            unsigned int receivedDataSize = (unsigned int)(((unsigned long long)peers[i].receiveData.FragmentTable[0].FragmentBuffer) - ((unsigned long long)peers[i].receiveBuffer));

                                            if (receivedDataSize >= sizeof(RequestResponseHeader))
                                            {
                                                RequestResponseHeader* requestResponseHeader = (RequestResponseHeader*)peers[i].receiveBuffer;
                                                if (requestResponseHeader->size() < sizeof(RequestResponseHeader))
                                                {
                                                    setText(message, L"Forgetting ");
                                                    appendNumber(message, peers[i].address[0], FALSE);
                                                    appendText(message, L".");
                                                    appendNumber(message, peers[i].address[1], FALSE);
                                                    appendText(message, L".");
                                                    appendNumber(message, peers[i].address[2], FALSE);
                                                    appendText(message, L".");
                                                    appendNumber(message, peers[i].address[3], FALSE);
                                                    appendText(message, L"...");
                                                    forget(*((int*)peers[i].address));
                                                    closePeer(&peers[i]);
                                                }
                                                else
                                                {
                                                    if (receivedDataSize >= requestResponseHeader->size())
                                                    {
                                                        unsigned int saltedId;

                                                        const unsigned int header = *((unsigned int*)requestResponseHeader);
                                                        *((unsigned int*)requestResponseHeader) = salt;
                                                        KangarooTwelve((unsigned char*)requestResponseHeader, header & 0xFFFFFF, (unsigned char*)&saltedId, sizeof(saltedId));
                                                        *((unsigned int*)requestResponseHeader) = header;

                                                        if (!((dejavu0[saltedId >> 6] | dejavu1[saltedId >> 6]) & (1ULL << (saltedId & 63))))
                                                        {
                                                            if ((requestQueueBufferHead >= requestQueueBufferTail || requestQueueBufferHead + requestResponseHeader->size() < requestQueueBufferTail)
                                                                && (unsigned short)(requestQueueElementHead + 1) != requestQueueElementTail)
                                                            {
                                                                dejavu0[saltedId >> 6] |= (1ULL << (saltedId & 63));

                                                                requestQueueElements[requestQueueElementHead].offset = requestQueueBufferHead;
                                                                bs->CopyMem(&requestQueueBuffer[requestQueueBufferHead], peers[i].receiveBuffer, requestResponseHeader->size());
                                                                requestQueueBufferHead += requestResponseHeader->size();
                                                                requestQueueElements[requestQueueElementHead].peer = &peers[i];
                                                                if (requestQueueBufferHead > REQUEST_QUEUE_BUFFER_SIZE - BUFFER_SIZE)
                                                                {
                                                                    requestQueueBufferHead = 0;
                                                                }
                                                                // TODO: Place a fence
                                                                requestQueueElementHead++;

                                                                if (!(--dejavuSwapCounter))
                                                                {
                                                                    unsigned long long* tmp = dejavu1;
                                                                    dejavu1 = dejavu0;
                                                                    bs->SetMem(dejavu0 = tmp, 536870912, 0);
                                                                    dejavuSwapCounter = DEJAVU_SWAP_LIMIT;
                                                                }
                                                            }
                                                            else
                                                            {
                                                                _InterlockedIncrement64(&numberOfDiscardedRequests);
                                                            }
                                                        }
                                                        else
                                                        {
                                                            _InterlockedIncrement64(&numberOfDuplicateRequests);
                                                        }

                                                        bs->CopyMem(peers[i].receiveBuffer, ((char*)peers[i].receiveBuffer) + requestResponseHeader->size(), receivedDataSize -= requestResponseHeader->size());
                                                        peers[i].receiveData.FragmentTable[0].FragmentBuffer = ((char*)peers[i].receiveBuffer) + receivedDataSize;

                                                        goto iteration;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if (((unsigned long long)peers[i].tcp4Protocol) > 1)
                            {
                                if (!peers[i].isReceiving && peers[i].isConnectedAccepted && !peers[i].isClosing)
                                {
                                    if ((((unsigned long long)peers[i].receiveData.FragmentTable[0].FragmentBuffer) - ((unsigned long long)peers[i].receiveBuffer)) < BUFFER_SIZE)
                                    {
                                        peers[i].receiveData.DataLength = peers[i].receiveData.FragmentTable[0].FragmentLength = BUFFER_SIZE - (unsigned int)(((unsigned long long)peers[i].receiveData.FragmentTable[0].FragmentBuffer) - ((unsigned long long)peers[i].receiveBuffer));
                                        if (peers[i].receiveData.DataLength)
                                        {
                                            EFI_TCP4_CONNECTION_STATE state;
                                            if ((status = peers[i].tcp4Protocol->GetModeData(peers[i].tcp4Protocol, &state, NULL, NULL, NULL, NULL))
                                                || state == Tcp4StateClosed)
                                            {
                                                closePeer(&peers[i]);
                                            }
                                            else
                                            {
                                                if (status = peers[i].tcp4Protocol->Receive(peers[i].tcp4Protocol, &peers[i].receiveToken))
                                                {
                                                    if (status != EFI_CONNECTION_FIN)
                                                    {
                                                        logStatus(L"EFI_TCP4_PROTOCOL.Receive() fails", status, __LINE__);
                                                    }

                                                    closePeer(&peers[i]);
                                                }
                                                else
                                                {
                                                    peers[i].isReceiving = TRUE;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            if (((unsigned long long)peers[i].tcp4Protocol) > 1)
                            {
                                if (peers[i].transmitToken.CompletionToken.Status != -1)
                                {
                                    peers[i].isTransmitting = FALSE;
                                    if (peers[i].transmitToken.CompletionToken.Status)
                                    {
                                        peers[i].transmitToken.CompletionToken.Status = -1;
                                        closePeer(&peers[i]);
                                    }
                                    else
                                    {
                                        peers[i].transmitToken.CompletionToken.Status = -1;
                                        if (peers[i].isClosing)
                                        {
                                            closePeer(&peers[i]);
                                        }
                                        else
                                        {
                                            numberOfTransmittedBytes += peers[i].transmitData.DataLength;
                                        }
                                    }
                                }
                            }
                            if (((unsigned long long)peers[i].tcp4Protocol) > 1)
                            {
                                if (peers[i].dataToTransmitSize && !peers[i].isTransmitting && peers[i].isConnectedAccepted && !peers[i].isClosing)
                                {
                                    bs->CopyMem(peers[i].transmitData.FragmentTable[0].FragmentBuffer, peers[i].dataToTransmit, peers[i].transmitData.DataLength = peers[i].transmitData.FragmentTable[0].FragmentLength = peers[i].dataToTransmitSize);
                                    peers[i].dataToTransmitSize = 0;
                                    if (status = peers[i].tcp4Protocol->Transmit(peers[i].tcp4Protocol, &peers[i].transmitToken))
                                    {
                                        logStatus(L"EFI_TCP4_PROTOCOL.Transmit() fails", status, __LINE__);

                                        closePeer(&peers[i]);
                                    }
                                    else
                                    {
                                        peers[i].isTransmitting = TRUE;
                                    }
                                }
                            }

                            if (!peers[i].tcp4Protocol)
                            {
                                if (i < NUMBER_OF_OUTGOING_CONNECTIONS)
                                {
                                    *((int*)peers[i].address) = *((int*)publicPeers[random(numberOfPublicPeers)].address);

                                    unsigned int j;
                                    for (j = 0; j < NUMBER_OF_OUTGOING_CONNECTIONS; j++)
                                    {
                                        if (peers[j].tcp4Protocol && *((int*)peers[j].address) == *((int*)peers[i].address))
                                        {
                                            break;
                                        }
                                    }
                                    if (j == NUMBER_OF_OUTGOING_CONNECTIONS)
                                    {
                                        if (peers[i].connectAcceptToken.NewChildHandle = getTcp4Protocol(peers[i].address, PORT, &peers[i].tcp4Protocol))
                                        {
                                            peers[i].receiveData.FragmentTable[0].FragmentBuffer = peers[i].receiveBuffer;
                                            peers[i].dataToTransmitSize = 0;
                                            peers[i].isReceiving = FALSE;
                                            peers[i].isTransmitting = FALSE;
                                            peers[i].exchangedPublicPeers = FALSE;
                                            peers[i].isClosing = FALSE;

                                            if (status = peers[i].tcp4Protocol->Connect(peers[i].tcp4Protocol, (EFI_TCP4_CONNECTION_TOKEN*)&peers[i].connectAcceptToken))
                                            {
                                                logStatus(L"EFI_TCP4_PROTOCOL.Connect() fails", status, __LINE__);

                                                bs->CloseProtocol(peers[i].connectAcceptToken.NewChildHandle, &tcp4ProtocolGuid, ih, NULL);
                                                tcp4ServiceBindingProtocol->DestroyChild(tcp4ServiceBindingProtocol, peers[i].connectAcceptToken.NewChildHandle);
                                                peers[i].tcp4Protocol = NULL;
                                            }
                                            else
                                            {
                                                peers[i].isConnectingAccepting = TRUE;
                                            }
                                        }
                                        else
                                        {
                                            peers[i].tcp4Protocol = NULL;
                                        }
                                    }
                                }
                                else
                                {
                                    if (!listOfPeersIsStatic)
                                    {
                                        peers[i].receiveData.FragmentTable[0].FragmentBuffer = peers[i].receiveBuffer;
                                        peers[i].dataToTransmitSize = 0;
                                        peers[i].isReceiving = FALSE;
                                        peers[i].isTransmitting = FALSE;
                                        peers[i].exchangedPublicPeers = FALSE;
                                        peers[i].isClosing = FALSE;

                                        if (status = peerTcp4Protocol->Accept(peerTcp4Protocol, &peers[i].connectAcceptToken))
                                        {
                                            logStatus(L"EFI_TCP4_PROTOCOL.Accept() fails", status, __LINE__);
                                        }
                                        else
                                        {
                                            peers[i].isConnectingAccepting = TRUE;
                                            peers[i].tcp4Protocol = (EFI_TCP4_PROTOCOL*)1;
                                        }
                                    }
                                }
                            }
                        }

                        if (curTimeTick - systemDataSavingTick >= SYSTEM_DATA_SAVING_PERIOD * frequency / 1000)
                        {
                            systemDataSavingTick = curTimeTick;

                            saveSystem();
                        }

                        if (curTimeTick - peerRefreshingTick >= PEER_REFRESHING_PERIOD * frequency / 1000)
                        {
                            peerRefreshingTick = curTimeTick;

                            for (unsigned int i = 0; i < (NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS) / 4; i++)
                            {
                                closePeer(&peers[random(NUMBER_OF_OUTGOING_CONNECTIONS + NUMBER_OF_INCOMING_CONNECTIONS)]);
                            }
                        }

                        if (curTimeTick - tickRequestingTick >= TICK_REQUESTING_PERIOD * frequency / 1000)
                        {
                            tickRequestingTick = curTimeTick;

                            if (tickRequestingIndicator == tickTotalNumberOfComputors)
                            {
                                requestedQuorumTick.header.randomizeDejavu();
                                requestedQuorumTick.requestQuorumTick.quorumTick.tick = system.tick;
                                bs->SetMem(&requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags, sizeof(requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags), 0);
                                const unsigned int baseOffset = (system.tick - system.initialTick) * NUMBER_OF_COMPUTORS;
                                for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                                {
                                    const Tick* tick = &ticks[baseOffset + i];
                                    if (tick->epoch == system.epoch)
                                    {
                                        requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags[i >> 3] |= (1 << (i & 7));
                                    }
                                }
                                pushToAny(&requestedQuorumTick.header);
                            }
                            tickRequestingIndicator = tickTotalNumberOfComputors;
                            if (futureTickRequestingIndicator == futureTickTotalNumberOfComputors)
                            {
                                requestedQuorumTick.header.randomizeDejavu();
                                requestedQuorumTick.requestQuorumTick.quorumTick.tick = system.tick + 1;
                                bs->SetMem(&requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags, sizeof(requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags), 0);
                                const unsigned int baseOffset = (system.tick + 1 - system.initialTick) * NUMBER_OF_COMPUTORS;
                                for (unsigned int i = 0; i < NUMBER_OF_COMPUTORS; i++)
                                {
                                    const Tick* tick = &ticks[baseOffset + i];
                                    if (tick->epoch == system.epoch)
                                    {
                                        requestedQuorumTick.requestQuorumTick.quorumTick.voteFlags[i >> 3] |= (1 << (i & 7));
                                    }
                                }
                                pushToAny(&requestedQuorumTick.header);
                            }
                            futureTickRequestingIndicator = futureTickTotalNumberOfComputors;
                            
                            if (tickData[system.tick + 1 - system.initialTick].epoch != system.epoch
                                || targetNextTickDataDigestIsKnown)
                            {
                                requestedTickData.header.randomizeDejavu();
                                requestedTickData.requestTickData.requestedTickData.tick = system.tick + 1;
                                pushToAny(&requestedTickData.header);
                            }
                            if (tickData[system.tick + 2 - system.initialTick].epoch != system.epoch)
                            {
                                requestedTickData.header.randomizeDejavu();
                                requestedTickData.requestTickData.requestedTickData.tick = system.tick + 2;
                                pushToAny(&requestedTickData.header);
                            }

                            if (requestedTickTransactions.requestedTickTransactions.tick)
                            {
                                requestedTickTransactions.header.randomizeDejavu();
                                pushToAny(&requestedTickTransactions.header);

                                requestedTickTransactions.requestedTickTransactions.tick = 0;
                            }
                        }

                        const unsigned short responseQueueElementHead = ::responseQueueElementHead;
                        if (responseQueueElementTail != responseQueueElementHead)
                        {
                            while (responseQueueElementTail != responseQueueElementHead)
                            {
                                RequestResponseHeader* responseHeader = (RequestResponseHeader*)&responseQueueBuffer[responseQueueElements[responseQueueElementTail].offset];
                                if (responseQueueElements[responseQueueElementTail].peer)
                                {
                                    push(responseQueueElements[responseQueueElementTail].peer, responseHeader);
                                }
                                else
                                {
                                    pushToSeveral(responseHeader);
                                }
                                responseQueueBufferTail += responseHeader->size();
                                if (responseQueueBufferTail > RESPONSE_QUEUE_BUFFER_SIZE - BUFFER_SIZE)
                                {
                                    responseQueueBufferTail = 0;
                                }
                                // TODO: Place a fence
                                responseQueueElementTail++;
                            }
                        }

                        if (systemMustBeSaved)
                        {
                            systemMustBeSaved = false;
                            saveSystem();
                        }
                        if (spectrumMustBeSaved)
                        {
                            spectrumMustBeSaved = false;
                            saveSpectrum();
                        }
                        if (universeMustBeSaved)
                        {
                            universeMustBeSaved = false;
                            saveUniverse();
                        }
                        if (computerMustBeSaved)
                        {
                            computerMustBeSaved = false;
                            saveComputer();
                        }

                        processKeyPresses();

                        if (curTimeTick - loggingTick >= frequency)
                        {
                            loggingTick = curTimeTick;

                            logInfo();

                            if (mainLoopDenominator)
                            {
                                setText(message, L"Main loop duration = ");
                                appendNumber(message, (mainLoopNumerator / mainLoopDenominator) * 1000000 / frequency, TRUE);
                                appendText(message, L" mcs.");
                                log(message);
                            }
                            mainLoopNumerator = 0;
                            mainLoopDenominator = 0;

                            if (tickerLoopDenominator)
                            {
                                setText(message, L"Ticker loop duration = ");
                                appendNumber(message, (tickerLoopNumerator / tickerLoopDenominator) * 1000000 / frequency, TRUE);
                                appendText(message, L" microseconds. Latest created tick = ");
                                appendNumber(message, system.latestCreatedTick, TRUE);
                                appendText(message, L".");
                                log(message);
                            }
                            tickerLoopNumerator = 0;
                            tickerLoopDenominator = 0;
                        }
                        else
                        {
                            mainLoopNumerator += __rdtsc() - curTimeTick;
                            mainLoopDenominator++;
                        }
                    }

                    bs->CloseProtocol(peerChildHandle, &tcp4ProtocolGuid, ih, NULL);
                    tcp4ServiceBindingProtocol->DestroyChild(tcp4ServiceBindingProtocol, peerChildHandle);

                    saveSystem();

                    setText(message, L"Qubic ");
                    appendQubicVersion(message);
                    appendText(message, L" is shut down.");
                    log(message);
                }
            }
        }
    }
    else
    {
        log(L"Initialization fails!");
    }

    deinitialize();

    bs->Stall(1000000);
    if (!state)
    {
        st->ConIn->Reset(st->ConIn, FALSE);
        unsigned long long eventIndex;
        bs->WaitForEvent(1, &st->ConIn->WaitForKey, &eventIndex);
    }

	return EFI_SUCCESS;
}
