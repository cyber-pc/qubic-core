#include <intrin.h>
#include "../../src/platform/uefi.h"


static volatile char logMessageLock = 0;

#include "../../src/platform/time.h"
#include "../../src/platform/file_io.h"
#include "../../src/platform/time_stamp_counter.h"
#include "../../src/platform/concurrency.h"

#include "../../src/text_output.h"
#include "../../src/platform/console_logging.h"


#include "../../src/kangaroo_twelve.h"
#include "../../src/four_q.h"

// Change the number of processors use for testing
#define NUMBER_TEST_PROCESSORS 8


#define LOOP_COUNT_TEST 10000
#define LOOP_COUNT_TEST_SMALL 1000
#define MAX_NUMBER_TEST_PROCESSORS 256
static constexpr unsigned long long MEM_BUFFER_SIZE = 52ULL * 1024ULL * 1024ULL;
typedef struct
{
    bool isBSProc;
    unsigned long long id;
    unsigned int StatusFlag;

    unsigned int package;
    unsigned int core;
    unsigned int thread;

} Processor;

static EFI_EVENT events[MAX_NUMBER_TEST_PROCESSORS];

static volatile int shutDownNode = 0;
static EFI_MP_SERVICES_PROTOCOL* gpServicesProtocol;
static unsigned long long gNumberOfAllProcessors = 0;
//static volatile char logMessageLock = 0;

static Processor processors[MAX_NUMBER_TEST_PROCESSORS];

static char gProcessorLock[MAX_NUMBER_TEST_PROCESSORS];
static char gProcessorReady[MAX_NUMBER_TEST_PROCESSORS];
static char gProcessorResult[MAX_NUMBER_TEST_PROCESSORS];
static unsigned long long gBSProc = 0;

struct
{
    unsigned long long count;
    unsigned long long totalProcessingTime; // ms
    unsigned long long avgProcessingTime;
    unsigned long long totalSizeInMB;

    void reset()
    {
        count = 0;
        totalProcessingTime = 0;
        avgProcessingTime = 0;
        totalSizeInMB = 0;
    }

} gProfiles[MAX_NUMBER_TEST_PROCESSORS];

enum TestName
{
    MEMCPY_SINGLE_THREAD_ONE_CHUNK = 0,
    MEMCPY_SINGLE_THREAD_FULL_COPY,
    MEMCPY_SINGLE_THREAD_MANY_CHUNKS,

    MEMCPY_MULTITHREADS_ONE_CHUNK,
    MEMCPY_MULTITHREADS_MANY_CHUNKS,
    MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS,
    MAX_TEST
};

static unsigned int gTestCases[] = {
    MEMCPY_SINGLE_THREAD_ONE_CHUNK,
    MEMCPY_SINGLE_THREAD_FULL_COPY,
    MEMCPY_SINGLE_THREAD_MANY_CHUNKS,
    MEMCPY_MULTITHREADS_ONE_CHUNK,
    MEMCPY_MULTITHREADS_MANY_CHUNKS,
    MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS,
};
static CHAR16 gTestCasesString[MAX_TEST][256];

static unsigned int gCurrentTestCase = gTestCases[0];

static void logToConsole(const CHAR16* message)
{
    timestampedMessage[0] = (utcTime.Year % 100) / 10 + L'0';
    timestampedMessage[1] = utcTime.Year % 10 + L'0';
    timestampedMessage[2] = utcTime.Month / 10 + L'0';
    timestampedMessage[3] = utcTime.Month % 10 + L'0';
    timestampedMessage[4] = utcTime.Day / 10 + L'0';
    timestampedMessage[5] = utcTime.Day % 10 + L'0';
    timestampedMessage[6] = utcTime.Hour / 10 + L'0';
    timestampedMessage[7] = utcTime.Hour % 10 + L'0';
    timestampedMessage[8] = utcTime.Minute / 10 + L'0';
    timestampedMessage[9] = utcTime.Minute % 10 + L'0';
    timestampedMessage[10] = utcTime.Second / 10 + L'0';
    timestampedMessage[11] = utcTime.Second % 10 + L'0';
    timestampedMessage[12] = ' ';
    timestampedMessage[13] = 0;

    appendText(timestampedMessage, message);
    appendText(timestampedMessage, L"\r\n");

    outputStringToConsole(timestampedMessage);
}

static void enableAVX()
{
    __writecr4(__readcr4() | 0x40000);
    _xsetbv(_XCR_XFEATURE_ENABLED_MASK, _xgetbv(_XCR_XFEATURE_ENABLED_MASK) | (7
#ifdef __AVX512F__
        | 224
#endif
        ));
}

static constexpr unsigned long long CHUNK_SIZE = (1ULL << 30);
static constexpr unsigned long long MEMORY_REGIONS_COUNT = 32;
static constexpr unsigned long long ITERATIONS = 32;

unsigned char* gMemorySrc[MEMORY_REGIONS_COUNT] = { NULL };
unsigned char* gMemoryDest[MEMORY_REGIONS_COUNT] = { NULL };

unsigned char* gContinuousMemorySrc = NULL;
unsigned char* gContinuousMemoryDst = NULL;
unsigned long long gTestSizeInMB = 0;

static_assert(CHUNK_SIZE % 8 == 0, "MEMORY_TEST_SIZE % 8 == 0");

static bool initialize()
{
    enableAVX();

    initTimeStampCounter();

    return true;
}

static bool initMemcpyTest()
{

    return true;
}

static bool initTest()
{
    for (int i = 0; i < MAX_NUMBER_TEST_PROCESSORS; i++)
    {
        gProcessorLock[i] = 0;
        gProcessorReady[i] = 0;
        gProcessorResult[i] = 0;
    }

    setText(gTestCasesString[MEMCPY_SINGLE_THREAD_ONE_CHUNK], L"MEMCPY_SINGLE_THREAD_ONE_CHUNK");
    setText(gTestCasesString[MEMCPY_SINGLE_THREAD_MANY_CHUNKS], L"MEMCPY_SINGLE_THREAD_MANY_CHUNKS");
    setText(gTestCasesString[MEMCPY_MULTITHREADS_ONE_CHUNK], L"MEMCPY_MULTITHREADS_ONE_CHUNK");
    setText(gTestCasesString[MEMCPY_MULTITHREADS_MANY_CHUNKS], L"MEMCPY_MULTITHREADS_MANY_CHUNKS");
    setText(gTestCasesString[MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS], L"MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS"); 
    setText(gTestCasesString[MEMCPY_SINGLE_THREAD_FULL_COPY], L"MEMCPY_SINGLE_THREAD_FULL_COPY");

    if (!initMemcpyTest())
    {
        return false;
    }

    return true;
}

static void deinitMemcpyTest()
{
    for (int i = 0; i < MEMORY_REGIONS_COUNT; i++)
    {
        if (gMemorySrc[i] != NULL)
        {
            freePool(gMemorySrc[i]);
        }
        if (gMemoryDest[i] != NULL)
        {
            freePool(gMemoryDest[i]);
        }
    }
    if (gContinuousMemorySrc != NULL)
    {
        freePool(gContinuousMemorySrc);
    }
    if (gContinuousMemoryDst != NULL)
    {
        freePool(gContinuousMemoryDst);
    }
}

static void deinitialize()
{
    deinitMemcpyTest();
}

inline static unsigned int random(const unsigned int range)
{
    unsigned int value;
    _rdrand32_step(&value);

    return value % range;
}

inline static unsigned long long random64(const unsigned long long range)
{
    unsigned long long value;
    _rdrand64_step(&value);

    return (value % range);
}

bool runCopySingleChunk(int id)
{
    // Copy
    unsigned long long startTSC = __rdtsc();

    for (unsigned long long i = 0; i < ITERATIONS; i++)
    {
        copyMem(gMemoryDest[0], gMemorySrc[0], CHUNK_SIZE);
    }

    unsigned long long endTSC = __rdtsc();

    unsigned long long total_cycles = endTSC - startTSC;

    gProfiles[id].count = ITERATIONS;
    gProfiles[id].totalProcessingTime = total_cycles;
    gProfiles[id].totalSizeInMB = ((ITERATIONS * CHUNK_SIZE) >> 20);

    return true;
}

bool runCopyFull(int id)
{
    unsigned long long startTSC = __rdtsc();

    //for (unsigned long long i = 0; i < ITERATIONS; i++)
    {
        copyMem(gContinuousMemoryDst, gContinuousMemorySrc, CHUNK_SIZE * MEMORY_REGIONS_COUNT);
    }

    unsigned long long endTSC = __rdtsc();

    unsigned long long total_cycles = endTSC - startTSC;

    gProfiles[id].count = ITERATIONS;
    gProfiles[id].totalProcessingTime = total_cycles;
    gProfiles[id].totalSizeInMB = ((CHUNK_SIZE * MEMORY_REGIONS_COUNT) >> 20);
    return true;
}

bool verifyCopySingleChunk()
{
    // Copy
    for (unsigned long long j = 0; j < CHUNK_SIZE; j++)
    {
        if (gMemoryDest[0][j] != gMemorySrc[0][j])
        {
            return false;
        }
    }
    return true;
}

bool runCopyMultipleChunks(int id)
{
    // Copy
    unsigned long long startTSC = __rdtsc();

    //for (int i = 0; i < ITERATIONS; i++)
    {
        for (int j = 0; j < MEMORY_REGIONS_COUNT; j++)
        {
            copyMem(gMemoryDest[j], gMemorySrc[j], CHUNK_SIZE);
        }
    }

    unsigned long long endTSC = __rdtsc();

    unsigned long long total_cycles = endTSC - startTSC;

    gProfiles[id].count = ITERATIONS;
    gProfiles[id].totalProcessingTime = total_cycles;
    gProfiles[id].totalSizeInMB = ((CHUNK_SIZE * MEMORY_REGIONS_COUNT) >> 20);

    return true;
}

bool runCopyMultipleChunksMultiThread(int id)
{
    // Copy
    for (int j = id; j < MEMORY_REGIONS_COUNT; j += gNumberOfAllProcessors)
    {
        copyMem(gMemoryDest[j], gMemorySrc[j], CHUNK_SIZE);
    }

    return true;
}

bool runCopySingleChunksMultiThread(int id)
{
    // Copy
    for (int i = 0; i < (MEMORY_REGIONS_COUNT / gNumberOfAllProcessors); i++)
    {
        copyMem(gMemoryDest[id], gMemorySrc[id], CHUNK_SIZE);
    }

    return true;
}

bool verifyCopyMultipleChunks()
{
    // Copy
    for (int i = 0; i < MEMORY_REGIONS_COUNT; i++)
    {
        for (unsigned long long j = 0; j < CHUNK_SIZE; j++)
        {
            if (gMemoryDest[i][j] != gMemorySrc[i][j])
            {
                return false;
            }
        }
    }
    return true;
}

bool verifyCopyMultipleThreadSingleChunk()
{
    for (int i = 0; i < gNumberOfAllProcessors; i++)
    {
        for (unsigned long long j = 0; j < CHUNK_SIZE; j++)
        {
            if (gMemoryDest[i][j] != gMemorySrc[i][j])
            {
                return false;
            }
        }
    }
    return true;
}

bool verifyCopyMultipleContinuousChunks()
{
    for (unsigned long long i = 0; i < MEMORY_REGIONS_COUNT * CHUNK_SIZE; i++)
    {
        if (gContinuousMemoryDst[i] != gContinuousMemorySrc[i])
        {
            return false;
        }
    }
    return true;
}

bool runCopyContinuousMultipleChunksMultiThread(int id)
{
    // Compute total memory size in bytes
    unsigned long long totalSize = MEMORY_REGIONS_COUNT * CHUNK_SIZE;

    // Divide total size among threads
    unsigned long long baseSize = totalSize / gNumberOfAllProcessors;
    unsigned long long remainder = totalSize % gNumberOfAllProcessors;

    // Each thread gets 'baseSize' bytes
    // The last thread gets the remainder too
    unsigned long long startOffset = id * baseSize;
    unsigned long long endOffset = startOffset + baseSize;

    // Give remainder to the last thread
    if (id == gNumberOfAllProcessors - 1)
        endOffset += remainder;

    // Clamp (just in case)
    if (endOffset > totalSize)
    {
        endOffset = totalSize;
    }

    // Copy continuous region
    for (unsigned long long offset = startOffset; offset < endOffset; offset += CHUNK_SIZE)
    {
        unsigned long long remaining = endOffset - offset;
        unsigned long long sizeToCopy = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;

        copyMem(gContinuousMemoryDst + offset, gContinuousMemorySrc + offset, sizeToCopy);
    }
    return true;

}

bool prepareTest()
{
    // Randomly fill the MEMORY_TEST_SIZE
    bool initContinuousMem = false;

    switch (gCurrentTestCase)
    {
    case MEMCPY_SINGLE_THREAD_ONE_CHUNK:
        gTestSizeInMB = ((MEMORY_REGIONS_COUNT * CHUNK_SIZE * ITERATIONS) >> 20);
        break;
    case MEMCPY_SINGLE_THREAD_MANY_CHUNKS:
        gTestSizeInMB = ((MEMORY_REGIONS_COUNT * CHUNK_SIZE) >> 20);
        break;
    case MEMCPY_MULTITHREADS_MANY_CHUNKS:
        gTestSizeInMB = ((MEMORY_REGIONS_COUNT * CHUNK_SIZE) >> 20);
        break;
    case MEMCPY_MULTITHREADS_ONE_CHUNK:
        gTestSizeInMB = ((CHUNK_SIZE * gNumberOfAllProcessors * (MEMORY_REGIONS_COUNT / gNumberOfAllProcessors)) >> 20);
        break;
    case MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS:
        gTestSizeInMB = ((CHUNK_SIZE * MEMORY_REGIONS_COUNT) >> 20);
        initContinuousMem = true;
        break;
    case MEMCPY_SINGLE_THREAD_FULL_COPY:
        gTestSizeInMB = ((CHUNK_SIZE * MEMORY_REGIONS_COUNT) >> 20);
        initContinuousMem = true;
        break;
    default:
        break;
    }

    if (initContinuousMem)
    {
        // Continuous mem
        allocatePool(MEMORY_REGIONS_COUNT * CHUNK_SIZE, (void**)&gContinuousMemorySrc);
        allocatePool(MEMORY_REGIONS_COUNT * CHUNK_SIZE, (void**)&gContinuousMemoryDst);
        setMem(gContinuousMemorySrc, MEMORY_REGIONS_COUNT * CHUNK_SIZE, 0);
        setMem(gContinuousMemoryDst, MEMORY_REGIONS_COUNT * CHUNK_SIZE, 0);

        for (unsigned long long i = 0; i < MEMORY_REGIONS_COUNT * CHUNK_SIZE; i += 8)
        {
            _rdrand64_step((unsigned long long*)(gContinuousMemoryDst + i));
            _rdrand64_step((unsigned long long*)(gContinuousMemorySrc + i));
        }
    }
    else
    {
        for (int i = 0; i < MEMORY_REGIONS_COUNT; i++)
        {
            // Allocate bigger so that assure memory not continuous
            allocatePool(3 * CHUNK_SIZE / 2, (void**)&gMemorySrc[i]);
            allocatePool(3 * CHUNK_SIZE / 2, (void**)&gMemoryDest[i]);

            setMem(gMemorySrc[i], 3 * CHUNK_SIZE / 2, 0);
            setMem(gMemoryDest[i], 3 * CHUNK_SIZE / 2, 0);
        }

        for (unsigned long long i = 0; i < MEMORY_REGIONS_COUNT; i++)
        {
            for (unsigned long long j = 0; j < CHUNK_SIZE; j += 8)
            {
                _rdrand64_step((unsigned long long*)(gMemorySrc[i] + j));
                _rdrand64_step((unsigned long long*)(gMemoryDest[i] + j));
            }
        }
    }

    return true;
}

bool finalizeTest()
{
    logToConsole(L"Finalizing test...");
    if (gCurrentTestCase == MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS
        || gCurrentTestCase == MEMCPY_SINGLE_THREAD_FULL_COPY)
    {
        if (gContinuousMemorySrc != NULL)
        {
            freePool(gContinuousMemorySrc);
            gContinuousMemorySrc = NULL;
        }
        if (gContinuousMemoryDst != NULL)
        {
            freePool(gContinuousMemoryDst);
            gContinuousMemoryDst = NULL;
        }
    }
    else
    {
        for (int i = 0; i < MEMORY_REGIONS_COUNT; i++)
        {
            if (gMemorySrc[i] != NULL)
            {
                freePool(gMemorySrc[i]);
                gMemorySrc[i] = NULL;
            }
            if (gMemoryDest[i] != NULL)
            {
                freePool(gMemoryDest[i]);
                gMemoryDest[i] = NULL;
            }
        }
    }
    

    return true;
}

bool verifyResult()
{
    bool testResult = true;

    // Test the save file
    switch (gCurrentTestCase)
    {
    case MEMCPY_SINGLE_THREAD_ONE_CHUNK:
        testResult = verifyCopySingleChunk();
        break;
    case MEMCPY_SINGLE_THREAD_MANY_CHUNKS:
        testResult = verifyCopyMultipleChunks();
        break;
    case MEMCPY_MULTITHREADS_MANY_CHUNKS:
        testResult = verifyCopyMultipleChunks();
        break;
    case MEMCPY_MULTITHREADS_ONE_CHUNK:
        testResult = verifyCopyMultipleThreadSingleChunk();
        break;
    case MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS:
        testResult = verifyCopyMultipleContinuousChunks();
        break;
    case MEMCPY_SINGLE_THREAD_FULL_COPY:
        testResult = verifyCopyMultipleContinuousChunks();
        break;
    default:
        break;
    }

    return testResult;
}

// Main test function for each processor
void threadRun(void* processId)
{
    unsigned long long processorNumber;
    gpServicesProtocol->WhoAmI(gpServicesProtocol, &processorNumber);

    int id = processorNumber;
    bool testResult = true;

    ACQUIRE(gProcessorLock[id]);
    gProcessorReady[id] = 0;
    RELEASE(gProcessorLock[id]);

    // Test the save file
    switch (gCurrentTestCase)
    {
        case MEMCPY_SINGLE_THREAD_FULL_COPY:
            testResult = runCopyFull(id);
            break;
        case MEMCPY_SINGLE_THREAD_ONE_CHUNK:
            testResult = runCopySingleChunk(id);
            break;
        case MEMCPY_SINGLE_THREAD_MANY_CHUNKS:
            testResult = runCopyMultipleChunks(id);
            break;
        case MEMCPY_MULTITHREADS_MANY_CHUNKS:
            testResult = runCopyMultipleChunksMultiThread(id);
            break;
        case MEMCPY_MULTITHREADS_ONE_CHUNK:
            testResult = runCopySingleChunksMultiThread(id);
            break;
        case MEMCPY_MULTITHREADS_MANY_CHUNKS_CONTINUOUS:
            testResult = runCopyContinuousMultipleChunksMultiThread(id);
            break;
        default:
            break;
    }

    gProcessorResult[id] = testResult ? 1 : 0;

    ACQUIRE(gProcessorLock[id]);
    gProcessorReady[id] = 1;
    RELEASE(gProcessorLock[id]);
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
            */
        case 0x0C:
        {
            logToConsole(L"Pressed F2 key. ");

        }
        break;
        /*
       *
       * F3 Key
       */
        case 0x0D:
        {
            logToConsole(L"Pressed F2 key.");
        }
        break;
        /*
        * F4 Key
        */
        case 0x0E:
        {
            logToConsole(L"Pressed F4 key.");
        }
        break;
        /*
        * ESC Key
        * By Pressing the ESC Key the node will stop
        */
        case 0x17:
        {
            shutDownNode = 1;
        }
        break;
        default:
            setText(message, L"Press ");
            appendNumber(message, key.ScanCode, false);
            logToConsole(message);
        }
    }
}

static void shutdownCallback(EFI_EVENT Event, void* Context)
{
    bs->CloseEvent(Event);
}

void processorEventCallback(EFI_EVENT Event, void* Context)
{

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
    logToConsole(message);

    EFI_STATUS status;
    if (initialize())
    {
        logToConsole(L"Setting up multiprocessing ...");

        CHAR16 loginfo[512];

        // MP service protocol
        unsigned int computingProcessorNumber;
        EFI_GUID mpServiceProtocolGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
        status = bs->LocateProtocol(&mpServiceProtocolGuid, NULL, (void**)&gpServicesProtocol);
        if (EFI_SUCCESS != status)
        {
            logToConsole(L"Can not locate MP_SERVICES_PROTOCOL");
        }

        // Get number of processers and enabled processors
        unsigned long long  numberOfEnabledProcessors;
        status = gpServicesProtocol->GetNumberOfProcessors(gpServicesProtocol, &gNumberOfAllProcessors, &numberOfEnabledProcessors);
        if (EFI_SUCCESS != status)
        {
            logToConsole(L"Can not get number of processors.");
        }
        setText(loginfo, L"Enabled processors: ");
        appendNumber(loginfo, numberOfEnabledProcessors, false);
        appendText(loginfo, L" / ");
        appendNumber(loginfo, gNumberOfAllProcessors, false);

        gNumberOfAllProcessors = gNumberOfAllProcessors > NUMBER_TEST_PROCESSORS ? NUMBER_TEST_PROCESSORS : gNumberOfAllProcessors;
        appendText(loginfo, L". Using ");
        appendNumber(loginfo, gNumberOfAllProcessors, false);
        appendText(loginfo, L" processors ");
        logToConsole(loginfo);

        // Processor health and location
        int bsProcID = 0;
        for (int i = 0; i < gNumberOfAllProcessors; i++)
        {
            EFI_PROCESSOR_INFORMATION procInfo;
            status = gpServicesProtocol->GetProcessorInfo(gpServicesProtocol, i, &procInfo);
            processors[i].id = procInfo.ProcessorId;
            processors[i].StatusFlag = procInfo.StatusFlag;
            if (procInfo.StatusFlag & 0x1)
            {
                processors[i].isBSProc = true;
                bsProcID = i;
                gBSProc = bsProcID;
            }
            else
            {
                processors[i].isBSProc = false;
            }

            EFI_CPU_PHYSICAL_LOCATION cpuLocation = procInfo.Location;
            processors[i].package = cpuLocation.Package;
            processors[i].core = cpuLocation.Core;
            processors[i].thread = cpuLocation.Thread;
        }

        {
            setText(loginfo, L"BS Processor ");
            appendText(loginfo, L"id: ");
            appendNumber(loginfo, bsProcID, false);
            logToConsole(loginfo);
        }


        // Init test
        initTest();

        int testSuccessCount = 0;
        int testCount = sizeof(gTestCases) / sizeof(gTestCases[0]);

        // Run the tests
        for (int test = 0; test < testCount; test++)
        {
            gCurrentTestCase = gTestCases[test];

            setText(loginfo, L"Trigger test ");
            appendNumber(loginfo, test, false);
            appendText(loginfo, L": ");
            appendText(loginfo, gTestCasesString[gCurrentTestCase]);
            logToConsole(loginfo);

            // Prepare the test
            logToConsole(L"  - Preparing test...");
            if (!prepareTest())
            {
                continue;
            }

            // Run the test
            setMem(gProcessorReady, gNumberOfAllProcessors * sizeof(gProcessorReady[0]), 1);
            setMem(gProcessorResult, gNumberOfAllProcessors * sizeof(gProcessorResult[0]), 0);
            if (gCurrentTestCase != MEMCPY_SINGLE_THREAD_ONE_CHUNK 
                && gCurrentTestCase != MEMCPY_SINGLE_THREAD_MANY_CHUNKS
                && gCurrentTestCase != MEMCPY_SINGLE_THREAD_FULL_COPY)
            {
                logToConsole(L"  - Running multithread test...");

                unsigned long long startTick = __rdtsc();
                unsigned long long eventsCount = 0;
                // Start the task for all application Proccessor
                for (int i = 0; i < gNumberOfAllProcessors; i++)
                {
                    // Start the task if it is an AP
                    if (!processors[i].isBSProc)
                    {
                        status = bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, processorEventCallback, NULL, &events[eventsCount]);
                        if (status != EFI_SUCCESS)
                        {
                            setText(loginfo, L"Event ");
                            appendNumber(loginfo, i, false);
                            appendText(loginfo, L" is failed to created.");
                            logToConsole(loginfo);
                        }
                        status = gpServicesProtocol->StartupThisAP(gpServicesProtocol, threadRun, i, events[eventsCount], 0, &i, NULL);
                        if (status != EFI_SUCCESS)
                        {
                            setText(loginfo, L"Process ");
                            appendNumber(loginfo, i, false);
                            appendText(loginfo, L" is failed to start.");
                            logToConsole(loginfo);
                        }
                        eventsCount++;
                    }
                }

                // Start the test with main processor
                threadRun(&bsProcID);
                // Wait for all task is done
                int readyCount = 0;
                while (readyCount < gNumberOfAllProcessors)
                {
                    readyCount = 0;
                    for (int i = 0; i < gNumberOfAllProcessors; i++)
                    {
                        char readyFlag = 0;
                        ACQUIRE(gProcessorLock[i]);
                        readyFlag = gProcessorReady[i];
                        RELEASE(gProcessorLock[i]);
                        if (readyFlag)
                        {
                            readyCount++;
                        }
                    }
                }
                unsigned long long endTick = __rdtsc();
                unsigned long long total_cycles = endTick - startTick;

                logToConsole(L"  - All tasks done...");


                unsigned long long totalProcessingTime = total_cycles;
                setText(loginfo, L"  - Profile.");

                appendText(loginfo, L" TotalSize: ");
                appendNumber(loginfo, gTestSizeInMB, true);
                appendText(loginfo, L" MB.");

                long long copyTimeMs = totalProcessingTime * 1000 / frequency;
                appendText(loginfo, L" CopyTime: ");
                appendNumber(loginfo, copyTimeMs, false);
                appendText(loginfo, L" ms.");
                if (copyTimeMs > 0)
                {
                    long long copyRate = gTestSizeInMB * 1000 / copyTimeMs ;
                    appendText(loginfo, L" CopyRate: ");
                    appendNumber(loginfo, copyRate, true);
                    appendText(loginfo, L" MB/s.");
                }
                else
                {
                    appendText(loginfo, L" NA");
                }

                logToConsole(loginfo);

                // Close all events
                for (int k = 0; k < eventsCount; k++)
                {
                    bs->CloseEvent(events[k]);
                }

            }
            else // single thread test
            {
                logToConsole(L"  - Running test...");

                // Start the test with main processor
                gProfiles[bsProcID].reset();
                setMem(gProcessorResult, gNumberOfAllProcessors * sizeof(gProcessorResult[0]), 1);
                threadRun(&bsProcID);

                setText(loginfo, L"  - Profile.");

                appendText(loginfo, L" TotalSize: ");
                appendNumber(loginfo, gProfiles[bsProcID].totalSizeInMB, true);
                appendText(loginfo, L" MB.");
                
                long long copyTimeMs = gProfiles[bsProcID].totalProcessingTime * 1000 / frequency;
                appendText(loginfo, L" CopyTime: ");
                appendNumber(loginfo, copyTimeMs, false);
                appendText(loginfo, L" ms.");

                appendText(loginfo, L" CopyRate: ");
                if (copyTimeMs > 0)
                {
                    long long copyRate = gProfiles[bsProcID].totalSizeInMB * 1000 / copyTimeMs ;
                    appendNumber(loginfo, copyRate , true);
                    appendText(loginfo, L" MB/s.");
                }
                else
                {
                    appendText(loginfo, L" NA");
                }

                logToConsole(loginfo);
            }

            logToConsole(L"  - Verifying result...");

            // Verify results
            bool sts = verifyResult();
            if (sts)
            {
                testSuccessCount++;
            }

            // Finalize the test
            finalizeTest();

        }
        // Show test result
        setText(loginfo, L"Passed tests: ");
        appendNumber(loginfo, testSuccessCount, false);
        appendText(loginfo, L" / ");
        appendNumber(loginfo, testCount, false);
        logToConsole(loginfo);

        setText(loginfo, L"Multi-thread test DONE. Press F2, F3, F4 to do more agressive test");
        logToConsole(loginfo);

        // -----------------------------------------------------
        // Wait for more test
        logToConsole(L"Tests DONE. Waiting for key press...");
        while (!shutDownNode)
        {
            processKeyPresses();
        }

    }
    else
    {
        logToConsole(L"Initialization fails!");
    }


    deinitialize();

    bs->Stall(1000000);
    if (!shutDownNode)
    {
        st->ConIn->Reset(st->ConIn, FALSE);
        unsigned long long eventIndex;
        bs->WaitForEvent(1, &st->ConIn->WaitForKey, &eventIndex);
    }

    return EFI_SUCCESS;
}

