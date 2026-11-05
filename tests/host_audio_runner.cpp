#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#ifdef ORPHEUS_TEST_GENERATED
extern "C" {
#include "orpheus_graph.h"
#include "orpheus_control.h"
}
#ifndef ORPHEUS_TEST_TASK_FUNCTION
#define ORPHEUS_TEST_TASK_FUNCTION orpheus_generated_process_task_default
#endif
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=7);
    const char* mode=argv[1];unsigned quantum=(unsigned)atoi(argv[2]),blocks=(unsigned)atoi(argv[3]);int first=4;
    CHECK(orpheus_generated_init(48000,quantum)==ORPHEUS_OK);
#define WRITE(n,k,v,c) orpheus_control_write_bulk(n,k,v,c)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define SET(n,k,v) orpheus_control_set_value(n,k,v)
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define PROCESS() (!strcmp(mode,"task") ? ORPHEUS_TEST_TASK_FUNCTION(0) : orpheus_generated_process(quantum))
#else
    CHECK(argc>=9);
    const char* mode=argv[3];unsigned quantum=(unsigned)atoi(argv[4]),blocks=(unsigned)atoi(argv[5]);int first=6;
    orpheus::Runtime runtime;
    const orpheus::Plan plan=orpheus::Plan::load_from_file(argv[1]);CHECK(plan.tasks.size()==1);
    const std::string& task=plan.tasks.front().id;
    CHECK(runtime.load_plan(plan,argv[2])==ORPHEUS_OK);
#define WRITE(n,k,v,c) runtime.write_bulk(n,k,v,c)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define SET(n,k,v) runtime.set_parameter(n,k,v)
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define PROCESS() (!strcmp(mode,"task") ? runtime.process_task(task,0) : runtime.process_block(quantum))
#endif
    CHECK((argc-first)%3==0 && quantum>0 && blocks>0);
    FILE* file=fopen("host_audio.bin","wb");CHECK(file);
    for (unsigned block=0;block<blocks;++block) {
        for (int i=first;i<argc;i+=3) {
            if (strcmp(argv[i],"I")) continue;
            unsigned channels=(unsigned)atoi(argv[i+2]);CHECK(channels>0 && channels<=64);
            std::vector<float> values((size_t)channels*quantum);
            for (unsigned f=0;f<quantum;++f) for (unsigned c=0;c<channels;++c)
                values[(size_t)f*channels+c]=((int)((block*17+f*3+c*13)%257)-128)*0.0009765625f;
            CHECK(WRITE(argv[i+1],"samples",values.data(),values.size())==ORPHEUS_OK);
            OrpheusValue ready={ORPHEUS_VALUE_BOOL,{0}};ready.value.b=true;
            CHECK(SET(argv[i+1],"ready",ready)==ORPHEUS_OK);
        }
        CHECK(PROCESS()==ORPHEUS_OK);
        for (int i=first;i<argc;i+=3) {
            OrpheusValue flag={ORPHEUS_VALUE_BOOL,{0}};
            if (!strcmp(argv[i],"I")) {
                CHECK(GET(argv[i+1],"ready",&flag)==ORPHEUS_OK && !flag.value.b);
                continue;
            }
            CHECK(!strcmp(argv[i],"O"));
            CHECK(GET(argv[i+1],"valid",&flag)==ORPHEUS_OK && flag.value.b);
            unsigned channels=(unsigned)atoi(argv[i+2]);std::vector<float> values((size_t)channels*quantum);
            CHECK(READ(argv[i+1],"samples",values.data(),values.size())==ORPHEUS_OK);
            CHECK(fwrite(values.data(),sizeof(float),values.size(),file)==values.size());
        }
    }
    CHECK(fclose(file)==0);
    CHECK(PROCESS()!=ORPHEUS_OK);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
