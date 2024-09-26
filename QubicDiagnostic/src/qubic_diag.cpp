#include <intrin.h>
#include "../../src/platform/uefi.h"


#include "../../src/platform/time.h"
#include "../../src/platform/time_stamp_counter.h"
#include "../../src/platform/concurrency.h"
#include "../../src/platform/debugging.h"

#include "../../src/text_output.h"
#include "../../src/platform/console_logging.h"

#include "../../src/kangaroo_twelve.h"
#include "../../src/four_q.h"

#include "../../src/public_settings.h"
#include "../../src/score.h"

#if defined(NDEBUG)
    #define diagLogToConsole(loginfo) logToConsole(loginfo)
    #define diagPrintDebugMessages()
#else
    #define diagLogToConsole(loginfo) addDebugMessage(loginfo)
    #define diagPrintDebugMessages() printDebugMessages()
#endif

// Change the number of processors use for testing
#define NUMBER_TEST_PROCESSORS 16

#define TEST_DURATION 8
#define TEST_NEURON 256
#define TEST_NN 256

#define MAX_NUMBER_TEST_PROCESSORS 256
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

} Processor;
static volatile int shutDownNode = 0;
static EFI_MP_SERVICES_PROTOCOL* mpServicesProtocol;
static unsigned int numberOfProcessors = 0;
static volatile char logMessageLock = 0;
static Processor processors[MAX_NUMBER_TEST_PROCESSORS];
static ScoreFunction<DATA_LENGTH, TEST_NEURON, TEST_NN, TEST_DURATION, NUMBER_TEST_PROCESSORS>* gpScore = nullptr;

CHAR16 loginfo[2048];

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

static char gtSamples[][3][65] = {
{"80f531580b97c8de305ee20b7c87624b022c65badf37863f75360d9b94c8b748", "ec8dc8f960ce120cfc670b71120eb3756fc77039f81f7e9f763820c1d185ee78", "59039e870f2626a33bbc24821b9c2ca6bf11aa89149d3f726fdf8e3bdef41b03"},
{"33599e8c36c33829a77ffde84b0c9a63fb6eb1fd984a7202b27d8e98a198d87f", "10833f675abafcda6ed8962325b5492d570f04f028c2c928a613ad8f8ab4b59e", "cf2a3e440731066c9554203f364597e8e169306b16bf5dbed16bead466db9afa"},
{"8c53fb63add5ba46d7d35f3e884436c75d99141fbf8cecb2ceb4a49bec39b453", "45e4cc860d917fd83e2fbe418036ff8299b045ea8ff390c4e67f226ce809a703", "57ba057d57a2353e562eb11e3cd06184660e68e5a8295b0077cf65cda8455032"},
{"00bcca3db8ecb2e5837ba713a3311c6296d4bea989759fb80bcc03684e33140e", "ab0a50c2ea6eda36a5e4bfc9de3325682b7e95d3514332d437f91892783e0ee6", "60e4227369abce0de26272a110f9313b4422259d493a884f05c37f6dd85afc93"},
{"09c6876f692ee4f6dab5ea33fe0c30ae760d963a65d7f5ef84081847d5d313ae", "269e8f56b6a6fc9467f19df64c7550f331478fced4351a8b88e8580cf6b9308f", "404466a7dbd477b2f07e88abe497e978b5f92f62bc2b73e70735fa1c6fe05b15"},
{"b8057dc06435a52da037aa6b0cc8aa225eaaf35df702fd55a3c5b7e1854ba484", "d19dd21972d9a0c94c022d919aa6f4eac56198d9f74d79fdc10f873dcb5200de", "6be282581136b5057f68efba9ce8192761a5964fc45be06953ae8311cab57865"},
{"5905d90d0aa97296d4f7afb08903cd2099dfda1392a32dcb31ae0cc011885e83", "dc1c49b4e9f7f37b9924dc6d950100f9757902792607a94123961aa3edf7ab91", "4b2259498dc9a5bc3c7cdb80d90cbe3fd770cbb36d505ed3766d57fd66a51088"},
{"a2a682a0150c46484903c8230a9c1e6031d49c480e5a4d1279afa620f7476525", "4379baa2d19f02fda714720dd5499d1bebc3f77819d700367def523c1ecec35f", "99a9e19d1294be153356c4f9a23ab2dd782b7072f91479247397e160da77ef9a"},
};

static unsigned int gtScores[] = {
82,
77,
89,
82,
81,
86,
75,
77,
};

static void byteToHex(const unsigned char* byte, char* hex, const int sizeInByte)
{
    const char hexDigits[] = "0123456789abcdef";
    for (int i = 0; i < sizeInByte; i++)
    {
        hex[i * 2] = hexDigits[(byte[i] >> 4) & 0xF];
        hex[i * 2 + 1] = hexDigits[byte[i] & 0xF];
    }
    hex[sizeInByte * 2] = '\0';
}

static void hexToByte(const char* hex, unsigned char* byte, const int sizeInByte)
{
    auto hexCharToByte = [](char c) -> unsigned char
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
    for (int i = 0; i < sizeInByte; i++)
    {
        byte[i] = (hexCharToByte(hex[i * 2]) << 4) | hexCharToByte(hex[i * 2 + 1]);
    }
}

static bool InitScoreTest()
{
    // Init score
    if (!allocatePool(sizeof(ScoreFunction<DATA_LENGTH, TEST_NEURON, TEST_NN, TEST_DURATION, NUMBER_TEST_PROCESSORS>), (void**)&gpScore))
    {
        logToConsole(L"Allocate score failed!");
        return false;
    }
    gpScore->initMemory();

    return true;
}

static bool InitTest()
{
    if (!InitScoreTest())
    {
        logToConsole(L"InitTest failed!");
        return false;
    }
    return true;
}

static bool initialize()
{
    enableAVX();

#if defined (__AVX512F__) && !GENERIC_K12
    initAVX512KangarooTwelveConstants();
#endif
#if defined (__AVX512F__)
    initAVX512FourQConstants();
#endif
    
    if (!initFilesystem())
    {
        return false;
    }
    return true;
}

static void deinitialize()
{
}

bool RunScoreTest(unsigned long long processID)
{
    unsigned int testPassed = 0;
    unsigned int totalTests = 0;
    unsigned int numTest = 1;//sizeof(gtScores) / sizeof(gtScores[0]);
    for( unsigned int testIndex = 0; testIndex < numTest; testIndex++)
    {
        m256i testPublicKey = m256i::zero();
        m256i testMiningSeed = m256i::zero();
        m256i testNonce = m256i::zero();

        hexToByte(gtSamples[testIndex][0], testMiningSeed.m256i_u8, 32);
        hexToByte(gtSamples[testIndex][1], testPublicKey.m256i_u8, 32);
        hexToByte(gtSamples[testIndex][2], testNonce.m256i_u8, 32);


        // Init mining data
        gpScore->initMiningData(testMiningSeed);

        // Run SSE
        unsigned int sseScore = gpScore->RunSSE(processID, testPublicKey, testMiningSeed, testNonce);

        // Run SSE
        unsigned int avxScore = gpScore->RunAVX(processID, testPublicKey, testMiningSeed, testNonce);

        setText(loginfo, L"Score test ");
        appendNumber(loginfo, testIndex, false);
        appendText(loginfo, L" : ");
        if (sseScore != avxScore)
        {
            appendText(loginfo, L" FAILED (sse: ");
            appendNumber(loginfo, sseScore, false);
            appendText(loginfo, L" vs avx: ");
            appendNumber(loginfo, avxScore, false);
            //appendText(loginfo, L" vs ");
            //appendNumber(loginfo, gtScores[testIndex], false);
            appendText(loginfo, L" )");
        }
        else
        {
            testPassed++;
            appendText(loginfo, L" PASSED ");
        }

        diagLogToConsole(loginfo);
        totalTests++;
    }

    setText(loginfo, L"Score result: PASS ");
    appendNumber(loginfo, testPassed, false);
    appendText(loginfo, L" / ");
    appendNumber(loginfo, totalTests, false);

    if (testPassed == totalTests)
    {
        return true;
    }
    return false;
}

void RunTest(void* proccessorInfo)
{
    Processor* process = (Processor*)proccessorInfo;
    process->testResult = RunScoreTest(process->id);

    process->isReady = true;
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
            logToConsole(L"Pressed F2 key");
        }
        break;
        /*
       *
       * F3 Key
       */
        case 0x0D:
        {
            logToConsole(L"Pressed F3 key");
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
            setText(message, L"Press key scan code ");
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
        diagLogToConsole(L"Setting up multiprocessing ...");

        // MP service protocol
        unsigned int computingProcessorNumber;
        EFI_GUID mpServiceProtocolGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
        status = bs->LocateProtocol(&mpServiceProtocolGuid, NULL, (void**)&mpServicesProtocol);
        if (EFI_SUCCESS != status)
        {
            diagLogToConsole(L"Can not locate MP_SERVICES_PROTOCOL");
        }

        // Get number of processers and enabled processors
        unsigned long long numberOfAllProcessors, numberOfEnabledProcessors;
        status = mpServicesProtocol->GetNumberOfProcessors(mpServicesProtocol, &numberOfAllProcessors, &numberOfEnabledProcessors);
        if (EFI_SUCCESS != status)
        {
            diagLogToConsole(L"Can not get number of processors.");
        }
        setText(loginfo, L"Enabled processors: ");
        appendNumber(loginfo, numberOfEnabledProcessors, false);
        appendText(loginfo, L" / ");
        appendNumber(loginfo, numberOfAllProcessors, false);

        numberOfAllProcessors = numberOfAllProcessors > NUMBER_TEST_PROCESSORS ? NUMBER_TEST_PROCESSORS : numberOfAllProcessors;
        appendText(loginfo, L". Using ");
        appendNumber(loginfo, numberOfAllProcessors, false);
        appendText(loginfo, L" processors ");

        diagLogToConsole(loginfo);

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

            if (processors[i].isBSProc)
            {
                setText(loginfo, L"Processor ");
                appendNumber(loginfo, i, false);
                appendText(loginfo, L" [BS] ");
                diagLogToConsole(loginfo);
            }
            //appendText(loginfo, L" : id = ");
            //appendNumber(loginfo, processors[i].id, false);
            //appendText(loginfo, L" , StsFlag = ");
            //appendNumber(loginfo, processors[i].StatusFlag, false);
            //appendText(loginfo, L" , Location: ");
            //appendText(loginfo, L"Package ");  appendNumber(loginfo, processors[i].package, false);
            //appendText(loginfo, L" , Core ");  appendNumber(loginfo, processors[i].core, false);
            //appendText(loginfo, L" , Thread ");  appendNumber(loginfo, processors[i].thread, false);
            //diagLogToConsole(loginfo);
        }

        // Init test case
        if (InitTest())
        {
            static int testCases[] = { 0 };
            unsigned int numberOfTests = sizeof(testCases) / sizeof(testCases[0]);
            unsigned int numberOfPassed = 0;
            for (unsigned int test = 0; test < numberOfTests; test++)
            {
                // Start the test with main processor
                RunTest(&processors[bsProcID]);

                // Check the result
                if (processors[bsProcID].isReady)
                {
                    processors[bsProcID].isReady = false;
                    if (processors[bsProcID].testResult)
                    {
                        numberOfPassed++;
                    }
                }
            }

            setText(loginfo, L"TEST SUIT PASS ");
            appendNumber(loginfo, numberOfPassed, false);
            appendText(loginfo, L" /");
            appendNumber(loginfo, numberOfTests, false);
            diagLogToConsole(loginfo);
        }

        diagPrintDebugMessages();

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
        while (!shutDownNode)
        {
            processKeyPresses();
        }
    }
    else
    {
        diagLogToConsole(L"Initialization fails!");
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

