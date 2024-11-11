#include <intrin.h>
#include "../../src/platform/uefi.h"


#include "../../src/platform/time.h"
#include "../../src/platform/time_stamp_counter.h"
#include "../../src/platform/concurrency.h"
#include "../../src/platform/file_io.h"

#include "../../src/text_output.h"
#include "../../src/platform/console_logging.h"

#include "../../src/kangaroo_twelve.h"
#include "../../src/four_q.h"

// Change the number of processors use for testing
#define NUMBER_TEST_PROCESSORS 256


#define LOOP_COUNT_TEST 10000
#define LOOP_COUNT_TEST_SMALL 1000
#define MAX_NUMBER_TEST_PROCESSORS 256

static constexpr unsigned long long TEST_FILE_SIZE = 1024 * 1024 * 1024;
typedef struct
{
    char lock;
    bool isReady;
    bool isBSProc;
    unsigned long long id;
    unsigned int StatusFlag;
    EFI_EVENT event;
    unsigned char buffer[32];
    unsigned long long testCase;

    unsigned int package;
    unsigned int core;
    unsigned int thread;

    bool testResult;

    // Mem
    unsigned char* memBuffer;

    // File root
    EFI_FILE_PROTOCOL* pRootFile;

} Processor;
static volatile int shutDownNode = 0;
static EFI_MP_SERVICES_PROTOCOL* mpServicesProtocol;
static unsigned int numberOfProcessors = 0;
static volatile char logMessageLock = 0;
static Processor processors[MAX_NUMBER_TEST_PROCESSORS];



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

    if (!initFilesystem())
    {
        return false;
    }

    return true;
}

static void deinitialize(unsigned int numberOfAllProcessors)
{
    // Clean up the memory of each processor
    for (int i = 0; i < numberOfAllProcessors; i++)
    {
        freePool(processors[i].memBuffer);
    }
}

inline static unsigned int random(const unsigned int range)
{
    unsigned int value;
    _rdrand32_step(&value);

    return value % range;
}

// Test function
bool writeSimpleFile(CHAR16* filename, unsigned long long byteSize, unsigned char* buffer)
{
    long long savedSize = save(filename, byteSize, buffer);
    if (savedSize == byteSize)
    {
        return true;
    }
    return false;
}

bool allocateMemTest(unsigned long long id, unsigned long long byteSize, unsigned char* buffer)
{
    bool sts = allocatePool(byteSize, (void**)(&buffer));
    return sts;
}

// Main test function for each processor
void threadRun(void* proccessorInfo)
{
    Processor* process = (Processor*)proccessorInfo;
    bool testResult = true;

    //testResult = allocateMemTest(process->id, fileSize, process->memBuffer);
    //if (testResult)
    {
        CHAR16 fileName[256];
        setText(fileName, L"dump_file_");
        appendNumber(fileName, process->id, false);
        appendText(fileName, L".bin");

        // Write with separate file root
        //testResult = save2(fileName, TEST_FILE_SIZE, process->memBuffer, NULL, process->pRootFile);

        testResult = save(fileName, TEST_FILE_SIZE, process->memBuffer, NULL);
    }

    process->testResult = testResult;

    ACQUIRE(process->lock);
    process->isReady = true;
    RELEASE(process->lock);
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
    unsigned long long numberOfAllProcessors, numberOfEnabledProcessors;
    if (initialize())
    {
        logToConsole(L"Setting up multiprocessing ...");

        CHAR16 loginfo[512];

        // MP service protocol
        unsigned int computingProcessorNumber;
        EFI_GUID mpServiceProtocolGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
        status = bs->LocateProtocol(&mpServiceProtocolGuid, NULL, (void**)&mpServicesProtocol);
        if (EFI_SUCCESS != status)
        {
            logToConsole(L"Can not locate MP_SERVICES_PROTOCOL");
        }

        // Get number of processers and enabled processors
        status = mpServicesProtocol->GetNumberOfProcessors(mpServicesProtocol, &numberOfAllProcessors, &numberOfEnabledProcessors);
        if (EFI_SUCCESS != status)
        {
            logToConsole(L"Can not get number of processors.");
        }
        setText(loginfo, L"Enabled processors: ");
        appendNumber(loginfo, numberOfEnabledProcessors, false);
        appendText(loginfo, L" / ");
        appendNumber(loginfo, numberOfAllProcessors, false);

        numberOfAllProcessors = numberOfAllProcessors > NUMBER_TEST_PROCESSORS ? NUMBER_TEST_PROCESSORS : numberOfAllProcessors;
        appendText(loginfo, L". Using ");
        appendNumber(loginfo, numberOfAllProcessors, false);
        appendText(loginfo, L" processors ");

        logToConsole(loginfo);

        static int testCases[] = { 16};
        // Processor health and location
        int bsProcID = 0;
        for (int i = 0; i < numberOfAllProcessors; i++)
        {
            EFI_PROCESSOR_INFORMATION procInfo;
            status = mpServicesProtocol->GetProcessorInfo(mpServicesProtocol, i, &procInfo);
            processors[i].id = procInfo.ProcessorId;
            processors[i].StatusFlag = procInfo.StatusFlag;
            processors[i].lock = 0;
            if (procInfo.StatusFlag & 0x1)
            {
                processors[i].isBSProc = true;
                bsProcID = i;
            }
            else
            {
                processors[i].isBSProc = false;

                // Create event for AP
                status = bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, NULL, NULL, &processors[i].event);
                processors[i].isReady = false;
            }

            EFI_CPU_PHYSICAL_LOCATION cpuLocation = procInfo.Location;
            processors[i].package = cpuLocation.Package;
            processors[i].core = cpuLocation.Core;
            processors[i].thread = cpuLocation.Thread;

            // Allocated memory 
            processors[i].memBuffer = NULL;
            bool allocateMem = allocatePool(TEST_FILE_SIZE, (void**)&(processors[i].memBuffer));
            if (!allocateMem)
            {
                setText(loginfo, L"Failed to allocated ");
                appendNumber(loginfo, TEST_FILE_SIZE / 1024, false);
                appendText(loginfo, L" KB at proc ");
                appendNumber(loginfo, i, false);
                logToConsole(loginfo);
            }

            // Init the filesystem for each processor
            bool initFileSystem = initFilesystem2(processors[i].pRootFile);

            if (!initFileSystem)
            {
                setText(loginfo, L"Failed to initFileSystem for proc ");
                appendNumber(loginfo,i, false);
                logToConsole(loginfo);
            }
        }

        // Write a file for validating one thread write success fully
        {
            setText(loginfo, L"test_file_init.bin");
            bool sts = writeSimpleFile(loginfo, TEST_FILE_SIZE, processors[bsProcID].memBuffer);
            if (sts)
            {
                appendText(loginfo, L" is successfully to be saved.");
            }
            else
            {
                appendText(loginfo, L" is FAILED to be saved.");
            }
            logToConsole(loginfo);
        }

        {
            setText(loginfo, L"BS Processor ");
            appendText(loginfo, L"id: ");
            appendNumber(loginfo, bsProcID, false);
            logToConsole(loginfo);
        }

        for (int test = 0; test < sizeof(testCases) / sizeof(testCases[0]); test++)
        {
            setText(loginfo, L"Trigger test ");
            appendNumber(loginfo, testCases[test], false);
            logToConsole(loginfo);

            // Start the task for all application Proccessor
            for (int i = 0; i < numberOfAllProcessors; i++)
            {
                processors[i].testCase = testCases[test];
                // Start the task if it is an AP
                if (!processors[i].isBSProc)
                {
                    status = mpServicesProtocol->StartupThisAP(mpServicesProtocol, threadRun, i, &processors[i].event,
                        EFI_TIMEOUT, &processors[i], NULL);
                }
            }

            // Start the test with main processor
            threadRun(&processors[bsProcID]);

            // Wait for all task is done
            bool isAllTestPassed = true;
            for (int i = 0; i < numberOfAllProcessors; i++)
            {
                bool isReady = false;
                while (!isReady)
                {
                    ACQUIRE(processors[i].lock);
                    isReady = processors[i].isReady;
                    RELEASE(processors[i].lock);
                }
                processors[i].isReady = false;

                // Check the result
                if (!processors[i].testResult)
                {
                    isAllTestPassed = false;
                    setText(loginfo, L"Test failed at thread ");
                    appendNumber(loginfo, i, false);
                    logToConsole(loginfo);
                }
            }
            bs->Stall(1000000);
        }

        setText(loginfo, L"Multi-thread test DONE. Press F2, F3, F4 to do more agressive test");
        logToConsole(loginfo);

        // Close all event
        for (int i = 0; i < numberOfAllProcessors; i++)
        {
            if (!processors[i].isBSProc)
            {
                bs->CloseEvent(&processors[i].event);
            }
        }

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

    deinitialize(numberOfAllProcessors);

    bs->Stall(1000000);
    if (!shutDownNode)
    {
        st->ConIn->Reset(st->ConIn, FALSE);
        unsigned long long eventIndex;
        bs->WaitForEvent(1, &st->ConIn->WaitForKey, &eventIndex);
    }

    return EFI_SUCCESS;
}

