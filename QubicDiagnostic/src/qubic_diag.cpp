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
#define NUMBER_TEST_PROCESSORS 16


#define LOOP_COUNT_TEST 10000
#define LOOP_COUNT_TEST_SMALL 1000
#define MAX_NUMBER_TEST_PROCESSORS 32
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


// For testing the scheduler save file
struct SaveFileTestData
{
    // Reserve memories
    unsigned int memBuffer[MEM_BUFFER_SIZE];

    // Randomly parition of data for writing
    unsigned long long dataPos[4][2];
};

enum TestName
{
    WRITE_FILE = 0,
    WRITE_LARGE_FILE,
    READ_FILE,
    READ_LARGE_FILE,
    ASYNC_WRITE_FILE,
    ASYNC_WRITE_LARGE_FILE,
    ASYNC_BLOCKING_WRITE_FILE,
    ASYNC_BLOCKING_WRITE_LARGE_FILE,
    ASYNC_READ_FILE,
    ASYNC_READ_LARGE_FILE,
    MAX_TEST
};

static unsigned int gTestCases[] = {
    WRITE_FILE,
    WRITE_LARGE_FILE,
    READ_FILE,
    READ_LARGE_FILE,
    ASYNC_WRITE_FILE,
    ASYNC_WRITE_LARGE_FILE,
    ASYNC_BLOCKING_WRITE_FILE,
    ASYNC_BLOCKING_WRITE_LARGE_FILE,
    ASYNC_READ_FILE,
    ASYNC_READ_LARGE_FILE
};
static CHAR16 gTestCasesString[MAX_TEST][256];

static unsigned int gCurrentTestCase = gTestCases[0];
static SaveFileTestData* saveFileTestData[MAX_NUMBER_TEST_PROCESSORS];
static SaveFileTestData* saveFileTestDataBuffer;

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


static bool initialize()
{
    enableAVX();

    initTimeStampCounter();

    return true;
}

static bool initSaveFileTest()
{
    allocatePool(sizeof(SaveFileTestData), (void**)&saveFileTestDataBuffer);
    setMem(saveFileTestDataBuffer, sizeof(SaveFileTestData), 0);
    for (int i = 0; i < gNumberOfAllProcessors; i++)
    {
        allocatePool(sizeof(SaveFileTestData), (void**)&saveFileTestData[i]);
        setMem(saveFileTestData[i], sizeof(SaveFileTestData), 0);
    }
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

    setText(gTestCasesString[WRITE_FILE], L"WRITE_FILE");
    setText(gTestCasesString[WRITE_LARGE_FILE], L"WRITE_LARGE_FILE");
    setText(gTestCasesString[READ_FILE], L"READ_FILE");
    setText(gTestCasesString[READ_LARGE_FILE], L"READ_LARGE_FILE");
    setText(gTestCasesString[ASYNC_BLOCKING_WRITE_FILE], L"ASYNC_BLOCKING_WRITE_FILE");
    setText(gTestCasesString[ASYNC_BLOCKING_WRITE_LARGE_FILE], L"ASYNC_BLOCKING_WRITE_LARGE_FILE");
    setText(gTestCasesString[ASYNC_WRITE_FILE], L"ASYNC_WRITE_FILE");
    setText(gTestCasesString[ASYNC_WRITE_LARGE_FILE], L"ASYNC_WRITE_LARGE_FILE");
    setText(gTestCasesString[ASYNC_READ_FILE], L"ASYNC_READ_FILE");
    setText(gTestCasesString[ASYNC_READ_LARGE_FILE], L"ASYNC_READ_LARGE_FILE");

    if (!initSaveFileTest())
    {
        return false;
    }

    return true;
}

static void deinitSaveFileTest()
{
    freePool(saveFileTestDataBuffer);
    for (int i = 0; i < gNumberOfAllProcessors; i++)
    {
        freePool(saveFileTestData[i]);
    }
}

static void deinitialize()
{
    deinitSaveFileTest();

    deInitFileSystem();
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

void generateDataPerId(int id)
{
    // randomly generate a chunk of data
    for (unsigned long long i = 0; i < MEM_BUFFER_SIZE; i++)
    {
        saveFileTestData[id]->memBuffer[i] = random(4096) * (id + 1);
    }

    // Randomly pick some part of data for writing out
    unsigned long long remainedData = MEM_BUFFER_SIZE;
    for (int i = 0; i < sizeof(saveFileTestData[id]->dataPos) / sizeof(saveFileTestData[id]->dataPos[0]); i++)
    {
        // Start of the data
        saveFileTestData[id]->dataPos[i][0] = random64(MEM_BUFFER_SIZE - 1);

        // Size of the data. Make sure we limit all small files in size of total MEM_BUFFER_SIZE
        unsigned long long dataSize = random64(MEM_BUFFER_SIZE - saveFileTestData[id]->dataPos[i][0]);
        dataSize = dataSize > remainedData ? remainedData : dataSize;
        remainedData = remainedData - dataSize;

        if (dataSize == 0)
        {
            dataSize = 1;
        }

        saveFileTestData[id]->dataPos[i][1] = dataSize;
    }
}

bool runSaveLargeFile(int processId, bool paralellFlag = true, bool blocking = true)
{
    int id = processId;

    // Save file
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    // Generate random data
    for (unsigned long long i = 0; i < MEM_BUFFER_SIZE; i++)
    {
        saveFileTestData[id]->memBuffer[i] = random(4096) * (id + 1);
    }

    // Try to save the files
    long long sts = -1;
    if (paralellFlag)
    {
        sts = asyncSaveLargeFile(fileName, MEM_BUFFER_SIZE * sizeof(unsigned int), (unsigned char*)(saveFileTestData[id]->memBuffer), NULL, false, blocking);
    }
    else
    {
        sts = saveLargeFile(fileName, MEM_BUFFER_SIZE * sizeof(unsigned int), (unsigned char*)(saveFileTestData[id]->memBuffer), NULL, false);
    }
    if (sts <= 0 || sts != MEM_BUFFER_SIZE * sizeof(unsigned int))
    {
        CHAR16 loginfo[256];
        setText(loginfo, L"saveFile failed at ");
        appendText(loginfo, fileName);
        appendText(loginfo, L" with size ");
        appendNumber(loginfo, MEM_BUFFER_SIZE * sizeof(unsigned int) / 1024, true);
        appendText(loginfo, L"KB . Error: -");
        appendNumber(loginfo, -sts, true);

        ACQUIRE(logMessageLock);
        logToConsole(loginfo);
        RELEASE(logMessageLock);

        return false;
    }

    return true;
}

bool runSaveFile(int processId, bool parallelFlag = true, bool blocking = true)
{
    int id = processId;

    // Save file
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    // Generate random data
    generateDataPerId(id);

    // Try to save the files
    for (int i = 0; i < sizeof(saveFileTestData[id]->dataPos) / sizeof(saveFileTestData[id]->dataPos[0]); i++)
    {
        unsigned long long dataStart = saveFileTestData[id]->dataPos[i][0];
        unsigned long long dataCount = saveFileTestData[id]->dataPos[i][1];

        CHAR16 partionFileName[256];
        setText(partionFileName, fileName);
        appendText(partionFileName, L".");
        appendNumber(partionFileName, i, false);

        long long sts = -1;
        if (parallelFlag)
        {
            sts = asyncSave(partionFileName, dataCount * sizeof(unsigned int), (unsigned char*)&(saveFileTestData[id]->memBuffer[dataStart]), NULL, blocking);
        }
        else
        {
            sts = save(partionFileName, dataCount * sizeof(unsigned int), (unsigned char*)&(saveFileTestData[id]->memBuffer[dataStart]), NULL);
        }

        if (sts <= 0)
        {
            CHAR16 loginfo[256];
            setText(loginfo, L"saveFile failed at ");
            appendText(loginfo, partionFileName);
            appendText(loginfo, L" with size ");
            appendNumber(loginfo, dataCount * sizeof(unsigned int) / 1024, true);
            appendText(loginfo, L"KB . Error: -");
            appendNumber(loginfo, -sts, true);

            ACQUIRE(logMessageLock);
            logToConsole(loginfo);
            RELEASE(logMessageLock);

            return false;
        }
    }

    return true;
}

bool runReadFile(int processId, bool parallelFlag = true)
{
    int id = processId;

    // Save file
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    // Try to read the files
    for (int i = 0; i < sizeof(saveFileTestData[id]->dataPos) / sizeof(saveFileTestData[id]->dataPos[0]); i++)
    {
        unsigned long long dataStart = saveFileTestData[id]->dataPos[i][0];
        unsigned long long dataCount = saveFileTestData[id]->dataPos[i][1];

        CHAR16 partionFileName[256];
        setText(partionFileName, fileName);
        appendText(partionFileName, L".");
        appendNumber(partionFileName, i, false);

        long long sts = -1;
        if (parallelFlag)
        {
            sts = asyncLoad(partionFileName, dataCount * sizeof(unsigned int), (unsigned char*)&(saveFileTestData[id]->memBuffer[dataStart]), NULL);
        }
        else
        {
            sts = load(partionFileName, dataCount * sizeof(unsigned int), (unsigned char*)&(saveFileTestData[id]->memBuffer[dataStart]), NULL);
        }

        if (sts <= 0)
        {
            CHAR16 loginfo[256];
            setText(loginfo, L"read failed at ");
            appendText(loginfo, partionFileName);
            appendText(loginfo, L" with size ");
            appendNumber(loginfo, dataCount * sizeof(unsigned int) / 1024, true);
            appendText(loginfo, L"KB . Error: -");
            appendNumber(loginfo, -sts, true);

            ACQUIRE(logMessageLock);
            logToConsole(loginfo);
            RELEASE(logMessageLock);

            return false;
        }
    }

    return true;
}

bool runReadLargeFile(int processId, bool parallelFlag = true)
{
    int id = processId;

    // Save file
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    // Try to load the large file
    long long sts = -1;
    if (parallelFlag)
    {
        sts = asyncLoadLargeFile(fileName, MEM_BUFFER_SIZE * sizeof(unsigned int), (unsigned char*)(saveFileTestData[id]->memBuffer), NULL);
    }
    else
    {
        sts = loadLargeFile(fileName, MEM_BUFFER_SIZE * sizeof(unsigned int), (unsigned char*)(saveFileTestData[id]->memBuffer), NULL);
    }
    if (sts <= 0 || sts != MEM_BUFFER_SIZE * sizeof(unsigned int))
    {
        CHAR16 loginfo[256];
        setText(loginfo, L"loadLargeFile failed at ");
        appendText(loginfo, fileName);
        appendText(loginfo, L" with size ");
        appendNumber(loginfo, MEM_BUFFER_SIZE * sizeof(unsigned int) / 1024, true);
        appendText(loginfo, L"KB . Error: -");
        appendNumber(loginfo, -sts, true);

        ACQUIRE(logMessageLock);
        logToConsole(loginfo);
        RELEASE(logMessageLock);

        return false;
    }

    return true;
}

bool verifyWriteFile(int id)
{
    CHAR16 logInfo[256];
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    for (int i = 0; i < sizeof(saveFileTestData[id]->dataPos) / sizeof(saveFileTestData[id]->dataPos[0]); i++)
    {
        unsigned long long dataStart = saveFileTestData[id]->dataPos[i][0];
        unsigned long long dataCount = saveFileTestData[id]->dataPos[i][1];

        CHAR16 partionFileName[256];
        setText(partionFileName, fileName);
        appendText(partionFileName, L".");
        appendNumber(partionFileName, i, false);

        long long sts = load(partionFileName, dataCount * sizeof(unsigned int), (unsigned char*)&(saveFileTestDataBuffer->memBuffer[0]), NULL);

        if (sts <= 0)
        {
            setText(logInfo, partionFileName);
            appendText(logInfo, L" is FAILED to load.");
            logToConsole(logInfo);

            return false;
        }

        unsigned int* originalData = &(saveFileTestData[id]->memBuffer[dataStart]);
        unsigned int* loadedData = &(saveFileTestDataBuffer->memBuffer[0]);

        for (unsigned long long k = 0; k < dataCount; k++)
        {
            if (originalData[k] != loadedData[k])
            {
                setText(logInfo, partionFileName);
                appendText(logInfo, L" Data mismatched. [");
                appendNumber(logInfo, k, false);
                appendText(logInfo, L"]: ");
                appendNumber(logInfo, originalData[k], false );
                appendText(logInfo, L" vs  ");
                appendNumber(logInfo, loadedData[k], false);
                logToConsole(logInfo);

                return false;
            }
        }
    }

    return true;
}

bool verifyWriteLargeFile(int id)
{
    CHAR16 logInfo[256];
    CHAR16 fileName[32];
    setText(fileName, L"file_");
    appendNumber(fileName, id, false);

    setMem((unsigned char*)&(saveFileTestDataBuffer->memBuffer[0]), MEM_BUFFER_SIZE * sizeof(unsigned int), 0);
    long long sts = loadLargeFile(fileName, MEM_BUFFER_SIZE * sizeof(unsigned int), (unsigned char*)&(saveFileTestDataBuffer->memBuffer[0]), NULL);

    if (sts <= 0 || sts != MEM_BUFFER_SIZE * sizeof(unsigned int))
    {
        setText(logInfo, fileName);
        appendText(logInfo, L" is FAILED to load.");
        logToConsole(logInfo);

        return false;
    }

    unsigned int* originalData = saveFileTestData[id]->memBuffer;
    unsigned int* loadedData = saveFileTestDataBuffer->memBuffer;

    for (unsigned long long k = 0; k < MEM_BUFFER_SIZE; k++)
    {
        if (originalData[k] != loadedData[k])
        {
            setText(logInfo, fileName);
            appendText(logInfo, L" Data mismatched. [");
            appendNumber(logInfo, k, false);
            appendText(logInfo, L"]: ");
            appendNumber(logInfo, originalData[k], false);
            appendText(logInfo, L" vs  ");
            appendNumber(logInfo, loadedData[k], false);
            logToConsole(logInfo);

            return false;
        }
    }

    return true;
}

bool verifyWriteResult(bool parallelFlag)
{
    // Scheduler write will happen here. Flush all data to disk.
    flushAsyncFileIOBuffer();

    CHAR16 logInfo[256];
    // Check the result by loading the file and compare
    int matchFileCount = 0;
    int expectedMatchFileCount = parallelFlag ? gNumberOfAllProcessors : 1;
    for (int id = 0; id < gNumberOfAllProcessors; id++)
    {
        if (parallelFlag)
        {
            if (verifyWriteFile(id))
            {
                matchFileCount++;
            }
        }
        else
        {
            if (id == gBSProc)
            {
                if (verifyWriteFile(id))
                {
                    matchFileCount++;
                }
                break;
            }
        }

    }

    setText(logInfo, L"  - Matched data: ");
    appendNumber(logInfo, matchFileCount, false);
    appendText(logInfo, L" / ");
    appendNumber(logInfo, expectedMatchFileCount, false);
    logToConsole(logInfo);

    return (matchFileCount == expectedMatchFileCount);
}

bool verifyLargeFileWriteResult(bool parallelFlag)
{
    // Scheduler write will happen here. Flush all data to disk.
    flushAsyncFileIOBuffer();

    CHAR16 logInfo[256];
    // Check the result by loading the file and compare
    int matchFileCount = 0;
    int expectedMatchFileCount = parallelFlag ? gNumberOfAllProcessors : 1;
    for (int id = 0; id < gNumberOfAllProcessors; id++)
    {
        if (parallelFlag)
        {
            if (verifyWriteLargeFile(id))
            {
                matchFileCount++;
            }
        }
        else
        {
            if (id == gBSProc)
            {
                if (verifyWriteLargeFile(id))
                {
                    matchFileCount++;
                }
                break;
            }
        }

    }

    setText(logInfo, L"  - Matched data: ");
    appendNumber(logInfo, matchFileCount, false);
    appendText(logInfo, L" / ");
    appendNumber(logInfo, expectedMatchFileCount, false);
    logToConsole(logInfo);

    return (matchFileCount == expectedMatchFileCount);

    return true;
}

bool prepareTest()
{
    // For test loading files. Generate a list of files before reading
    for (int id = 0; id < gNumberOfAllProcessors; id++)
    {
        bool sts = true;
        switch (gCurrentTestCase)
        {
            case READ_FILE:
                sts = runSaveFile(id, false);
                break;
            case READ_LARGE_FILE:
                sts = runSaveLargeFile(id, false);
                break;
            case ASYNC_READ_FILE:
                sts = runSaveFile(id, false);
                break;
            case ASYNC_READ_LARGE_FILE:
                sts = runSaveLargeFile(id, false);
                break;
            default:
                break;
        }

        if (!sts)
        {
            logToConsole(L"Prepare test for READ file is failed");
            return false;
        }
    }
    return true;
}

bool verifyResult()
{
    bool allTestPass = true;

    // Check test result
    CHAR16 logInfo[256];
    for (int id = 0; id < gNumberOfAllProcessors; id++)
    {
        if (gProcessorResult[id] == 0)
        {
            allTestPass = false;
            setText(logInfo, L"Read/Write failed at thread ");
            appendNumber(logInfo, id, false);
            logToConsole(logInfo);
        }
    }

    // Test matching data.
    if (allTestPass)
    {
        switch (gCurrentTestCase)
        {
        case WRITE_FILE:
            allTestPass = verifyWriteResult(false);
            break;
        case ASYNC_WRITE_FILE:
        case ASYNC_BLOCKING_WRITE_FILE:
            allTestPass = verifyWriteResult(true);
            break;
        case WRITE_LARGE_FILE:
            allTestPass = verifyLargeFileWriteResult(false);
            break;
        case ASYNC_WRITE_LARGE_FILE:
        case ASYNC_BLOCKING_WRITE_LARGE_FILE:
            allTestPass = verifyLargeFileWriteResult(true);
            break;
        case READ_FILE:
            allTestPass = verifyWriteResult(true);
            break;
        case READ_LARGE_FILE:
            allTestPass = verifyLargeFileWriteResult(true);
            break;
        case ASYNC_READ_FILE:
            allTestPass = verifyWriteResult(true);
            break;
        case ASYNC_READ_LARGE_FILE:
            allTestPass = verifyLargeFileWriteResult(true);
            break;
        default:
            break;
        }
    }

    return allTestPass;
}

#pragma optimize("", off)
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

    //CHAR16 logInfo[256];
    //setText(logInfo, L"Thread id");
    //appendNumber(logInfo, id, false);

    ////if (id == 0)
    //{
    //    ACQUIRE(logMessageLock);
    //    logToConsole(logInfo);
    //    RELEASE(logMessageLock);
    //}

    // Test the save file
    switch (gCurrentTestCase)
    {
        case WRITE_FILE:
            testResult = runSaveFile(id, false);
            break;
        case WRITE_LARGE_FILE:
            testResult = runSaveLargeFile(id, false);
            break;
        case ASYNC_WRITE_FILE:
            testResult = runSaveFile(id, true, false);
            break;
        case ASYNC_WRITE_LARGE_FILE:
            testResult = runSaveLargeFile(id, true, false);
            break;
        case ASYNC_BLOCKING_WRITE_FILE:
            testResult = runSaveFile(id, true, true);
            break;
        case ASYNC_BLOCKING_WRITE_LARGE_FILE:
            testResult = runSaveLargeFile(id, true, true);
            break;
        case READ_FILE:
            testResult = runReadFile(id, false);
            break;
        case READ_LARGE_FILE:
            testResult = runReadLargeFile(id, false);
            break;
        case ASYNC_READ_FILE:
            testResult = runReadFile(id, true);
            break;
        case ASYNC_READ_LARGE_FILE:
            testResult = runReadLargeFile(id, true);
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


        if (!initFilesystem(gpServicesProtocol))
        {
            logToConsole(L"Init filesystem failed!");
            return EFI_ABORTED;
        }


        // Init test
        initTest();

        int testSuccessCount = 0;
        int testCount = sizeof(gTestCases) / sizeof(gTestCases[0]);

        // Run the tests
        setText(loginfo, L"BufferSize for each thread: ");
        appendNumber(loginfo, MEM_BUFFER_SIZE * sizeof(int) / 1024, false);
        appendText(loginfo, L" KB");
        logToConsole(loginfo);
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
            if (gCurrentTestCase != WRITE_FILE 
                && gCurrentTestCase != WRITE_LARGE_FILE
                && gCurrentTestCase != READ_FILE
                && gCurrentTestCase != READ_LARGE_FILE)
            {
                logToConsole(L"  - Running multithread test...");
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
                logToConsole(L"  - Waiting for all tasks done...");
                unsigned long long startTick = __rdtsc();
                int readyCount = 0;
                while (readyCount < gNumberOfAllProcessors)
                {
                    // Don't flush right away. Wait sometimes for simulate
                    unsigned long long waitingTimeInMs = (__rdtsc() - startTick) * 1000 / frequency;
                    if (waitingTimeInMs > 30000)
                    {
                        logToConsole(L"  - Flusing the buffer ...");
                        startTick = __rdtsc();
                        flushAsyncFileIOBuffer();
                    }

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
                setMem(gProcessorResult, gNumberOfAllProcessors * sizeof(gProcessorResult[0]), 1);
                threadRun(&bsProcID);
            }

            logToConsole(L"  - Verifying result...");

            // Verify results
            bool sts = verifyResult();
            if (sts)
            {
                testSuccessCount++;
            }

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

