#include <intrin.h>
#include "../../src/platform/uefi.h"


#include "../../src/platform/time.h"
#include "../../src/platform/time_stamp_counter.h"
#include "../../src/platform/concurrency.h"

#include "../../src/text_output.h"
#include "../../src/platform/console_logging.h"

#include "../../src/kangaroo_twelve.h"
#include "../../src/four_q.h"

// Change the number of processors use for testing
#define NUMBER_TEST_PROCESSORS 256


#define MAX_NUMBER_TEST_PROCESSORS 256
typedef struct
{
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

} Processor;
static volatile int shutDownNode = 0;
static EFI_MP_SERVICES_PROTOCOL* mpServicesProtocol;
static unsigned int numberOfProcessors = 0;
static volatile char logMessageLock = 0;
static Processor processors[MAX_NUMBER_TEST_PROCESSORS];



static void logToConsole(const CHAR16* message)
{
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
    return true;
}

static void deinitialize()
{
}

inline static unsigned int random(const unsigned int range)
{
    unsigned int value;
    _rdrand32_step(&value);

    return value % range;
}



template <unsigned long long len>
void K12Test(unsigned char* outputResult = NULL)
{
    unsigned char input[64];
    unsigned char ouput[len];
    unsigned char overflow_checking[] = { 0, 1,2,3,4,5,6 };

    // Random input
    for (unsigned int i = 0; i < 64; i++) {
        input[i] = random(255);
    }
    KangarooTwelve(input, 64, ouput, len);

    // Checking the overflow buffer
    for (unsigned int i = 0; i < sizeof(overflow_checking); i++) {
        if (i != overflow_checking[i]) {
            ACQUIRE(logMessageLock);
            logToConsole(L"Overflow detection");
            RELEASE(logMessageLock);
            return;
        }
    }

    // Get the ouput result as 32 bytes
    if (NULL != outputResult)
    {
        KangarooTwelve(ouput, len, outputResult, 32);
    }
}

EFI_STATUS TestStackLimitTemplateK12() {

    static unsigned char output[32];
    constexpr unsigned int kb_size = 1024;
    bs->Stall(1000000);
    K12Test<32 * kb_size>(output);

    bs->Stall(1000000);
    K12Test<64 * kb_size>(output);

    bs->Stall(1000000);
    K12Test<512 * kb_size>(output);

    bs->Stall(1000000);
    K12Test<(512 + 16)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(512 + 32)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(512 + 64)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(512 + 128)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 16)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 32)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 64)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 128)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 256)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 512)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1024 + 512 + 48)* kb_size>(output);

    bs->Stall(1000000);
    K12Test<(1599)* kb_size>(output);

    return EFI_SUCCESS;
}



void TestStackLimitTemplateK12Processor(void* proccessorInfo) {
    constexpr unsigned int kb_size = 1024;
    CHAR16 messageK12[512];

    Processor* process = (Processor*)proccessorInfo;
    switch (process->testCase)
    {
    case 4:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<4 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 8:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<8 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 12:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<12 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 16:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<16 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 20:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<20 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 24:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<24 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 28:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<28 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 29:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<29 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 30:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<30 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 31:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<31 * kb_size>(&(process->buffer[0]));
        }
        break;
        // 32KB
    case 32:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<32 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 36:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<36 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 40:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<40 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 44:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<44 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 48:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<48 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 52:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<52 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 56:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<56 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 60:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<60 * kb_size>(&(process->buffer[0]));
        }
        break;
        // 64KB
    case 64:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<64 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 68:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<68 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 72:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<72 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 76:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<76 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 80:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<80 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 84:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<84 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 88:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<88 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 82:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<92 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 96:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<96 * kb_size>(&(process->buffer[0]));
        }
        break;
        // 128KB
    case 128:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<128 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 512:
        for (int i = 0; i < 10000; i++)
        {
            K12Test<512 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 900:
        for (int i = 0; i < 100; i++)
        {
            K12Test<900 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 990:
        for (int i = 0; i < 100; i++)
        {
            K12Test<990 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 1000:
        for (int i = 0; i < 100; i++)
        {
            K12Test<1000 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 1024:
        for (int i = 0; i < 100; i++)
        {
            K12Test<1024 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 1500:
        for (int i = 0; i < 100; i++)
        {
            K12Test<1500 * kb_size>(&(process->buffer[0]));
        }
        break;
    case 1599: // Maximum size set by /Gs
        for (int i = 0; i < 100; i++)
        {
            K12Test<1599 * kb_size>(&(process->buffer[0]));
        }
        break;
    default:
        break;
    }
    process->isReady = true;

}


EFI_STATUS TestStackLimit(unsigned int depth) {
    // 4KB each call
    volatile unsigned char buffer[2 * 1024];

    // Initialize buffer to consume stack space
    for (int i = 0; i < sizeof(buffer); i++) {
        buffer[i] = i;
    }

    unsigned long long totalBytesInStack = (depth + 1) * sizeof(buffer);
    // Stall the print to see the result
    if (totalBytesInStack > 1024 * 1024)
    {
        bs->Stall(200000);
    }
    else if (totalBytesInStack > 1500 * 1024) {
        bs->Stall(500000);
    }
    setText(message, L"Level ");
    appendNumber(message, depth, false); appendText(message, L": , size: ");
    appendNumber(message, totalBytesInStack >> 10, false);  appendText(message, L" KB");
    logToConsole(message);

    // Recursively call
    EFI_STATUS Status = TestStackLimit(depth + 1);

    if (Status == EFI_LOAD_ERROR) {
        // Check for stack overflow. Actually it crashes here
        return depth;
    }

    return EFI_SUCCESS;

}


template <unsigned long long numberOfKB>
void StackLimitTemplate()
{
    setText(message, L"Test ");
    appendNumber(message, numberOfKB, false); appendText(message, L" KB ");
    volatile unsigned char buffer[numberOfKB * 1024];
    // Initialize buffer 
    for (unsigned long long i = 0; i < sizeof(buffer); i++) {
        buffer[i] = i;
    }
    appendText(message, L"OK ");
    logToConsole(message);
};

EFI_STATUS TestStackLimitTemplate() {

    StackLimitTemplate<16>();
    StackLimitTemplate<32>();
    StackLimitTemplate<64>();
    StackLimitTemplate<128>();
    StackLimitTemplate<256>();
    StackLimitTemplate<512>();
    StackLimitTemplate<512 + 16>();
    StackLimitTemplate<512 + 32>();
    StackLimitTemplate<512 + 64>();
    StackLimitTemplate<512 + 128>();
    StackLimitTemplate<512 + 256>();
    StackLimitTemplate<1024>();
    StackLimitTemplate<1024 + 16>();
    StackLimitTemplate<1024 + 32>();
    StackLimitTemplate<1024 + 64>();
    StackLimitTemplate<1024 + 128>();
    StackLimitTemplate<1024 + 256>();
    StackLimitTemplate<1024 + 512>();
    StackLimitTemplate<1024 + 512 + 16>();
    StackLimitTemplate<1024 + 512 + 32>();
    StackLimitTemplate<1024 + 512 + 48>();

    // Maximum size set by /Gs
    StackLimitTemplate<1599>();
    return EFI_SUCCESS;
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
            logToConsole(L"Pressed F2 key. Test with recursive call. Will crash when hit recursion level or stack overflow.");
            EFI_STATUS sts = TestStackLimit(0);
            if (sts == EFI_SUCCESS) {
                appendText(message, L" Stack limit tested successfully.");
            }
            else {
                appendText(message, L"Stack overflow occurred at depth.");
                appendNumber(message, (int)sts, false);
            }
        }
        break;
        /*
       *
       * F3 Key
       */
        case 0x0D:
        {
            logToConsole(L"Pressed F2 key. Test with interative call. Will crash if stack overflow.");
            TestStackLimitTemplate();
        }
        break;
        /*
        * F4 Key
        */
        case 0x0E:
        {
            logToConsole(L"Pressed F4 key. Test with K12 computation. Will crash if stack overflow.");
            TestStackLimitTemplateK12();
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
        unsigned long long numberOfAllProcessors, numberOfEnabledProcessors;
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

        static int testCases[] = { 16, 30, 31, 32 , 36, 40, 44, 48, 52 , 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 128, 512, 900 ,990, 1024, 1500, 1599 };
        // Processor health and location
        int bsProcID = 0;
        for (int i = 0; i < numberOfAllProcessors; i++)
        {
            EFI_PROCESSOR_INFORMATION procInfo;
            status = mpServicesProtocol->GetProcessorInfo(mpServicesProtocol, i, &procInfo);
            processors[i].id = procInfo.ProcessorId;
            processors[i].StatusFlag = procInfo.StatusFlag;
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

            setText(loginfo, L"Processor ");
            appendNumber(loginfo, i, false);
            if (processors[i].isBSProc)
            {
                appendText(loginfo, L" [BS] ");
            }
            appendText(loginfo, L" : id = ");
            appendNumber(loginfo, processors[i].id, false);
            appendText(loginfo, L" , StsFlag = ");
            appendNumber(loginfo, processors[i].StatusFlag, false);
            appendText(loginfo, L" , Location: ");
            appendText(loginfo, L"Package ");  appendNumber(loginfo, processors[i].package, false);
            appendText(loginfo, L" , Core ");  appendNumber(loginfo, processors[i].core, false);
            appendText(loginfo, L" , Thread ");  appendNumber(loginfo, processors[i].thread, false);
            logToConsole(loginfo);
        }

        for (int test = 0; test < sizeof(testCases) / sizeof(testCases[0]); test++)
        {
            setText(loginfo, L"Test ");
            appendNumber(loginfo, testCases[test], false);
            appendText(loginfo, L" KB");
            logToConsole(loginfo);

            // Start the task for all application Proccessor
            for (int i = 0; i < numberOfAllProcessors; i++)
            {
                processors[i].testCase = testCases[test];
                // Start the task if it is an AP
                if (!processors[i].isBSProc)
                {
                    status = mpServicesProtocol->StartupThisAP(mpServicesProtocol, TestStackLimitTemplateK12Processor, i, &processors[i].event,
                        EFI_TIMEOUT, &processors[i], NULL);
                }
            }

            // Start the test with main processor
            TestStackLimitTemplateK12Processor(&processors[bsProcID]);

            // Wait for all task is done
            for (int i = 0; i < numberOfAllProcessors; i++)
            {
                while (!processors[i].isReady)
                {
                    //processKeyPresses();
                }
                processors[i].isReady = false;

                //CHAR16 digestChars[61];
                //getIdentity(processors[i].buffer, digestChars, true);

                //setText(loginfo, L"Test ");
                //appendNumber(loginfo, processors[i].testCase, false);
                //appendText(loginfo, L" KB:");

                //appendText(loginfo, L"proc ");
                //appendNumber(loginfo, processors[i].id, false);
                //appendText(loginfo, L" disgest: ");
                //appendText(loginfo, digestChars);
                //logToConsole(loginfo);
            }
            setText(loginfo, L"***Test ");
            appendNumber(loginfo, testCases[test], false);
            appendText(loginfo, L" KB is PASSED");
            logToConsole(loginfo);

            bs->Stall(1000000);
        }

        setText(loginfo, L"K12 multi-thread Stack overflow test DONE. Press F2, F3, F4 to do more agressive test");
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
        logToConsole(L"Test with K12 multi-threads DONE");
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

