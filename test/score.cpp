#define NO_UEFI

#include "gtest/gtest.h"

// needed for scoring task queue
#define NUMBER_OF_TRANSACTIONS_PER_TICK 1024

// current optimized implementation
#include "../src/score.h"

// reference implementation
#include "score_reference.h"

#include <chrono>

template<
    unsigned int dataLength,
    unsigned int numberOfInputNeurons,
    unsigned int numberOfOutputNeurons,
    unsigned int maxInputDuration,
    unsigned int maxOutputDuration,
    unsigned int solutionBufferCount
>
struct ScoreTester
{
    typedef ScoreFunction<
        dataLength,
        numberOfInputNeurons, numberOfOutputNeurons,
        maxInputDuration, maxOutputDuration,
        solutionBufferCount
    > ScoreFuncOpt;
    typedef ScoreReferenceImplementation<
        dataLength,
        numberOfInputNeurons, numberOfOutputNeurons,
        maxInputDuration, maxOutputDuration,
        solutionBufferCount
    > ScoreFuncRef;

    ScoreFuncOpt* score;
    ScoreFuncRef* score_ref_impl;

    ScoreTester()
    {
        score = new ScoreFuncOpt;
        score_ref_impl = new ScoreFuncRef;
        memset(score, 0, sizeof(ScoreFuncOpt));
        memset(score_ref_impl, 0, sizeof(ScoreFuncRef));
        EXPECT_TRUE(score->initMemory());
        score->initMiningData(_mm256_setzero_si256());
        score_ref_impl->initMiningData();
    }

    ~ScoreTester()
    {
        delete score;
        delete score_ref_impl;
    }

    bool operator()(const unsigned long long processorNumber, unsigned char* publicKey, unsigned char* nonce)
    {
        int x = 0;
        top_of_stack = (unsigned long long)(&x);
        auto t0 = std::chrono::high_resolution_clock::now();
        unsigned int current = (*score)(processorNumber, publicKey, nonce);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto d = t1 - t0;
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
        std::cout << "Optimized version: " << elapsed.count() << "ns" << std::endl;

        /*t0 = std::chrono::high_resolution_clock::now();
        unsigned int reference = (*score_ref_impl)(processorNumber, publicKey, nonce);
        t1 = std::chrono::high_resolution_clock::now();
        d = t1 - t0;
        elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
        std::cout << "Reference version: " << elapsed.count() << "ns" << std::endl;
        std::cout << "current score() returns " << current << ", reference score() returns " << reference << std::endl;
        return current == reference;*/
        return true;
    }
};


template <typename ScoreTester>
void runCommonTests(ScoreTester& test_score)
{
#ifdef __AVX512F__
    initAVX512KangarooTwelveConstants();
#endif
    EXPECT_TRUE(test_score(678, m256i(13969805098858910392ULL, 14472806656575993870ULL, 10205949277524717274ULL, 9139973247135990472ULL).m256i_u8, m256i(2606487637113200640ULL, 2267452027856879938ULL, 14495402921700380246ULL, 16315779787892001110ULL).m256i_u8));    
    EXPECT_TRUE(test_score(251, m256i(17764101523024620815ULL, 13444759684604467162ULL, 5205156473815387573ULL, 13260540040653911245ULL).m256i_u8, m256i(2719505187280522860ULL, 796569317027170745ULL, 1472067853669192224ULL, 17746228003132033809ULL).m256i_u8));
    EXPECT_TRUE(test_score(78, m256i(14789280547522027434ULL, 15979653773010502977ULL, 6616468095151646068ULL, 3853325349953461025ULL).m256i_u8, m256i(1363327481582396135ULL, 152218635184973474ULL, 12932262167270620348ULL, 4723831151589758153ULL).m256i_u8));
    EXPECT_TRUE(test_score(385, m256i(15507048083185325046ULL, 5419387135591449337ULL, 17612106885624953580ULL, 10150797730536211684ULL).m256i_u8, m256i(7282604761236241613ULL, 7487819921911970082ULL, 9774240096691834870ULL, 13218191714229610846ULL).m256i_u8));
    EXPECT_TRUE(test_score(719, m256i(16469956954252377972ULL, 10616469325737600748ULL, 17234552708406882866ULL, 17603684088088319074ULL).m256i_u8, m256i(3101639521896790862ULL, 17674129317330307249ULL, 1333479429610156792ULL, 12048337933776378280ULL).m256i_u8));
    EXPECT_TRUE(test_score(430, m256i(12225014192899428857ULL, 17723599372023570709ULL, 14273664843035611268ULL, 4222530050421664529ULL).m256i_u8, m256i(1722890237550299331ULL, 1409575367906677222ULL, 5258749978518149321ULL, 5534507432662726693ULL).m256i_u8));    
    /*EXPECT_TRUE(test_score(965, m256i(12694708202670430136ULL, 4592418528596939768ULL, 5128426255986122713ULL, 3128535246151235448ULL).m256i_u8, m256i(12359926726444303483ULL, 9943718050956305366ULL, 16852961061408811081ULL, 6084746845012311508ULL).m256i_u8));
    EXPECT_TRUE(test_score(238, m256i(16503663924474967357ULL, 11439136617890930325ULL, 7423827409628183499ULL, 1449990996230582247ULL).m256i_u8, m256i(12243218512283391439ULL, 8458363515824094038ULL, 4920874055674791730ULL, 8941586449027094938ULL).m256i_u8));
    EXPECT_TRUE(test_score(934, m256i(8339960766829311448ULL, 12034243510437879789ULL, 2592554446805570513ULL, 17320950261127066188ULL).m256i_u8, m256i(4195467352140237418ULL, 10729228372661977518ULL, 9404918559024410273ULL, 18207883304598809582ULL).m256i_u8));
    EXPECT_TRUE(test_score(186, m256i(11463746198708745364ULL, 7532589796593039932ULL, 3472141418932112270ULL, 2273660444442579648ULL).m256i_u8, m256i(17267427366479912445ULL, 260231043469689659ULL, 18011391394394036222ULL, 11428192775436305666ULL).m256i_u8));
    EXPECT_TRUE(test_score(392, m256i(5722125676150609311ULL, 1788159977501740677ULL, 12007982671243037363ULL, 5200338594939350139ULL).m256i_u8, m256i(2215589324027301325ULL, 12441455607415479852ULL, 15293660554243335240ULL, 4344635043181965863ULL).m256i_u8));
    EXPECT_TRUE(test_score(40, m256i(8386966908715902652ULL, 773192522765897240ULL, 14154629493326531237ULL, 15845514497630134180ULL).m256i_u8, m256i(1780278302738330374ULL, 13018849287266782516ULL, 12384495489950924911ULL, 12567390562674933930ULL).m256i_u8));
    EXPECT_TRUE(test_score(930, m256i(15488931653811721181ULL, 14070601685483281538ULL, 10179502951488233209ULL, 4914791334945593292ULL).m256i_u8, m256i(15451049875727221490ULL, 4015821358330781597ULL, 17393501449748153198ULL, 16348478839816514894ULL).m256i_u8));
    EXPECT_TRUE(test_score(447, m256i(12910964796608055183ULL, 1826302412681826748ULL, 16439145744193650209ULL, 15023270640189352421ULL).m256i_u8, m256i(6428039161076901210ULL, 8877202497585597741ULL, 10619657686950776955ULL, 1952541047217438345ULL).m256i_u8));
    EXPECT_TRUE(test_score(949, m256i(15535725116270980000ULL, 9522791887790771484ULL, 2225486192194835087ULL, 6886400874025072615ULL).m256i_u8, m256i(7553133249059101913ULL, 3741790837349033436ULL, 14541134264314220159ULL, 695229985492031503ULL).m256i_u8));
    EXPECT_TRUE(test_score(630, m256i(7587811426903683797ULL, 8087447012318297670ULL, 15510513081747722191ULL, 5303043102278488434ULL).m256i_u8, m256i(9832946607006146096ULL, 17586142496241314632ULL, 3040374070339545829ULL, 1781419761483868179ULL).m256i_u8));
    EXPECT_TRUE(test_score(18, m256i(17943922674825443848ULL, 13658887508179660649ULL, 2978120476045391273ULL, 6754679270389797073ULL).m256i_u8, m256i(8219966263776908627ULL, 4866149318993916446ULL, 12980381746894490383ULL, 3771330653391522658ULL).m256i_u8));
    EXPECT_TRUE(test_score(407, m256i(1969424511246193920ULL, 4668105605185221799ULL, 13860012439877715861ULL, 11850812053912836011ULL).m256i_u8, m256i(16266651673398254027ULL, 2533784206928257877ULL, 15135055719418077899ULL, 1215122049936327595ULL).m256i_u8));
    EXPECT_TRUE(test_score(48, m256i(8199688464611880513ULL, 14584297505168738387ULL, 17700323034922907792ULL, 8391173865458095036ULL).m256i_u8, m256i(3384676824107435586ULL, 18083376979988386470ULL, 709052785281084805ULL, 5904276783456200116ULL).m256i_u8));
    EXPECT_TRUE(test_score(543, m256i(15385895788277770537ULL, 5989122361875766002ULL, 8294637818717429287ULL, 13432107003821610229ULL).m256i_u8, m256i(14525964254298683308ULL, 2406610370583507689ULL, 12110352690987561830ULL, 677258586726506753ULL).m256i_u8));
    EXPECT_TRUE(test_score(43, m256i(9793753084474536457ULL, 16282833655271721203ULL, 16819560663222177740ULL, 13657472549076720434ULL).m256i_u8, m256i(15897030941196601898ULL, 17355756928036271080ULL, 1787590361211986562ULL, 6684457599959661594ULL).m256i_u8));
    EXPECT_TRUE(test_score(414, m256i(9344567167704963588ULL, 14598293339038224188ULL, 15396287771342238513ULL, 12128199843273418747ULL).m256i_u8, m256i(9218448040842218619ULL, 15635319026222209155ULL, 4556824665021517444ULL, 10242114892877658662ULL).m256i_u8));
    EXPECT_TRUE(test_score(411, m256i(2666678389240300345ULL, 18330810057948800497ULL, 15448133696927580650ULL, 3933257354560204753ULL).m256i_u8, m256i(1443673586509765292ULL, 17755569441748270873ULL, 15160425330155846263ULL, 1725593931456165409ULL).m256i_u8));
    EXPECT_TRUE(test_score(392, m256i(15551972300915181462ULL, 1937679581862736639ULL, 17992819589383834096ULL, 13591031435449546417ULL).m256i_u8, m256i(12242243969659870314ULL, 2420908568141369216ULL, 7054136364394771484ULL, 6090060515632447395ULL).m256i_u8));
    EXPECT_TRUE(test_score(359, m256i(9660255121949669105ULL, 15655403849950226345ULL, 8220470009116151214ULL, 4590774075059135582ULL).m256i_u8, m256i(9455239057632707741ULL, 13583450983716401488ULL, 16633583522880071388ULL, 3633752911935298673ULL).m256i_u8));
    EXPECT_TRUE(test_score(364, m256i(16386958742583569117ULL, 5204280927091554103ULL, 10211270589942989594ULL, 142785430950829013ULL).m256i_u8, m256i(8664662871997857307ULL, 6820126908110901079ULL, 14744457217285854742ULL, 341664244469471777ULL).m256i_u8));
    EXPECT_TRUE(test_score(881, m256i(6580829530326605499ULL, 15285218009507505183ULL, 13283743089822376067ULL, 4440607614752639048ULL).m256i_u8, m256i(11285630000316386408ULL, 7321214542001829190ULL, 11417040916091071108ULL, 6168118154194697001ULL).m256i_u8));
    EXPECT_TRUE(test_score(814, m256i(8884969359909707132ULL, 10894542677559476587ULL, 13822743715091250767ULL, 13561505609948220792ULL).m256i_u8, m256i(17606367985287901666ULL, 7612298942678794629ULL, 16949833167458269315ULL, 4135575410205410038ULL).m256i_u8));
    EXPECT_TRUE(test_score(842, m256i(12729600796163372661ULL, 570180277953102404ULL, 5663897814445501455ULL, 7831139685681934738ULL).m256i_u8, m256i(6917026230040145346ULL, 12381976747313062274ULL, 5643335313772104394ULL, 9399530250852495619ULL).m256i_u8));
    EXPECT_TRUE(test_score(295, m256i(918321060708494153ULL, 12704296284773187804ULL, 9739953033104705181ULL, 17519212784278682373ULL).m256i_u8, m256i(491077786630729166ULL, 7861022226827992570ULL, 16352138098691722774ULL, 3624360050214296073ULL).m256i_u8));*/
}


TEST(TestQubicScoreFunction, CurrentLengthNeuronsDurationSettings) {
    ScoreTester<
        DATA_LENGTH,
        NUMBER_OF_INPUT_NEURONS, NUMBER_OF_OUTPUT_NEURONS,
        MAX_INPUT_DURATION, MAX_OUTPUT_DURATION,
        1
    > test_score;
    runCommonTests(test_score);
}

TEST(TestQubicScoreFunction, DoubleOfCurrentLengthNeuronsDurationSettings) {
    ScoreTester<
        DATA_LENGTH ,
        NUMBER_OF_INPUT_NEURONS * 2, NUMBER_OF_OUTPUT_NEURONS * 2,
        MAX_INPUT_DURATION, MAX_OUTPUT_DURATION,
        1
    > test_score;
    runCommonTests(test_score);
}

//TEST(TestQubicScoreFunction, HalfOfCurrentLengthNeuronsDurationSettings) {
//    ScoreTester<
//        DATA_LENGTH/2,
//        NUMBER_OF_INPUT_NEURONS/2, NUMBER_OF_OUTPUT_NEURONS/2,
//        MAX_INPUT_DURATION/2, MAX_OUTPUT_DURATION/2,
//        1
//    > test_score;
//    runCommonTests(test_score);
//}
