//
// Created by moe on 17.07.19.
//

#include "BaseApp.h"
#include "BenchThread.h"
#include "GlobalVariables.h"

#ifdef RUNTIME_PARSER
#include "InputParser.h"
#endif


/*  SIGINT handler in order to stop the benchmark with Ctrl+C   */
void InterruptHandler(int dummy)
{
    (void)dummy;
    stop_bench();
}

#define DUMMY_INDEX     1
#define MAX_TEST_NAME_LENGTH 32
static char test_names[NUM_OF_TEST_MODULES + DUMMY_INDEX][MAX_TEST_NAME_LENGTH] = {
        "NONE"
#ifdef CUSTOM_SHA256_TEST
        , "custom SHA256 test"
#endif

#ifdef CUSTOM_TEST
        , "custom test"
#endif

#ifdef RSA_KEY_GEN
        , "rsa key gen"
#endif

#ifdef ELLIPTIC_CURVE_KEY_GEN
        , "EC key gen"
#endif

#ifdef DNA_PATTERN_MATCHING
        , "DNA matching"
#endif

#ifdef RSA_TESTS
        , "RSA tests"
#endif

#ifdef ELLIPTIC_CURVE_TESTS
        , "EC tests"
#endif

#ifdef ELLIPTIC_CURVE_DIFFIE_HELLMAN_TESTS
        , "EC & DH tests"
#endif

#ifdef ELLIPTIC_CURVE_DSA_TESTS
        , "EC & DSA tests"
#endif

#ifdef BN_TESTS
        , "BN Tests"
#endif

#ifdef DEFFIE_HELLMAN_TESTS
        , "DH tests"
#endif

#ifdef SECURE_HASH_ALGORITHM_256
        , "SHA256"
#endif

#ifdef SECURE_HASH_ALGORITHM_1
        , "SHA1"
#endif

#ifdef THREAD_TESTS
        , "Multi-Thread tests"
#endif
};

static volatile int do_bench = 0;
static volatile int abort_measure = 0;
volatile uint64_t counter = 0;


typedef struct {
    uint64_t warmCnt;
    uint64_t runCnt;
} measurement_t;


measurement_t array[NUM_OF_TEST_MODULES];
uint64_t cur_elem = 0;
uint32_t a;


void doWarmUp()
{
    TimeVar initTime = timeNow();
    TimeVar currentTime;
    do
    {
        __asm__("pause");
        currentTime = timeNow();
    } while(abort_measure == 0 && duration(currentTime - initTime) <  GLOBAL_CONFIG.WARMUP_TIME);
}

void doRuntime()
{
    TimeVar initTime = timeNow();
    TimeVar curTime = timeNow();
    while(abort_measure == 0 && duration(curTime - initTime) < GLOBAL_CONFIG.RUNTIME)
    {
        curTime = timeNow();
        __asm__("pause");
    }
}

static inline void add_warm_measurement()
{
    array[cur_elem].warmCnt = counter;
    __sync_fetch_and_and(&counter,((uint64_t)0)); // reset the counter for the runtime phase
}


static inline void add_runtime_measurement()
{
    array[cur_elem].runCnt = counter;
    __sync_fetch_and_and(&counter,((uint64_t)0)); // reset the counter for the possible next warmup phase
    ++cur_elem;                                   // next element in the array for the next test
}



void *measure_thread(void *args)
{
    while(do_bench == 0)
    {
        __asm__("pause");
    }

    doWarmUp();                                 //do the warm up before starting
    add_warm_measurement();                     // add the warmup results and reset the tests counter

    doRuntime();                                // do the runtime
    add_runtime_measurement();                  // add the runtime results, reset the tests counter and increment the pointer to the next test
    InterruptHandler(0);
    return nullptr;
}

uint64_t WORKER_THREADS = 1;
pthread_barrier_t worker_barrier;

void *worker_thread(void *args)
{
    int *argptr = (int*) args;
    int test_id = *argptr;

    pthread_barrier_wait(&worker_barrier);  // register +1 to the thread barrier instance
    while(do_bench == 0)
    {
        __asm__("pause");
    }
    run_bench(test_id);
    return nullptr;
}

#define BASELINE_DATA_FILENAME  "BaselineData.txt"
static void print_array()
{
    //ToDo do the format of the output file
#ifdef WRITE_LOG_FILE
    FILE *fp;
    fp = fopen(BASELINE_DATA_FILENAME, "a");
    if (fp == NULL)
    {
        fprintf(stderr, "Couldnt open or create a file for the plot data!\n");
    }
#endif

    // print array either to an output file or to the console
    for (int i = 0; i < NUM_OF_TEST_MODULES; ++i)
    {
        float warmRate    = (float)array[i].warmCnt / (float)GLOBAL_CONFIG.WARMUP_TIME;
        float runtimeRate = (float)array[i].runCnt  / (float)GLOBAL_CONFIG.RUNTIME;
#ifdef WRITE_LOG_FILE
        fprintf(fp,"%s,%lu,%.5f,%lu,%.5f\n", test_names[i + DUMMY_INDEX], array[i].warmCnt, warmRate, array[i].runCnt, runtimeRate);
#else
        printf("%s,%lu, %.5f, %lu, %.5f\n", test_names[i + DUMMY_INDEX], array[i].warmCnt, warmRate, array[i].runCnt, runtimeRate);   //ToDo: think of an idea to append the name of the test ran for this calculation
#endif
    }
#ifdef WRITE_LOG_FILE
    fprintf(stderr, "Results are saved in a text file with the name: %s\n", BASELINE_DATA_FILENAME);
    fclose(fp);
#endif
}


static void run_tests()
{
    for (int test_id = 1; test_id < NUM_OF_TEST_MODULES+DUMMY_INDEX; ++test_id)
    {
        abort_measure = 0;
        pthread_t measure, worker[WORKER_THREADS];
        pthread_create(&measure, nullptr, measure_thread, nullptr); // start the measure thread
        pthread_barrier_init(&worker_barrier, nullptr, WORKER_THREADS + 1);
        for (int j = 0; j < (int)WORKER_THREADS; ++j)
        {
            //ToDo danger: i didnt pass by value because we only have 1 worker thread and its okay in this case but
            // if multiple threads then its better to pass by value as thread creation and execution may differ
            pthread_create(worker+j, nullptr, worker_thread, &test_id);
        }
        pthread_barrier_wait(&worker_barrier);

        fprintf(stderr, "Starting to benchmark the Module %s \n", test_names[test_id]);
        counter = 0;
        start_bench();
        do_bench = 1;


        for (int j = 0; j < (int)WORKER_THREADS; ++j)
        {
            fprintf(stderr, "Joining worker %d\n", j);
            pthread_join(worker[j], nullptr);
        }

        abort_measure = 1;
        fprintf(stderr, "Joining measure \n");
        pthread_join(measure, nullptr);

        pthread_barrier_destroy(&worker_barrier);
    }
}

static void exec_bench_setup()
{

    set_config((uint64_t *)&counter, &GLOBAL_CONFIG);

    // Run the benchmarks for each chosen test
    run_tests();

    // Print the array to an output file with some statistics information. For example, the rate of the executed tests per seconds
    print_array();
}

/* Application entry */
int main(int argc, char *argv[])
{
#ifdef RUNTIME_PARSER
    parseInput(argc, argv);
#else
    (void)(argc);
    (void)(argv);
#endif

#ifdef PRINT_CHECKS
    fprintf(stderr, "# Warmup phase: %lus\n", GLOBAL_CONFIG.WARMUP_TIME);
    fprintf(stderr, "# Runtime phase: %lus\n", GLOBAL_CONFIG.RUNTIME);

#endif

    signal(SIGINT, InterruptHandler);

    /* Initialize the enclave and execute the benchmarking setup */
    exec_bench_setup();

    return 0;
}