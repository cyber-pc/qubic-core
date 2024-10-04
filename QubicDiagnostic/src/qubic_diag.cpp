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

#include "../../test/score_params.h"

#if defined(NDEBUG)
    #define diagLogToConsole(loginfo) logToConsole(loginfo)
    #define diagPrintDebugMessages()
#else
    #define diagLogToConsole(loginfo) logToConsole(loginfo); addDebugMessage(loginfo)
    #define diagPrintDebugMessages() printDebugMessages()
#endif

// Change the number of processors use for testing
#define NUMBER_TEST_PROCESSORS 16

#define TEST_DURATION 2048
#define TEST_NEURON 2048
#define TEST_NN 2048

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
{"3d8ea411bbbe6d7059a2b526c624c83d3c343db76f537c4ab2f9cdcc5675b572", "3431ab5a9fa62e8f4345ff4a125589dabca6e6c7054a55fa7d83b702c0555926", "a0d29fe37d12c1620ff24a14d73d9286ab3dc4a42bc26998e4a4f014d1ee2aa1"},
{"4e8fe95c3c588c4e2c91200a25b440bd6370666aa7da01463b46380c61687dc1", "16ea6d89ea0eeeaba1bc42b0975f65cb35f0abea32f0ea0f28aba05181a284d6", "d3af183839bbc4796bb7f7caddca8692ed054af06bcf9f537d786fd7595be98f"},
{"d93555f4d9708d8d5769a9e67538bbb8bc972b13f0c050e1b9ae3e0e7b8b4f48", "c72062a0540da03bd3c0896c6e76bd8d3f30f194a453d5a238b38f581ae599d1", "d5755ed9b7aa87c0f2275d8904b1cbb62406fe83966d86eebac4eb76a86297e3"},
{"32a31b82aed7bc6bedb3ab19359bdd61581ec14425fd50462f61838cdf638da5", "e950efa0811a7dae3757009b39a3abdc32933e6fedb4d8e0209ab626fe5440d8", "ef3067854218e09f7e852fc39b50a5eb3cf7b29032a783cd05519ca16e04669d"},
{"7e948db1a7684a32f89a42438ca2c87a47091285bdbfea312aae387d76947892", "b583c78ad5c47d2325f367b35921d343bf9e6de0e44986134f2c7711314a0cd0", "f993e2c24eda4ce2e62ef3741fdc8b3e0fca5a381b3eed4cad930bf9ada7dba6"},
{"2e162de4cdee20213ebfcf2caf4c04a323de6bce26706ff75d9c10a6558f94c3", "09aa8fcbc2821fab3bf7f0bfb865a71c656500a100b0e12fa9d8bd526b360bbb", "81ea2e1944262bb84ff4be4cb8dd7d55a50e7c3dcc959d0294d22fbe9bec181c"},
{"faa36a4f7f4a24d6b0b0ed20a84f3f88107d145aced8d0121567ed306a1a5b54", "a4b440a48970f9f6b6e206e6f7ca9f2565797b64fdd6fac9054560c7c0d8011f", "2197aab5575d88589eec41860452b624305c28e3e79441792b310c678c02be3f"},
{"e760dd45e081d630936cf558aa12df41933bbc2ef5feab212d05e4215410535f", "476886ba5fd8c152ba27a4cd96b3506c21d5960ef5ddbc568b2a99aa10415a6c", "7fb7e252ec4a5fed05add14be6e83e32a91bc1ee249940d6e476d2de8a8de664"},
{"3e09334a94cf09a1276db79eafe31f635c0b7228133751bfbdec8fef6e627f1a", "15f94a5ef8e903bcc5006c1b4eff4cfa4450862cb3ec7106cc9ba314d3c6434f", "ac69a5759ca9e7c360c1331773f633df73d707f56350a508ae85119806c725fb"},
{"3a1c7bc599df3ffd675c75a36863c6fd5cb64d9bd0c9b4d4be971a5dc3610bbb", "87216ca3baa3bbe6f865eccd26b5e004ec21cc479e54f206d77042956fe5ee05", "e3a1dcd5e679fb18aa2c24d9a5d61435b27cab8922f623599218d864a20b2246"},
{"841ec7849705b75df1c7a5f253b047acbcbe7b41c1d3f023870140fa42d1da7e", "4a052601f6268a63a30ef7c8003c7bdf5c3267fe296b7b14bf8b34c18494bcde", "71d4f9d959c5a605bdb28f42558b51fd828424fc0cd2967f9994947e669c0553"},
{"c19ee36ec9622277e71b372ebea2f71a07a6b394160d44c75eb845f27e248b60", "406d3ae9d2ed39d0a2d7a70e3b5d04d01cdc610a8711971a3a016c469d77e761", "cefc6eb11e83a15480e301258200d2619eafe2de434233064bd5c3d6e2e00dfa"},
{"7b5afbe57d21dcda54a22da1baae612c78d869bbaf1591f6be115e39b9c704d5", "aae174b0420fcd0bac0db4e3c1b80ccf44c893bd0fb23b7a633c37d68bc16660", "171b332e29d830eeda828e2de0c7c033377203ee68de12ec30f1c114188c7d2d"},
{"8351f698ce4d8d51bafd60b3d4f65859d9e574340138d2310d4e212715e325c8", "58f276daf479d6003cfd58d8fbea1ca398c8f441f8ac1420a18ee135cb9fe0ac", "6f4961ea5932a4ef0b1949d9575c3e3c1a8a6a584f6ac9ca09e80f871b48299b"},
{"c849cfa6b1ec4b727101a06f3c822575b4924df54120813cad8bf669c165367e", "08a1d0cbb5de9d5097c06e117ef635c5f8270e3eb7980c9c25fa05d723e9bb2e", "3896216d13bd6ba119f955728370784b3f5502b1bc9886474b1e3bfba1a352d6"},
{"b9cb313d7852624a4add3ac76bae5d6f51f755fe1c2755407f644127c645a863", "be7edb2465d546372f384491df1cadf8cfb594b4dd37c3a13a94b2fdf628bb6f", "4381fa78fc094a53d6295bfd4cd038efd41b0d4f12ba481523302d9d0966bc37"},
{"545cbb878e0126261dd69605e10f02e27cc8080d593edc1f5d9a6b7432af85db", "86e169e92c2e42970ec09e017b604fe2bd6728776eb792e5130832502c5e7541", "0a2cb9ada195325b2dcf914bd38c8a8cf1d32763561205377698348b7f01c734"},
{"3fda0ed9c632857d6e0edd390b7f644f40739e5be9a028f21417a9f44601b0ca", "cf920d57fe3a5f6b5abc28c068c94033db6d45314b3ec8c05c38439d9f07f5d6", "407927dd89cfef4f9343cc2760763e2f7a70eec9dcd563674b1c91bb07a93d42"},
{"2fe4e34d532bef8c4ba5ecd715fe35d1e195803dd4c6968739391a2c47cd059c", "8b6f647242f84fddc5bb476455895af6506539727d537bcaf16f556862525aba", "ba485348975a9afc1ee5c2edf87c25b8b02cea4f14f1d7b6751ef62bc4c27a6b"},
{"8e26e98b5da7ab3a69dd52c4e3e1344b584744eb4a03309140ad7311866b1f0a", "ac502f8894c790ad38ee8d4e54fa816ba072d54f8376ef87325d0795532e0956", "7284ce4ecca0ef4be435026f9a4d94e1a37311a1235ed2272f4ce24757b73bf8"},
{"1a053b9c78f7a7660c6255463b626a807fb47d4af36c6570b4467044495623dd", "335cf0c6f0959112911c889f606956b911ef608b80199284ce6b2c9a9f76473c", "f1b50598e2ca32c6440e6db8aa42f2e585d65e52978fcb11e3660ba4b282bf31"},
{"cf42c718a7f11d6b0e7e63da48029a82bbe57dc4f1279965462831ac24456aca", "3c082e6e8d68120df35eb7512a7f92c6ee4219bb77b50ff91a1402f0104269e2", "c24506eb048805058bddcd39942f78901b5a511d1185088dbc543f594d80e52a"},
{"114a7aee8996ea110423cd7ffa024515005035b98e160a6ceaf26782cd625409", "70f1f878cfa0ddfcce0bc21f11cbb5ddb767d1456712687043fb6020f0e9c9f2", "8551e022a73368b63b39e77310218e268499c47aaf1af07962e5f562b39c917c"},
{"722836fca4ef287c7b70de23240793da04c49854285a3fcb7e9d80752d63a58c", "5958922a179c35522bd603b6674bb6cae9d9ec8d090e7ddbfa98b2e181bf5fb6", "780ff13e9c1775479c944b7f73839b43dd16810ac2604c0893ebf1348220d2bf"},
};

static unsigned int gtScores[][sizeof(score_params::kSettings) / sizeof(score_params::kSettings[0])] = {
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
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
    //if (!InitScoreTest())
    //{
    //    logToConsole(L"InitTest failed!");
    //    return false;
    //}
    return true;
}

static bool initialize()
{
    enableAVX();

    initTimeStampCounter();

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

template <unsigned int settingID>
bool RunScoreSetting(unsigned long long processID)
{
    unsigned int samplesPassed = 0;
    unsigned int totalSamples = 0;
    unsigned long long numSamples = sizeof(gtSamples) / sizeof(gtSamples[0]);

    // Init the score
    ScoreFunction < DATA_LENGTH,
        24000,
                    1000,
                    1000,
                    NUMBER_TEST_PROCESSORS>* pScore;

    if (!allocatePool(sizeof(ScoreFunction < DATA_LENGTH,
        24000,
        1000,
        1000,
        NUMBER_TEST_PROCESSORS>), (void**)&pScore))
    {
        logToConsole(L"Allocate score failed!");
        return false;
    }
    pScore->initMemory();

    unsigned long long avgTime = 0;

    // Running all samples here
    for (unsigned long long testIndex = 0; testIndex < numSamples; testIndex++)
    {
        m256i testPublicKey = m256i::zero();
        m256i testMiningSeed = m256i::zero();
        m256i testNonce = m256i::zero();

        hexToByte(gtSamples[testIndex][0], testMiningSeed.m256i_u8, 32);
        hexToByte(gtSamples[testIndex][1], testPublicKey.m256i_u8, 32);
        hexToByte(gtSamples[testIndex][2], testNonce.m256i_u8, 32);

        // Init mining data
        pScore->initMiningData(testMiningSeed);

        // Run score computation
        unsigned long long startTime = __rdtsc();
        unsigned int scoreVal = (*pScore)(processID, testPublicKey, testMiningSeed, testNonce);
        avgTime += __rdtsc() - startTime;

        //if (scoreVal != gtScores[testIndex][settingID])
        //{
        //    setText(loginfo, L" FAILED (");
        //    appendNumber(loginfo, scoreVal, false);
        //    appendText(loginfo, L" vs gt: ");
        //    appendNumber(loginfo, gtScores[testIndex][settingID], false);
        //    appendText(loginfo, L" )");
        //    diagLogToConsole(loginfo);
        //}
        //else
        {
            samplesPassed++;
        }

        totalSamples++;
    }

    avgTime = avgTime * 1000 / frequency;
    avgTime = avgTime / numSamples;

    setText(loginfo, L"        AvgTime: ");
    appendNumber(loginfo, avgTime, false);
    appendText(loginfo, L" ms");
    diagLogToConsole(loginfo);

    // Clean up
    pScore->freeMemory();

    // Deallocated
    freePool(pScore);

    return (totalSamples == samplesPassed);

}

bool RunScoreTest(unsigned long long processID)
{
    unsigned int testPassed = 0;
    unsigned int totalTests = 0;
    unsigned int numSettings = sizeof(gtScores[0]) / sizeof(gtScores[0][0]);
    unsigned int numSamples = sizeof(gtSamples) / sizeof(gtSamples[0]);

    setText(loginfo, L"Running score test with ");
    appendNumber(loginfo, numSettings, false);
    appendText(loginfo, L" setting on ");
    appendNumber(loginfo, numSamples, false);
    appendText(loginfo, L" samples.");

    // Run per settings
    for (unsigned int setting = 0; setting < numSettings; setting++)
    {
        setText(loginfo, L"Test ");
        appendNumber(loginfo, setting + 1, false);
        appendText(loginfo, L" / ");
        appendNumber(loginfo, numSettings, false);
        appendText(loginfo, L": NEURONS:");
        appendNumber(loginfo, score_params::kSettings[setting][score_params::NR_NEURONS], false);
        appendText(loginfo, L", NN: ");
        appendNumber(loginfo, score_params::kSettings[setting][score_params::NR_NEIGHBOR_NEURONS], false);
        appendText(loginfo, L", DUR: ");
        appendNumber(loginfo, score_params::kSettings[setting][score_params::DURATIONS], false);
        diagLogToConsole(loginfo);

        bool sts = false;
        switch (setting)
        {
        case 0:
            sts = RunScoreSetting<0>(processID);
            break;
        case 1:
            sts = RunScoreSetting<1>(processID);
            break;
        case 2:
            sts = RunScoreSetting<2>(processID);
            break;
        case 3:
            sts = RunScoreSetting<3>(processID);
            break;
        case 4:
            sts = RunScoreSetting<4>(processID);
            break;
        case 5:
            sts = RunScoreSetting<5>(processID);
            break;
        case 6:
            sts = RunScoreSetting<6>(processID);
            break;
        case 7:
            sts = RunScoreSetting<7>(processID);
            break;
        case 8:
            sts = RunScoreSetting<8>(processID);
            break;
        case 9:
            sts = RunScoreSetting<9>(processID);
            break;
        case 10:
            sts = RunScoreSetting<10>(processID);
            break;
        case 11:
            sts = RunScoreSetting<11>(processID);
            break;
        case 12:
            sts = RunScoreSetting<12>(processID);
            break;
        case 13:
            sts = RunScoreSetting<13>(processID);
            break;
        case 14:
            sts = RunScoreSetting<14>(processID);
            break;
        case 15:
            sts = RunScoreSetting<15>(processID);
            break;
        case 16:
            sts = RunScoreSetting<16>(processID);
            break;
        case 17:
            sts = RunScoreSetting<17>(processID);
            break;
        case 18:
            sts = RunScoreSetting<18>(processID);
            break;
        case 19:
            sts = RunScoreSetting<19>(processID);
            break;
        default:
            diagLogToConsole(L"Unknown score setting!");
            break;
        }
        if (sts)
        {
            setText(loginfo, L"    PASSED");
            testPassed++;
        }
        else
        {
            setText(loginfo, L"    FAILED");
        }
        totalTests++;
        diagLogToConsole(loginfo);
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

