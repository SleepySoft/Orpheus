#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=7);
    const char* mode=argv[1]; unsigned quantum=(unsigned)atoi(argv[2]); int first=3;
    CHECK(orpheus_generated_init(48000,quantum)==ORPHEUS_OK);
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define PROCESS() (!strcmp(mode,"task") ? orpheus_generated_process_task_default(0) : orpheus_generated_process(quantum))
#else
    CHECK(argc>=9);
    const char* mode=argv[3]; unsigned quantum=(unsigned)atoi(argv[4]); int first=5;
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])==ORPHEUS_OK);
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define PROCESS() (!strcmp(mode,"task") ? runtime.process_task("default",0) : runtime.process_block(quantum))
#endif
    CHECK((argc-first)%4==0);
    FILE* file=fopen("halfcomplex.bin","wb"); CHECK(file);
    float values[8192];
    for (unsigned block=0;block<3;++block) {
        CHECK(PROCESS()==ORPHEUS_OK);
        for (int i=first;i<argc;i+=4) {
            OrpheusValue value={ORPHEUS_VALUE_INT,{0}};
            CHECK(GET(argv[i],"calls",&value)==ORPHEUS_OK && value.value.i32==(int32_t)(block+1));
            CHECK(GET(argv[i],"quantum",&value)==ORPHEUS_OK && value.value.i32==atoi(argv[i+3]));
            unsigned packed=(unsigned)atoi(argv[i+1]),decoded=(unsigned)atoi(argv[i+2]);
            CHECK(packed<=8192 && decoded<=8192);
            CHECK(READ(argv[i],"packed_values",values,packed)==ORPHEUS_OK);
            CHECK(fwrite(values,4,packed,file)==packed);
            CHECK(READ(argv[i],"decoded_values",values,decoded)==ORPHEUS_OK);
            CHECK(fwrite(values,4,decoded,file)==decoded);
        }
    }
    CHECK(fclose(file)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
